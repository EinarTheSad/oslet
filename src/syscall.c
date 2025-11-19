#include "syscall.h"
#include "task.h"
#include "console.h"
#include "timer.h"
#include "drivers/fat32.h"
#include "drivers/vga.h"
#include "exec.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include <stddef.h>
#include "drivers/graphics.h"
#include "drivers/keyboard.h"

/* File descriptor table - kernel side */
#define MAX_OPEN_FILES 32
typedef struct {
    fat32_file_t *file;
    int in_use;
} file_descriptor_t;

static file_descriptor_t fd_table[MAX_OPEN_FILES];

static void fd_init(void) {
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].file = NULL;
        fd_table[i].in_use = 0;
    }
}

static int fd_alloc(fat32_file_t *file) {
    for (int i = 3; i < MAX_OPEN_FILES; i++) { /* Reserve 0,1,2 for stdin/stdout/stderr */
        if (!fd_table[i].in_use) {
            fd_table[i].file = file;
            fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1;
}

static fat32_file_t* fd_get(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return NULL;
    if (!fd_table[fd].in_use) return NULL;
    return fd_table[fd].file;
}

static void fd_free(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return;
    fd_table[fd].in_use = 0;
    fd_table[fd].file = NULL;
}

static int validate_ptr(uint32_t ptr) {
    task_t *current = task_get_current();
    if (!current) return 0;
    
    if (!current->user_mode) {
        return (ptr != 0);
    }
    
    return (ptr >= EXEC_LOAD_ADDR && ptr < 0xC0000000);
}

static uint32_t handle_console(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: /* Write string */
            if (!validate_ptr(ebx)) return -1;
            printf("%s", (const char*)ebx);
            return 0;

        case 0x01: /* Read string */
            /* Not yet */
            return 0;

        case 0x02: /* Set color */
            vga_set_color((uint8_t)ebx, (uint8_t)ecx);
            return 0;
        
        case 0x03: /* Get character */
            return (uint32_t)kbd_getchar();
        
        case 0x04: /* Clear screen */
            vga_clear();
            return 0;
            
        default:
            return -1;
    }
}

static uint32_t handle_process(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00: /* Exit */
            task_exit();
            return 0;
            
        case 0x01: { /* Exec */
            if (!validate_ptr(ebx)) return -1;
            exec_image_t image;
            if (exec_load((const char*)ebx, &image) != 0) return -1;
            if (exec_run(&image) != 0) return -1;
            exec_free(&image);
            return 0;
        }
            
        case 0x02: { /* Get PID */
            task_t *current = task_get_current();
            return current ? current->tid : 0;
        }
            
        case 0x03: /* Sleep */
            task_sleep(ebx);
            return 0;
            
        case 0x04: /* Yield */
            task_yield();
            return 0;
            
        default:
            return -1;
    }
}

static uint32_t handle_file(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: { /* Open */
            if (!validate_ptr(ebx) || !validate_ptr(ecx)) return (uint32_t)-1;
            
            fat32_file_t *file = fat32_open((const char*)ebx, (const char*)ecx);
            if (!file) return (uint32_t)-1;
            
            int fd = fd_alloc(file);
            if (fd < 0) {
                fat32_close(file);
                return (uint32_t)-1;
            }
            return (uint32_t)fd;
        }
            
        case 0x01: { /* Close */
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            fat32_close(file);
            fd_free((int)ebx);
            return 0;
        }
            
        case 0x02: { /* Read */
            if (!validate_ptr(ecx)) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_read(file, (void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x03: { /* Write */
            if (!validate_ptr(ecx)) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_write(file, (const void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x04: { /* Seek */
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_seek(file, ecx);
            return (uint32_t)result;
        }
        
        case 0x05: { /* Delete/Unlink */
            if (!validate_ptr(ebx)) return (uint32_t)-1;
            int result = fat32_unlink((const char*)ebx);
            return (uint32_t)result;
        }
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_dir(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: /* Chdir */
            if (!validate_ptr(ebx)) return -1;
            return fat32_chdir((const char*)ebx);
            
        case 0x01: /* Getcwd */
            if (!validate_ptr(ebx)) return -1;
            char *result = fat32_getcwd((char*)ebx, ecx);
            if (!result || ((char*)ebx)[0] == '\0') {
                if (ecx >= 4) {
                    memcpy_s((void*)ebx, "C:/", 4);
                    return (uint32_t)ebx;
                }
            }
            return (uint32_t)result;
        
        case 0x02: /* Mkdir */
            if (!validate_ptr(ebx)) return -1;
            return fat32_mkdir((const char*)ebx);
        
        case 0x03: /* Rmdir */
            if (!validate_ptr(ebx)) return -1;
            return fat32_rmdir((const char*)ebx);
        
        case 0x04: { /* List directory */
            if (!validate_ptr(ebx) || !validate_ptr(ecx)) return -1;
            
            fat32_dirent_t *fat_entries = (fat32_dirent_t*)kmalloc(sizeof(fat32_dirent_t) * edx);
            if (!fat_entries) return -1;
            
            int count = fat32_list_dir((const char*)ebx, fat_entries, edx);
            
            if (count > 0) {
                sys_dirent_t *sys_entries = (sys_dirent_t*)ecx;
                for (int i = 0; i < count; i++) {
                    memcpy_s(sys_entries[i].name, fat_entries[i].name, 13);
                    sys_entries[i].size = fat_entries[i].size;
                    sys_entries[i].first_cluster = fat_entries[i].first_cluster;
                    sys_entries[i].is_directory = fat_entries[i].is_directory;
                    sys_entries[i].attr = fat_entries[i].attr;
                }
            }
            
            kfree(fat_entries);
            return count;
        }
            
        default:
            return -1;
    }
}

static uint32_t handle_ipc(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: { /* send message */
            if (!validate_ptr(ecx)) return -2;
            if (edx > MSG_MAX_SIZE) return -1;
            
            task_t *sender = task_get_current();
            if (!sender) return -3;
            
            task_t *receiver = task_find_by_tid(ebx);
            if (!receiver) return -4;
            
            msg_queue_t *q = &receiver->msg_queue;
            if (q->count >= MSG_QUEUE_SIZE) return -5;
            
            message_t *msg = &q->msgs[q->head];
            msg->from_tid = sender->tid;
            msg->to_tid = ebx;
            msg->size = edx;
            memcpy_s(msg->data, (const void*)ecx, edx);
            
            q->head = (q->head + 1) % MSG_QUEUE_SIZE;
            q->count++;
            
            if (receiver->state == TASK_BLOCKED) {
                receiver->state = TASK_READY;
            }
            return 0;
        }
            
        case 0x01: { /* receive message */
            if (!validate_ptr(ebx)) return -1;
            
            task_t *current = task_get_current();
            if (!current) return -2;
            
            msg_queue_t *q = &current->msg_queue;
            if (q->count == 0) {
                current->state = TASK_BLOCKED;
                task_yield();
                if (q->count == 0) return -3;
            }
            
            message_t *msg = &q->msgs[q->tail];
            memcpy_s((void*)ebx, msg, sizeof(message_t));
            
            q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
            q->count--;
            return 0;
        }
            
        default:
            return -1;
    }
}

static uint32_t handle_time(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ebx; (void)ecx; (void)edx;
    
    switch (al) {
        case 0x01: /* Uptime */
            return timer_get_ticks();
        
        default:
            return -1;
    }
}

static uint32_t handle_info(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: { /* Memory info */
            if (!validate_ptr(ebx)) return -1;
            
            sys_meminfo_t *info = (sys_meminfo_t*)ebx;
            size_t total = pmm_total_frames();
            static size_t free = 0;
            free = pmm_count_free_frames();
            
            info->total_kb = (total * 4096) / 1024;
            info->free_kb = (free * 4096) / 1024;
            info->used_kb = info->total_kb - info->free_kb;
            return 0;
        }
        
        case 0x01: { /* Task list */
            if (!validate_ptr(ebx)) return -1;
            
            sys_taskinfo_t *tasks = (sys_taskinfo_t*)ebx;
            int max = (int)ecx;
            int count = 0;
            
            task_t *start = task_get_current();
            if (!start) return 0;
            
            task_t *t = start;
            do {
                if (count >= max) break;
                
                tasks[count].tid = t->tid;
                memcpy_s(tasks[count].name, t->name, 32);
                tasks[count].state = (uint8_t)t->state;
                tasks[count].priority = (uint8_t)t->priority;
                tasks[count].user_mode = t->user_mode;
                count++;
                
                t = t->next;
            } while (t != start && count < max);
            
            return count;
        }
        
        default:
            return -1;
    }
}

static uint32_t handle_graphics(uint32_t al, uint32_t ebx, 
                                uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: /* Enter mode */
            gfx_enter_mode();
            return 0;
            
        case 0x01: /* Exit mode */
            gfx_exit_mode();
            return 0;
            
        case 0x02: /* Clear */
            gfx_clear((uint8_t)ebx);
            return 0;
            
        case 0x03: /* Swap buffers */
            gfx_swap_buffers();
            return 0;
            
        case 0x04: /* Putpixel */
            gfx_putpixel((int)ebx, (int)ecx, (uint8_t)edx);
            return 0;
            
        case 0x05: { /* Line - używa struktury */
            if (!validate_ptr(ebx)) return (uint32_t)-1;
            int *coords = (int*)ebx;
            gfx_line(coords[0], coords[1], coords[2], coords[3], (uint8_t)ecx);
            return 0;
        }
            
        case 0x06: { /* Rect */
            if (!validate_ptr(ebx)) return (uint32_t)-1;
            int *coords = (int*)ebx;
            gfx_rect(coords[0], coords[1], coords[2], coords[3], (uint8_t)ecx);
            return 0;
        }
            
        case 0x07: { /* Fillrect */
            if (!validate_ptr(ebx)) return (uint32_t)-1;
            int *coords = (int*)ebx;
            gfx_fillrect(coords[0], coords[1], coords[2], coords[3], (uint8_t)ecx);
            return 0;
        }
            
        case 0x08: { /* Circle */
            gfx_circle((int)ebx, (int)ecx, (int)(edx >> 8), (uint8_t)(edx & 0xFF));
            return 0;
        }
            
        case 0x09: { /* Print text */
            if (!validate_ptr(edx)) return (uint32_t)-1;
            gfx_print((int)ebx, (int)(ecx >> 16), (const char*)edx, 
                     (uint8_t)((ecx >> 8) & 0xFF));
            return 0;
        }
            
        default:
            return (uint32_t)-1;
    }
}

uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    uint32_t ah = (eax >> 8) & 0xFF;
    uint32_t al = eax & 0xFF;
    
    switch (ah) {
        case 0x01: return handle_console(al, ebx, ecx, edx);
        case 0x02: return handle_process(al, ebx, ecx, edx);
        case 0x03: return handle_file(al, ebx, ecx, edx);
        case 0x04: return handle_dir(al, ebx, ecx, edx);
        case 0x05: return handle_ipc(al, ebx, ecx, edx);
        case 0x07: return handle_time(al, ebx, ecx, edx);
        case 0x08: return handle_info(al, ebx, ecx, edx);
        case 0x09: return handle_graphics(al, ebx, ecx, edx);
        
        default:
            printf("Unknown syscall: AH=%02Xh AL=%02Xh\n", ah, al);
            return (uint32_t)-1;
    }
}

void syscall_init(void) {
    fd_init();
}