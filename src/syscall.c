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
#include "fonts/bmf.h"
#include "rtc.h"

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
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
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

static uint32_t handle_console(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00:
            if (!ebx) return -1;
            printf("%s", (const char*)ebx);
            return 0;

        case 0x01:
            if (!ebx) return -1;
            return (uint32_t)kbd_getline((char*)ebx, ecx);

        case 0x02:
            vga_set_color((uint8_t)ebx, (uint8_t)ecx);
            return 0;
        
        case 0x03:
            return (uint32_t)kbd_getchar();
        
        case 0x04:
            vga_clear();
            return 0;
            
        default:
            return -1;
    }
}

static uint32_t handle_process(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00:
            task_exit();
            return 0;
            
        case 0x01: {
            if (!ebx) return -1;
            exec_image_t image;
            if (exec_load((const char*)ebx, &image) != 0) return -1;
            if (exec_run(&image) != 0) return -1;
            exec_free(&image);
            return 0;
        }
            
        case 0x02: {
            task_t *current = task_get_current();
            return current ? current->tid : 0;
        }
            
        case 0x03:
            task_sleep(ebx);
            return 0;
            
        case 0x04:
            task_yield();
            return 0;
            
        default:
            return -1;
    }
}

static uint32_t handle_file(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: {
            if (!ebx || !ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fat32_open((const char*)ebx, (const char*)ecx);
            if (!file) return (uint32_t)-1;
            
            int fd = fd_alloc(file);
            if (fd < 0) {
                fat32_close(file);
                return (uint32_t)-1;
            }
            return (uint32_t)fd;
        }
            
        case 0x01: {
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            fat32_close(file);
            fd_free((int)ebx);
            return 0;
        }
            
        case 0x02: {
            if (!ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_read(file, (void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x03: {
            if (!ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_write(file, (const void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x04: {
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_seek(file, ecx);
            return (uint32_t)result;
        }
        
        case 0x05: {
            if (!ebx) return (uint32_t)-1;
            int result = fat32_unlink((const char*)ebx);
            return (uint32_t)result;
        }
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_dir(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00:
            if (!ebx) return -1;
            return fat32_chdir((const char*)ebx);
            
        case 0x01:
            if (!ebx) return -1;
            char *result = fat32_getcwd((char*)ebx, ecx);
            if (!result || ((char*)ebx)[0] == '\0') {
                if (ecx >= 4) {
                    memcpy_s((void*)ebx, "C:/", 4);
                    return (uint32_t)ebx;
                }
            }
            return (uint32_t)result;
        
        case 0x02:
            if (!ebx) return -1;
            return fat32_mkdir((const char*)ebx);
        
        case 0x03:
            if (!ebx) return -1;
            return fat32_rmdir((const char*)ebx);
        
        case 0x04: {
            if (!ebx || !ecx) return -1;
            if (edx > 256) edx = 256;
            fat32_dirent_t *fat_entries = (fat32_dirent_t*)kmalloc(sizeof(fat32_dirent_t) * edx);
            if (!fat_entries) return -1;
            
            int count = fat32_list_dir((const char*)ebx, fat_entries, (int)edx);
            
            if (count > 0) {
                sys_dirent_t *sys_entries = (sys_dirent_t*)ecx;
                for (int i = 0; i < count; i++) {
                    memcpy_s(sys_entries[i].name, fat_entries[i].name, sizeof(sys_entries[i].name));
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
        case 0x00: {
            if (!ecx) return -2;
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
            
        case 0x01: {
            if (!ebx) return -1;
            
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
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00: {
            if (!ebx) return -1;
            
            sys_time_t *time = (sys_time_t*)ebx;
            rtc_read_time(time);
            return 0;
        }
        case 0x01:
            return timer_get_ticks();
        
        default:
            return -1;
    }
}

static uint32_t handle_info(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: {
            if (!ebx) return -1;
            
            sys_meminfo_t *info = (sys_meminfo_t*)ebx;
            size_t total = pmm_total_frames();
            static size_t free = 0;
            free = pmm_count_free_frames();
            
            info->total_kb = (total * 4096) / 1024;
            info->free_kb = (free * 4096) / 1024;
            info->used_kb = info->total_kb - info->free_kb;
            return 0;
        }
        
        case 0x01: {
            if (!ebx) return -1;
            
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
                count++;
                
                t = t->next;
            } while (t != start && count < max);
            
            return count;
        }

        case 0x02:
            return (uint32_t)(uintptr_t)kernel_version;

        case 0x03: {
            if (!ebx) return -1;
            sys_heapinfo_t *info = (sys_heapinfo_t*)ebx;
            
            size_t free_blocks = 0;
            size_t used_blocks = 0;
            size_t free_mem = 0;
            size_t used_mem = 0;
            
            block_t *curr = heap_start;
            while (curr) {
                if (curr->free) {
                    free_blocks++;
                    free_mem += curr->size;
                } else {
                    used_blocks++;
                    used_mem += curr->size;
                }
                curr = curr->next;
            }
            
            info->total_kb = (heap_end - HEAP_START) / 1024;
            info->used_bytes = used_mem;
            info->free_bytes = free_mem;
            info->used_blocks = used_blocks;
            info->free_blocks = free_blocks;
            info->total_allocated = total_allocated;
            info->total_freed = total_freed;
            
            return 0;
        }
        
        default:
            return -1;
    }
}

static uint32_t handle_memory(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ecx; (void)edx;
    
    switch (al) {
        case 0x00: {
            void *ptr = kmalloc((size_t)ebx);
            return (uint32_t)ptr;
        }
            
        case 0x01:
            kfree((void*)ebx);
            return 0;
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_graphics(uint32_t al, uint32_t ebx, 
                                uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00:
            gfx_enter_mode();
            return 0;
            
        case 0x01:
            gfx_exit_mode();
            return 0;
            
        case 0x02:
            gfx_clear((uint8_t)ebx);
            return 0;
            
        case 0x03:
            gfx_swap_buffers();
            return 0;
            
        case 0x04:
            gfx_putpixel((int)ebx, (int)ecx, (uint8_t)edx);
            return 0;
            
        case 0x05: { /* Line */
            int x0 = (int)(ebx >> 16);
            int y0 = (int)(ebx & 0xFFFF);
            int x1 = (int)(ecx >> 16);
            int y1 = (int)(ecx & 0xFFFF);
            gfx_line(x0, y0, x1, y1, (uint8_t)edx);
            return 0;
        }
            
        case 0x06: { /* Rect */
            int x = (int)(ebx >> 16);
            int y = (int)(ebx & 0xFFFF);
            int w = (int)(ecx >> 16);
            int h = (int)(ecx & 0xFFFF);
            gfx_rect(x, y, w, h, (uint8_t)edx);
            return 0;
        }
            
        case 0x07: { /* Fillrect */
            int x = (int)(ebx >> 16);
            int y = (int)(ebx & 0xFFFF);
            int w = (int)(ecx >> 16);
            int h = (int)(ecx & 0xFFFF);
            gfx_fillrect(x, y, w, h, (uint8_t)edx);
            return 0;
        }
            
        case 0x08: {
            gfx_circle((int)ebx, (int)ecx, (int)(edx >> 8), (uint8_t)(edx & 0xFF));
            return 0;
        }
            
        case 0x09: {
            /* free */
            return 0;
        }
            
        case 0x0A:
            if (!ebx) return (uint32_t)-1;
            return gfx_load_bmp_4bit((const char*)ebx, (int)ecx, (int)edx);

        case 0x0B: { /* Fillrect gradient */
            int x = (int)(ebx >> 16);
            int y = (int)(ebx & 0xFFFF);
            int w = (int)(ecx >> 16);
            int h = (int)(ecx & 0xFFFF);
            uint8_t c_start = (edx >> 16) & 0xFF;
            uint8_t c_end = (edx >> 8) & 0xFF;
            int orientation = edx & 0xFF;
            
            gfx_fillrect_gradient(x, y, w, h, c_start, c_end, orientation);
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
        case 0x06: return handle_memory(al, ebx, ecx, edx);
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