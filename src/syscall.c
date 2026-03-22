#include "syscall.h"
#include "task/task.h"
#include "console.h"
#include "task/timer.h"
#include "drivers/fat32.h"
#include "drivers/vga.h"
#include "task/exec.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "drivers/sb16.h"
#include "drivers/sound.h"
#include "fonts/bmf.h"
#include "rtc.h"
#include "win/window.h"
#include "win/wm_config.h"
#include "win/icon.h"
#include "win/bitmap.h"
#include "win/wm.h"
#include "win/controls.h"
#include "win/menu.h"
#include "win/compositor.h"
#include "irq/io.h"
#include "win/theme.h"
#include "vconsole.h"

#define MAX_OPEN_FILES 32

typedef struct {
    void *data;
    int width;
    int height;
} cached_bmp_t;

#define FD_CRITICAL_BEGIN \
    uint32_t _fd_eflags; \
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(_fd_eflags) :: "memory")
#define FD_CRITICAL_END \
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(_fd_eflags) : "cc", "memory")
extern int buffer_valid;

/* Global window manager */
static window_manager_t global_wm;
static int wm_initialized = 0;

typedef struct {
    fat32_file_t *file;
    int in_use;
    uint32_t owner_tid;  /* Task ID that owns this file descriptor */
} file_descriptor_t;

static file_descriptor_t fd_table[MAX_OPEN_FILES];

static void fd_init(void) {
    FD_CRITICAL_BEGIN;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].file = NULL;
        fd_table[i].in_use = 0;
        fd_table[i].owner_tid = 0;
    }
    FD_CRITICAL_END;
}

static int fd_alloc(fat32_file_t *file) {
    task_t *current = task_get_current();
    uint32_t tid = current ? current->tid : 0;

    FD_CRITICAL_BEGIN;
    for (int i = 3; i < MAX_OPEN_FILES; i++) {
        if (!fd_table[i].in_use) {
            fd_table[i].file = file;
            fd_table[i].in_use = 1;
            fd_table[i].owner_tid = tid;
            FD_CRITICAL_END;
            return i;
        }
    }
    FD_CRITICAL_END;
    return -1;
}

static fat32_file_t* fd_get(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return NULL;
    FD_CRITICAL_BEGIN;
    if (!fd_table[fd].in_use) { FD_CRITICAL_END; return NULL; }

    /* Check ownership - only the owning task can access this fd */
    task_t *current = task_get_current();
    if (current && fd_table[fd].owner_tid != current->tid) { FD_CRITICAL_END; return NULL; }

    fat32_file_t *file = fd_table[fd].file;
    FD_CRITICAL_END;
    return file;
}

static void fd_free(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return;

    FD_CRITICAL_BEGIN;
    /* Check ownership before freeing */
    task_t *current = task_get_current();
    if (current && fd_table[fd].owner_tid != 0 && fd_table[fd].owner_tid != current->tid) {
        FD_CRITICAL_END;
        return;  /* Access denied - can't close another task's fd */
    }

    fd_table[fd].in_use = 0;
    fd_table[fd].file = NULL;
    fd_table[fd].owner_tid = 0;
    FD_CRITICAL_END;
}

/* Cleanup all file descriptors owned by a task (called on task exit) */
void fd_cleanup_task(uint32_t tid) {
    __asm__ volatile ("cli");
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (fd_table[i].in_use && fd_table[i].owner_tid == tid) {
            if (fd_table[i].file) {
                fat32_close(fd_table[i].file);
            }
            fd_table[i].in_use = 0;
            fd_table[i].file = NULL;
            fd_table[i].owner_tid = 0;
        }
    }
    __asm__ volatile ("sti");
}

static uint32_t handle_console(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;

    task_t *cur = task_get_current();
    if (cur && cur->vconsole) {
        vconsole_t *vc = (vconsole_t *)cur->vconsole;
        switch (al) {
            case 0x00:
                if (!ebx) return (uint32_t)-1;
                vc_write(vc, (const char*)ebx);
                return 0;
            case 0x01:
                if (!ebx) return (uint32_t)-1;
                return (uint32_t)vc_getline(vc, (char*)ebx, ecx);
            case 0x02:
                vc_set_color(vc, (uint8_t)ebx, (uint8_t)ecx);
                return 0;
            case 0x03:
                return (uint32_t)vc_getchar(vc);
            case 0x04:
                vc_clear(vc);
                return 0;
            case 0x05:
                vc_set_cursor(vc, (int)ebx, (int)ecx);
                return 0;
            case 0x06:
                if (!ebx || !ecx) return (uint32_t)-1;
                vc_get_cursor(vc, (int*)ebx, (int*)ecx);
                return 0;
            case 0x07:
                if (!ebx || !ecx) return (uint32_t)-1;
                memcpy_s(vc->chars, (const void*)ebx, VC_COLS * VC_ROWS);
                memcpy_s(vc->attrs, (const void*)ecx, VC_COLS * VC_ROWS);
                vc->dirty = 1;
                return 0;
            default:
                return (uint32_t)-1;
        }
    }
    
    switch (al) {
        case 0x00:
            if (!ebx) return -1;
            {
                const char *s = (const char*)ebx;
                while (*s) putchar(*s++);
            }
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

        case 0x05: /* Set cursor */
            if (!ebx) return -1;
            vga_set_cursor((int)ebx, (int)ecx);
            return 0;
        
        case 0x06: /* Get cursor position */
            if (!ebx || !ecx) return -1;
            vga_get_cursor((int*)ebx, (int*)ecx);
            return 0;

        case 0x07: { /* Blit screen buffer */
            if (!ebx || !ecx) return -1;
            volatile uint16_t *vmem = (volatile uint16_t*)0xB8000;
            const uint8_t *ch = (const uint8_t*)ebx;
            const uint8_t *at = (const uint8_t*)ecx;
            for (int i = 0; i < 80 * 25; i++)
                vmem[i] = ((uint16_t)at[i] << 8) | ch[i];
            return 0;
        }

        default:
            return -1;
    }
}

static uint32_t handle_process(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
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
        
        case 0x05:
            if (!ebx) return -1;
            return task_spawn_and_wait((const char*)ebx, (const char*)ecx);

        case 0x06:  /* SYS_PROC_SPAWN_ASYNC - spawn without waiting */
            if (!ebx) return -1;
            return task_spawn((const char*)ebx, (const char*)ecx);

        case 0x07: { /* SYS_PROC_SET_ICON */
            if (!ebx || !ecx) return (uint32_t)-1;
            task_t *t = task_find_by_tid((uint32_t)ebx);
            if (!t) return (uint32_t)-1;
            strcpy_s(t->icon_path, (const char*)ecx, 64);
            return 0;
        }

        case 0x08: { /* SYS_PROC_KILL */
            if (!ebx) return (uint32_t)-1;
            uint32_t tid = (uint32_t)ebx;
            task_t *t = task_find_by_tid(tid);
            if (!t) return (uint32_t)-1;
            /* Do not allow killing the kernel task (tid 0) */
            if (t->tid == 0) return (uint32_t)-1;
            /* Do not allow killing the caller via kill (caller should use sys_exit) */
            task_t *current = task_get_current();
            if (current && current->tid == tid) return (uint32_t)-1;

            /* If parent is blocked waiting for this child, wake it up */
            if (t->parent_tid) {
                task_t *parent = task_find_by_tid(t->parent_tid);
                if (parent && parent->state == TASK_BLOCKED && parent->child_tid == t->tid) {
                    parent->state = TASK_READY;
                    parent->child_tid = 0;
                }
            }

            t->state = TASK_TERMINATED;
            return 0;
        }

        case 0x09: { /* SYS_PROC_GETARGS */
            if (!ebx) return (uint32_t)NULL;
            task_t *current = task_get_current();
            if (!current) return (uint32_t)NULL;
            
            char *buf = (char*)ebx;
            int len = (int)ecx;
            
            if (len > 0) {
                strcpy_s(buf, current->args, len);
                return (uint32_t)buf;
            }
            return (uint32_t)NULL;
        }

        default:
            return -1;
    }
}

static uint32_t handle_file(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: {
            if (!ebx || !ecx) return (uint32_t)-1;
            
            fat32_file_t *file = fat32_open((const char*)ebx, (const char*)ecx);
            if (!file) {
                return (uint32_t)-1;
            }
            
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
        
        case 0x06: {
            if (!ebx || !ecx) return (uint32_t)-1;
            int result = fat32_rename((const char*)ebx, (const char*)ecx);
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
            
        case 0x01: {
            if (!ebx) return -1;
            char *result = fat32_getcwd((char*)ebx, ecx);
            if (!result || ((char*)ebx)[0] == '\0') {
                if (ecx >= 4) {
                    memcpy_s((void*)ebx, "C:/", 4);
                    return (uint32_t)ebx;
                }
            }
            return (uint32_t)result;
        }
        
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
                    sys_entries[i].mtime = fat_entries[i].mtime;
                    sys_entries[i].mdate = fat_entries[i].mdate;
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

static __attribute__((noinline, noclone)) uint32_t handle_info(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: {
            if (!ebx) return -1;
            
            sys_meminfo_t *info = (sys_meminfo_t*)ebx;
            size_t total = pmm_total_frames();
            size_t free = pmm_count_free_frames();
            
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

        case 0x04:
            return (uint32_t)(uintptr_t)shell_version;

        case 0x05:
            shell_version = (const char*)ebx;
            return 0;

        case 0x06: { /* SYS_INFO_CPU */
            if (!ebx) return -1;
            sys_cpuinfo_t *out = (sys_cpuinfo_t*)ebx;

            /* Clear output */
            for (int i = 0; i < (int)sizeof(out->vendor); i++) out->vendor[i] = '\0';
            for (int i = 0; i < (int)sizeof(out->model); i++) out->model[i] = '\0';
            out->mhz = 0;

            /* --- Vendor string (CPUID leaf 0) --- */
            uint32_t a, b, c, d;
            char vbuf[13];
            __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
            ((uint32_t*)vbuf)[0] = b;
            ((uint32_t*)vbuf)[1] = d;
            ((uint32_t*)vbuf)[2] = c;
            vbuf[12] = '\0';
            for (int i = 0; i < 13; i++) out->vendor[i] = vbuf[i];

            /* --- Brand string (if supported) --- */
            __asm__ volatile("cpuid" : "=a"(a) : "a"(0x80000000) : "ebx","ecx","edx");
            if (a >= 0x80000004) {
                uint32_t regs[12];
                __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(0x80000002));
                __asm__ volatile("cpuid" : "=a"(regs[4]), "=b"(regs[5]), "=c"(regs[6]), "=d"(regs[7]) : "a"(0x80000003));
                __asm__ volatile("cpuid" : "=a"(regs[8]), "=b"(regs[9]), "=c"(regs[10]), "=d"(regs[11]) : "a"(0x80000004));

                /* copy 48 bytes into a temporary brand string */
                char brand[49];
                for (int ii = 0; ii < 12; ii++) {
                    uint32_t v = regs[ii];
                    brand[ii*4 + 0] = (char)(v & 0xFF);
                    brand[ii*4 + 1] = (char)((v >> 8) & 0xFF);
                    brand[ii*4 + 2] = (char)((v >> 16) & 0xFF);
                    brand[ii*4 + 3] = (char)((v >> 24) & 0xFF);
                }
                brand[48] = '\0';

                /* Try to extract an explicit frequency from the brand string (e.g. "300MHz" or "1.2GHz").
                   If present we'll trust that value (most reliable); otherwise fall back to TSC sampling. */
                int freq_idx = -1;
                uint32_t brand_mhz = 0;
                for (int p = 0; brand[p]; p++) {
                    if (brand[p] >= '0' && brand[p] <= '9') {
                        /* parse integer part */
                        uint32_t intval = 0;
                        int q = p;
                        while (brand[q] >= '0' && brand[q] <= '9') { intval = intval * 10 + (brand[q] - '0'); q++; }

                        /* optional fractional part */
                        uint32_t frac = 0; int frac_d = 0;
                        if (brand[q] == '.') {
                            q++;
                            while (brand[q] >= '0' && brand[q] <= '9' && frac_d < 3) { frac = frac * 10 + (brand[q] - '0'); frac_d++; q++; }
                        }

                        /* skip spaces */
                        while (brand[q] == ' ' || brand[q] == '\t') q++;

                        /* unit - look for 'M' (MHz) or 'G' (GHz) */
                        if (brand[q] == 'M' || brand[q] == 'm') {
                            brand_mhz = intval; /* ignore fraction for MHz */
                            freq_idx = p;
                            break;
                        } else if (brand[q] == 'G' || brand[q] == 'g') {
                            /* GHz -> convert to MHz, include up to 3 fractional digits */
                            uint32_t add = intval * 1000u;
                            if (frac_d > 0) {
                                /* scale fractional digits to MHz (e.g. .2GHz -> 200MHz) */
                                uint32_t scale = 1000u;
                                for (int s = 0; s < frac_d; s++) scale /= 10u;
                                add += frac * scale;
                            }
                            brand_mhz = add;
                            freq_idx = p;
                            break;
                        }
                        /* if not MHz/GHz, continue searching */
                    }
                }

                /* Build a sanitized model string: drop vendor prefix, remove (R)/(TM)/CPU and trim. */
                char vendor_norm[16];
                int vnlen = 0;
                /* derive a short vendor token */
                {
                    int is_genuineintel = 1;
                    const char *genuine = "GenuineIntel";
                    for (int _i = 0; genuine[_i]; _i++) { if (genuine[_i] != out->vendor[_i]) { is_genuineintel = 0; break; } }
                    if (is_genuineintel) {
                        const char *v = "Intel"; int k = 0; for (; v[k] && k < (int)sizeof(vendor_norm)-1; k++) vendor_norm[k] = v[k]; vendor_norm[k] = '\0';
                    } else {
                        int is_amd = 1; const char *amd = "AuthenticAMD";
                        for (int _i = 0; amd[_i]; _i++) { if (amd[_i] != out->vendor[_i]) { is_amd = 0; break; } }
                        if (is_amd) {
                            const char *v = "AMD"; int k = 0; for (; v[k] && k < (int)sizeof(vendor_norm)-1; k++) vendor_norm[k] = v[k]; vendor_norm[k] = '\0';
                        } else {
                            int j = 0; while (j < 15 && out->vendor[j]) { vendor_norm[j] = out->vendor[j]; j++; }
                            vendor_norm[j] = '\0';
                        }
                    }
                    while (vendor_norm[vnlen]) vnlen++;
                }

                char tmp[64]; int ti = 0;
                int idx = 0;
                /* skip vendor prefix if present (allow "Intel(R)" too) */
                int skip = 1;
                if (vnlen > 0) {
                    skip = 1;
                    for (int _k = 0; _k < vnlen; _k++) { if (brand[_k] != vendor_norm[_k]) { skip = 0; break; } }
                    if (skip) {
                        idx = vnlen;
                        /* skip optional (R) or (TM) immediately after vendor name */
                        while (brand[idx] == ' ' || brand[idx] == '(' || brand[idx] == '\t') {
                            if (brand[idx] == '(') {
                                while (brand[idx] && brand[idx] != ')') idx++;
                                if (brand[idx] == ')') idx++;
                                continue;
                            }
                            idx++;
                        }
                    } else {
                        idx = 0;
                    }
                }

                /* copy until frequency position or until 'CPU' token or string end */
                int stop_at = -1;
                if (freq_idx >= 0) stop_at = freq_idx;
                for (int p = idx; brand[p]; p++) {
                    /* detect 'CPU' token - stop and don't include it */
                    if ((brand[p] == 'C' || brand[p] == 'c') && (brand[p+1] == 'P' || brand[p+1] == 'p') && (brand[p+2] == 'U' || brand[p+2] == 'u')) {
                        stop_at = (stop_at < 0) ? p : (stop_at < p ? stop_at : p);
                        break;
                    }
                }
                if (stop_at < 0) stop_at = 48; /* whole brand */

                for (int p = idx; p < stop_at && ti < (int)sizeof(tmp)-1; p++) {
                    /* skip (R)/(TM) fragments inside */
                    if (brand[p] == '(') {
                        while (p < stop_at && brand[p] && brand[p] != ')') p++;
                        continue;
                    }
                    /* collapse multiple spaces */
                    if (brand[p] == ' ' || brand[p] == '\t') {
                        if (ti == 0 || tmp[ti-1] == ' ') continue;
                        tmp[ti++] = ' ';
                        continue;
                    }
                    tmp[ti++] = brand[p];
                }
                while (ti > 0 && tmp[ti-1] == ' ') ti--;
                tmp[ti] = '\0';

                if (ti > 0) {
                    int ci = 0; for (; ci < (int)sizeof(out->model)-1 && tmp[ci]; ci++) out->model[ci] = tmp[ci]; out->model[ci] = '\0';
                } else {
                    /* fallback to a short vendor/model if nothing parsed */
                    int ci = 0; for (; ci < (int)sizeof(out->model)-1 && out->vendor[ci]; ci++) out->model[ci] = out->vendor[ci]; out->model[ci] = '\0';
                }

                /* If brand string contained an explicit MHz/GHz token, use that (most reliable).
                   Otherwise we'll measure using TSC (averaged over several ticks). */
                if (brand_mhz > 0) {
                    out->mhz = brand_mhz;
                    return 0;
                }
            } else {
                /* No extended brand string — try SMBIOS/DMI Type-4 (BIOS-reported CPU name) first,
                   then fall back to CPUID leaf-1 family/model mapping. */

                /* Search BIOS memory for SMBIOS Entry Point "_SM_" (0xF0000 - 0x100000, step 16) */
                const char *smb_ent = NULL;
                for (uintptr_t addr = 0xF0000; addr < 0x100000; addr += 16) {
                    const char *p = (const char*)(uintptr_t)addr;
                    if (p[0] == '_' && p[1] == 'S' && p[2] == 'M' && p[3] == '_') { smb_ent = p; break; }
                }

                int smbios_used = 0;
                if (smb_ent) {
                    /* basic validation: entry point length at offset 0x05, table length at 0x16 */
                    uint8_t entry_len = (uint8_t)smb_ent[0x05];
                    if (entry_len >= 0x1F) {
                        uint16_t table_len = *(uint16_t*)(smb_ent + 0x16);
                        uint32_t table_addr = *(uint32_t*)(smb_ent + 0x18);
                        if (table_addr && table_len) {
                            const uint8_t *tbl = (const uint8_t*)(uintptr_t)table_addr;
                            size_t rem = table_len;
                            const uint8_t *cur = tbl;

                            while (rem >= 4) {
                                uint8_t type = cur[0];
                                uint8_t lenf = cur[1];
                                if (lenf < 4 || lenf > rem) break;

                                if (type == 4) { /* Processor Information */
                                    const uint8_t *strs = cur + lenf;
                                    const uint8_t *siter = strs;
                                    int found_model = 0;

                                    while ((uintptr_t)(siter - tbl) < table_len && *siter) {
                                        /* read string at siter */
                                        const char *st = (const char*)siter;
                                        /* check for keywords or an explicit MHz/GHz token */
                                        /* case-insensitive substring search for 'Celeron'/'Pentium' */
                                        for (int i = 0; st[i]; i++) {
                                            /* check Celeron */
                                            if ((st[i] == 'C' || st[i] == 'c') && (st[i+1] == 'e' || st[i+1] == 'E')) {
                                                /* crude check for "Celeron" substring */
                                                if ((st[i+0] == 'C' || st[i+0]=='c') && (st[i+1]=='e'||st[i+1]=='E') && (st[i+2]=='l'||st[i+2]=='L')) {
                                                    /* copy a cleaned model string */
                                                    int ci = 0; int si = i;
                                                    while (st[si] && ci < (int)sizeof(out->model)-1 && st[si] != '\r' && st[si] != '\n') out->model[ci++] = st[si++];
                                                    out->model[ci] = '\0';
                                                    /* try parse frequency from same string */
                                                    for (int k = 0; st[k]; k++) {
                                                        if (st[k] >= '0' && st[k] <= '9') {
                                                            /* parse number */
                                                            uint32_t intval = 0; int q = k; while (st[q] >= '0' && st[q] <= '9') { intval = intval*10 + (st[q]-'0'); q++; }
                                                            while (st[q] == ' ' || st[q] == '\t') q++;
                                                            if (st[q] == 'M' || st[q] == 'm') { out->mhz = intval; }
                                                            else if (st[q] == 'G' || st[q] == 'g') { out->mhz = intval * 1000u; }
                                                            break;
                                                        }
                                                    }
                                                    found_model = 1;
                                                    break;
                                                }
                                            }
                                            /* check Pentium token as fallback */
                                            if ((st[i] == 'P' || st[i]=='p') && (st[i+1]=='e' || st[i+1]=='E') && (st[i+2]=='n' || st[i+2]=='N')) {
                                                /* copy first words until digit or '(' */
                                                int ci = 0; int si = i;
                                                while (st[si] && ci < (int)sizeof(out->model)-1 && st[si] != '(' && !(st[si] >= '0' && st[si] <= '9')) {
                                                    out->model[ci++] = st[si++];
                                                }
                                                while (ci > 0 && out->model[ci-1] == ' ') ci--;
                                                out->model[ci] = '\0';
                                                found_model = 1;
                                                break;
                                            }
                                        }

                                        /* parse frequency if present in this string even without model token */
                                        for (int k = 0; st[k]; k++) {
                                            if (st[k] >= '0' && st[k] <= '9') {
                                                uint32_t intval = 0; int q = k; while (st[q] >= '0' && st[q] <= '9') { intval = intval*10 + (st[q]-'0'); q++; }
                                                while (st[q] == ' ' || st[q] == '\t') q++;
                                                if (st[q] == 'M' || st[q] == 'm') { if (out->mhz == 0) out->mhz = intval; }
                                                else if (st[q] == 'G' || st[q] == 'g') { if (out->mhz == 0) out->mhz = intval * 1000u; }
                                                break;
                                            }
                                        }

                                        /* advance to next string */
                                        while (*siter) siter++;
                                        siter++;
                                        if (found_model) break;
                                    }

                                    if (found_model) {
                                        smbios_used = 1;
                                        break;
                                    }
                                }

                                /* advance to next structure (find terminating double NUL after strings) */
                                const uint8_t *next = cur + lenf;
                                const uint8_t *s2 = next;
                                while ((uintptr_t)(s2 - tbl) < table_len) {
                                    if (s2[0] == 0 && s2[1] == 0) { s2 += 2; break; }
                                    s2++;
                                }
                                if ((uintptr_t)(s2 - tbl) >= table_len) break;
                                rem = table_len - (s2 - tbl);
                                cur = s2;
                            }
                        }
                    }
                }

                if (!smbios_used) {
                    /* No SMBIOS model found — fallback to CPUID leaf-1 mapping */
                    uint32_t ea, eb, ec, ed;
                    __asm__ volatile("cpuid" : "=a"(ea), "=b"(eb), "=c"(ec), "=d"(ed) : "a"(1));
                    uint32_t base_model = (ea >> 4) & 0xF;
                    uint32_t base_family = (ea >> 8) & 0xF;
                    uint32_t ext_model = (ea >> 16) & 0xF;
                    uint32_t ext_family = (ea >> 20) & 0xFF;
                    uint32_t display_model = base_model;
                    if (base_family == 6 || base_family == 15) display_model = base_model + (ext_model << 4);
                    uint32_t display_family = base_family;
                    if (base_family == 15) display_family = base_family + ext_family;

                    /* small family/model -> marketing-name map (include Celeron entries if known) */
                    struct fammap { uint8_t fam; uint8_t mod; const char *name; } fmap[] = {
                        {6, 3, "Pentium II"},
                        {6, 5, "Pentium II"},
                        {6, 6, "Pentium III"},
                        {6, 7, "Pentium III / Celeron"}, /* ambiguous - SMBIOS preferred */
                        {5, 0, "Pentium"},
                        {5, 4, "Pentium MMX"},
                    };
                    int found = 0;
                    for (int m = 0; m < (int)(sizeof(fmap)/sizeof(fmap[0])); m++) {
                        if (fmap[m].fam == display_family && fmap[m].mod == display_model) {
                            int ci = 0; for (; ci < (int)sizeof(out->model)-1 && fmap[m].name[ci]; ci++) out->model[ci] = fmap[m].name[ci]; out->model[ci] = '\0';
                            found = 1; break;
                        }
                    }
                    if (!found) {
                        int pos = 0;
                        const char *shortvend = out->vendor; int sv = 0;
                        while (shortvend[sv] && sv < 15 && pos < (int)sizeof(out->model)-2) out->model[pos++] = shortvend[sv++];
                        out->model[pos++] = ' ';
                        out->model[pos++] = 'F'; out->model[pos++] = 'a'; out->model[pos++] = 'm';
                        uint32_t v = display_family; char numbuf[12]; int npos = 0; if (v == 0) numbuf[npos++] = '0'; while (v > 0 && npos < (int)sizeof(numbuf)-1) { numbuf[npos++] = (char)('0' + (v % 10)); v /= 10; }
                        for (int k = npos-1; k >= 0 && pos < (int)sizeof(out->model)-1; k--) out->model[pos++] = numbuf[k];
                        out->model[pos++] = ' ';
                        out->model[pos++] = 'M'; out->model[pos++] = 'o'; out->model[pos++] = 'd';
                        v = display_model; npos = 0; if (v == 0) numbuf[npos++] = '0'; while (v > 0 && npos < (int)sizeof(numbuf)-1) { numbuf[npos++] = (char)('0' + (v % 10)); v /= 10; }
                        for (int k = npos-1; k >= 0 && pos < (int)sizeof(out->model)-1; k--) out->model[pos++] = numbuf[k];
                        out->model[pos] = '\0';
                    }
                }
            }

            /* --- Determine clock speed --- */
            if (out->mhz == 0) {
                /* 1) Try CPUID leaf 0x16 (base frequency, common on newer Intel CPUs) */
                uint32_t a16=0, b16=0, c16=0, d16=0;
                __asm__ volatile("cpuid" : "=a"(a16), "=b"(b16), "=c"(c16), "=d"(d16) : "a"(0x16));
                if (a16 && a16 < 0x80000) {
                    /* a16 is base frequency in MHz */
                    out->mhz = a16;
                    return 0;
                }

                /* NOTE: CPUID.0x15 handling omitted (avoids 64-bit div helpers). */

                /* 3) Try to parse explicit frequency from the brand string was handled earlier.
                   4) Fallback — measure TSC over multiple PIT ticks (serialized). */
                const uint32_t sample_ticks = 20; /* ~200ms at default 100Hz PIT — reduces error */

                unsigned int lo1, hi1, lo2, hi2;
                uint64_t t1, t2, cycles64;

                /* serialize, read TSC */
                uint32_t ca, cb, cc, cd;
                __asm__ volatile("cpuid" : "=a"(ca), "=b"(cb), "=c"(cc), "=d"(cd) : "a"(0));
                __asm__ volatile ("rdtsc" : "=a"(lo1), "=d"(hi1));

                timer_wait(sample_ticks);

                __asm__ volatile("cpuid" : "=a"(ca), "=b"(cb), "=c"(cc), "=d"(cd) : "a"(0));
                __asm__ volatile ("rdtsc" : "=a"(lo2), "=d"(hi2));

                t1 = ((uint64_t)hi1 << 32) | lo1;
                t2 = ((uint64_t)hi2 << 32) | lo2;
                cycles64 = (t2 > t1) ? (t2 - t1) : 0ULL;

                uint32_t tf = timer_get_frequency();

                /* Avoid 64-bit division helpers by doing the math in 32-bit pieces.
                   cycles64 is expected to be small enough that casting to 32-bit is safe
                   for the chosen sample_ticks; clamp if necessary. */
                uint32_t cycles32 = (cycles64 > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)cycles64;
                uint32_t cycles_per_tick = cycles32 / sample_ticks; /* 32-bit division */

                /* mhz = (cycles_per_tick * tf) / 1_000_000
                   compute as (cycles_per_tick / 1e6)*tf + ((cycles_per_tick % 1e6) * tf)/1e6
                   to keep intermediate values 32-bit */
                uint32_t whole = cycles_per_tick / 1000000u;
                uint32_t rem = cycles_per_tick % 1000000u;
                uint32_t mhz = whole * tf + (uint32_t)((rem * tf) / 1000000u);
                if (mhz == 0 && cycles32 > 0) mhz = 1;
                out->mhz = mhz;
            }

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

        case 0x0C: { /* Load BMP with transparency control */
            if (!ebx) return (uint32_t)-1;
            int x = (int)(ecx >> 16);
            int y = (int)(ecx & 0xFFFF);
            return gfx_load_bmp_4bit_ex((const char*)ebx, x, y, (int)edx);
        }

        case 0x0D: { /* Cache BMP to memory */
            if (!ebx || !ecx) return (uint32_t)-1;
            cached_bmp_t *out = (cached_bmp_t*)ecx;
            int w, h;
            uint8_t *data = gfx_load_bmp_to_buffer((const char*)ebx, &w, &h);
            if (!data) return (uint32_t)-1;
            out->data = data;
            out->width = w;
            out->height = h;
            return 0;
        }

        case 0x0E: { /* Draw cached BMP */
            if (!ebx) return (uint32_t)-1;
            cached_bmp_t *bmp = (cached_bmp_t*)ebx;
            if (!bmp->data) return (uint32_t)-1;
            int x = (int)(ecx >> 16);
            int y = (int)(ecx & 0xFFFF);
            gfx_draw_cached_bmp_ex(bmp->data, bmp->width, bmp->height, x, y, (int)edx);
            return 0;
        }

        case 0x0F: { /* Free cached BMP */
            if (!ebx) return (uint32_t)-1;
            cached_bmp_t *bmp = (cached_bmp_t*)ebx;
            if (bmp->data) {
                kfree(bmp->data);
                bmp->data = 0;
                bmp->width = 0;
                bmp->height = 0;
            }
            return 0;
        }

        case 0x11: { /* SYS_GFX_LOAD_BMP_SCALED - Load BMP and draw scaled (no transparency) */
            if (!ebx) return (uint32_t)-1;
            int dest_x = (int)(ecx >> 16);
            int dest_y = (int)(ecx & 0xFFFF);
            int dest_w = (int)(edx >> 16);
            int dest_h = (int)(edx & 0xFFFF);

            int src_w, src_h;
            uint8_t *bmp = gfx_load_bmp_to_buffer((const char*)ebx, &src_w, &src_h);
            if (!bmp) return (uint32_t)-1;

            /* Always scale to exact target dimensions */
            if (src_w != dest_w || src_h != dest_h) {
                bitmap_t src;
                src.data = bmp;
                src.width = src_w;
                src.height = src_h;
                src.bits_per_pixel = 4;

                bitmap_t *scaled = bitmap_scale_nearest(&src, dest_w, dest_h);

                if (!scaled) {
                    /* Scaling failed - draw original as fallback */
                    gfx_draw_cached_bmp_ex(bmp, src_w, src_h, dest_x, dest_y, 0);
                    kfree(bmp);
                    return (uint32_t)-1;
                }
                kfree(bmp); /* free original raw buffer */
                gfx_draw_cached_bmp_ex(scaled->data, scaled->width, scaled->height, dest_x, dest_y, 0);
                bitmap_free(scaled);
            } else {
                /* Already exact size - draw directly */
                gfx_draw_cached_bmp_ex(bmp, src_w, src_h, dest_x, dest_y, 0);
                kfree(bmp);
            }
            return 0;
        }

        case 0x12: { /* SYS_GFX_CACHE_BMP_SCALED - Load, scale, and cache BMP */
            if (!ebx || !edx) return (uint32_t)-1;
            int target_w = (int)(ecx >> 16);
            int target_h = (int)(ecx & 0xFFFF);
            cached_bmp_t *out = (cached_bmp_t*)edx;

            int src_w, src_h;
            uint8_t *bmp = gfx_load_bmp_to_buffer((const char*)ebx, &src_w, &src_h);
            if (!bmp) return (uint32_t)-1;

            /* Scale if dimensions don't match */
            if (src_w != target_w || src_h != target_h) {
                bitmap_t src;
                src.data = bmp;
                src.width = src_w;
                src.height = src_h;
                src.bits_per_pixel = 4;

                bitmap_t *scaled = bitmap_scale_nearest(&src, target_w, target_h);
                kfree(bmp); /* free original */

                if (!scaled) return (uint32_t)-1;
                out->data = scaled->data;
                out->width = scaled->width;
                out->height = scaled->height;
                /* Free bitmap structure but not data (now owned by cache) */
                kfree(scaled);
            } else {
                /* Already correct size */
                out->data = bmp;
                out->width = src_w;
                out->height = src_h;
            }
            return 0;
        }

        case 0x10: { /* Draw partial region of cached BMP */
            if (!ebx) return (uint32_t)-1;

            /* Userland passes a pointer to this struct in EBX */
            typedef struct {
                void *bmp;      /* gfx_cached_bmp_t* */
                int dest_x;
                int dest_y;
                int src_x;
                int src_y;
                int src_w;
                int src_h;
                int transparent;
            } draw_region_t;

            draw_region_t *r = (draw_region_t*)ebx;
            if (!r->bmp) return (uint32_t)-1;

            cached_bmp_t *bmp = (cached_bmp_t*)r->bmp;
            if (!bmp->data) return (uint32_t)-1;

            int sx = r->src_x;
            int sy = r->src_y;
            int sw = r->src_w;
            int sh = r->src_h;

            /* Clip source to bitmap bounds */
            if (sx < 0) { sw += sx; sx = 0; }
            if (sy < 0) { sh += sy; sy = 0; }
            if (sx >= bmp->width || sy >= bmp->height) return 0;
            if (sx + sw > bmp->width) sw = bmp->width - sx;
            if (sy + sh > bmp->height) sh = bmp->height - sy;
            if (sw <= 0 || sh <= 0) return 0;

            gfx_draw_cached_bmp_region((uint8_t*)bmp->data, bmp->width, bmp->height,
                                       r->dest_x, r->dest_y,
                                       sx, sy, sw, sh, r->transparent);
            return 0;
        }

        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_mouse(uint32_t al, uint32_t ebx, 
                                uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: {
            if (!ebx || !ecx || !edx) return (uint32_t)-1;
            *(int*)ebx = mouse_get_x();
            *(int*)ecx = mouse_get_y();
            *(uint8_t*)edx = mouse_get_buttons();
            return 0;
        }
        case 0x01: {
            if (!edx && buffer_valid) {
                mouse_restore();
            }
            
            mouse_save(ebx, ecx);
            mouse_draw_cursor(ebx, ecx);
            return 0;
        }

        case 0x02: {
            mouse_invalidate_buffer();
            return 0;
        }
        
        default:
            return (uint32_t)-1;
    }
}

#define ICON_DIRTY_MARGIN 15

static void pump_set_icons_dirty_rect(window_manager_t *wm) {
    int min_x = WM_SCREEN_WIDTH, min_y = WM_SCREEN_HEIGHT, max_x = 0, max_y = 0;
    int found = 0;

    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f || !f->win.is_minimized) continue;
        
        if (f->win.minimized_icon_id != -1 && f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];
                if (ctrl->type == CTRL_ICON && ctrl->id == f->win.minimized_icon_id) {
                    int ix = ctrl->x - ICON_DIRTY_MARGIN;
                    int iy = ctrl->y - ICON_DIRTY_MARGIN;
                    int iw = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
                    int label_lines = icon_count_label_lines(ctrl->text, 49);
                    int ih = icon_calc_total_height(32, label_lines);
                    int ix2 = ctrl->x + iw + ICON_DIRTY_MARGIN;
                    int iy2 = ctrl->y + ih + ICON_DIRTY_MARGIN;
                    if (ix < min_x) min_x = ix;
                    if (iy < min_y) min_y = iy;
                    if (ix2 > max_x) max_x = ix2;
                    if (iy2 > max_y) max_y = iy2;
                    found = 1;
                    break;
                }
            }
        }
    }

    if (found) {
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        compositor_set_dirty_rect(wm, min_x, min_y, max_x - min_x, max_y - min_y);
    }
}

static void pump_deselect_all_icons(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f || !f->win.is_minimized) continue;
        
        if (f->win.minimized_icon_id != -1 && f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];
                if (ctrl->type == CTRL_ICON && ctrl->id == f->win.minimized_icon_id) {
                    ctrl->icon.checked = 0;
                    break;
                }
            }
        }
    }
    pump_set_icons_dirty_rect(wm);
}

static int pump_handle_icon_click(gui_form_t *form, int mx, int my) {
    uint32_t current_time = timer_get_ticks();

    if (win_is_icon_clicked(form, mx, my)) {
        if (wm_is_icon_doubleclick(&global_wm, current_time, form)) {
            /* Double-click - restore window */
            int icon_x = 0, icon_y = 0;
            if (form->win.minimized_icon_id != -1 && form->controls) {
                gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                if (ctrl) {
                    icon_x = ctrl->x;
                    icon_y = ctrl->y;
                }
            }
            if (icon_x || icon_y) {
                wm_release_icon_slot(&global_wm, icon_x, icon_y);
            }
            win_restore(form);
            wm_bring_to_front(&global_wm, form);
            if (global_wm.count > 0) global_wm.focused_index = global_wm.count - 1;
            global_wm.needs_full_redraw = 1;
            compositor_draw_all(&global_wm);
            return 2;
        } else {
            mouse_invalidate_buffer();
            pump_deselect_all_icons(&global_wm);
            
            if (form->win.minimized_icon_id != -1 && form->controls) {
                gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                if (ctrl) {
                    ctrl->icon.checked = 1;
                    ctrl->icon.click_start_x = mx;
                    ctrl->icon.click_start_y = my;
                    ctrl->icon.original_x = ctrl->x;
                    ctrl->icon.original_y = ctrl->y;
                }
            }
            wm_set_icon_click(&global_wm, current_time, form);
            return 1;  /* Selection changed, needs redraw */
        }
    }
    return 0;
}

static void init_window_menu(gui_form_t *form) {
    menu_init(&form->window_menu);
    if (form->win.resizable) {
        if (form->win.is_maximized) {
            menu_add_item(&form->window_menu, "Restore", MENU_ACTION_RESTORE, MENU_ITEM_ENABLED);
        } else {
            menu_add_item(&form->window_menu, "Maximise", MENU_ACTION_MAXIMIZE, MENU_ITEM_ENABLED);
        }
    }
    menu_add_item(&form->window_menu, "Minimise", MENU_ACTION_MINIMIZE, MENU_ITEM_ENABLED);
    menu_add_item(&form->window_menu, "Close", MENU_ACTION_CLOSE, MENU_ITEM_ENABLED);
    form->window_menu_initialized = 1;
}

static int pump_handle_minimize(gui_form_t *form, int mx, int my) {
    if (win_is_minimize_button(&form->win, mx, my)) {
        /* Initialize menu if needed */
        init_window_menu(form);

        /* Calculate menu position - below the minimize button */
        int menu_x = form->win.x + form->win.w - 80;
        int menu_y = form->win.y + WM_TITLEBAR_HEIGHT + 2;

        /* Show the window menu instead of minimizing */
        menu_show(&form->window_menu, menu_x, menu_y);
        return 2;  /* Menu shown, needs redraw but don't minimize */
    }
    return 0;
}

static int is_any_icon_selected(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f || !f->win.is_minimized) continue;
        
        if (f->win.minimized_icon_id != -1 && f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];
                if (ctrl->type == CTRL_ICON && ctrl->id == f->win.minimized_icon_id) {
                    if (ctrl->icon.checked) return 1;
                    break;
                }
            }
        }
    }
    return 0;
}

static int pump_handle_titlebar_click(gui_form_t *form, int mx, int my) {
    if (form->win.is_maximized) return 0;
    if (win_is_titlebar(&form->win, mx, my)) {
        if (is_any_icon_selected(&global_wm))
            pump_deselect_all_icons(&global_wm);
        mouse_restore();
        mouse_invalidate_buffer();
        form->dragging = 1;
        form->drag_start_x = mx;
        form->drag_start_y = my;
        form->press_control_id = -1;
        return 1;  /* Dragging started */
    }
    return 0;
}

static int pump_handle_resize_corner_click(gui_form_t *form, int mx, int my) {
    if (form->win.is_maximized) return 0;
    if (win_is_resize_corner(&form->win, mx, my)) {
        if (is_any_icon_selected(&global_wm))
            pump_deselect_all_icons(&global_wm);
        mouse_restore();
        mouse_invalidate_buffer();
        form->resizing = 1;
        form->resize_start_w = form->win.w;
        form->resize_start_h = form->win.h;
        form->resize_start_mx = mx;
        form->resize_start_my = my;
        form->press_control_id = -1;
        return 1;  /* Resizing started */
    }
    return 0;
}

static int pump_update_dropdown_hover(gui_form_t *form, int mx, int my, int ctrl_y_offset) {
    /* Update hover state for any open dropdown list */
    int needs_redraw = 0;

    if (!form->controls) return 0;

    for (int i = 0; i < form->ctrl_count; i++) {
        gui_control_t *ctrl = &form->controls[i];
        if (ctrl->type != CTRL_DROPDOWN || !ctrl->dropdown.dropdown_open) continue;

        /* If user is dragging the dropdown scrollbar, don't overwrite that state */
        if (ctrl->dropdown.pressed && ctrl->dropdown.hovered_item == -2) continue;

        int abs_x = form->win.x + ctrl->x;
        int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
        int item_h = 16;
        int list_h = ctrl->dropdown.item_count * item_h;
        int list_y = abs_y + ctrl->h;
        
        /* Auto-flip: if list extends past screen bottom, render above control */
        if (list_y + list_h > GFX_HEIGHT) {
            list_y = abs_y - list_h;
            if (list_y < 0) {
                list_y = 0;
                list_h = abs_y;
                if (list_h < item_h) list_h = item_h;
            }
        }

        /* Calculate visible area and scrollbar requirements */
        int visible_count = list_h / item_h;
        if (visible_count < 1) visible_count = 1;
        int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
        int need_scrollbar = ctrl->dropdown.item_count > visible_count;
        int sb_w = need_scrollbar ? 18 : 0;
        int content_w = ctrl->w - sb_w;
        
        /* Clamp dropdown_scroll */
        if (ctrl->dropdown.dropdown_scroll > (uint16_t)max_scroll) ctrl->dropdown.dropdown_scroll = max_scroll;

        int old_hover = ctrl->dropdown.hovered_item;

        /* Use clipped list bounds when checking hover (support inline scrollbar) */
        if (mx >= abs_x && mx < abs_x + ctrl->w && my >= list_y && my < list_y + list_h) {

            /* If mouse is over the scrollbar area, mark with special hovered value (-2) */
            if (need_scrollbar && mx >= abs_x + content_w && mx < abs_x + ctrl->w) {
                if (old_hover != -2) {
                    ctrl->dropdown.hovered_item = -2; /* scrollbar region */
                    needs_redraw = 1;
                }
            } else {
                /* Mouse over item region: translate to absolute item index using dropdown_scroll */
                int rel = (my - list_y) / item_h;
                int hovered = ctrl->dropdown.dropdown_scroll + rel;
                if (hovered < 0) hovered = 0;
                if (hovered >= ctrl->dropdown.item_count) hovered = ctrl->dropdown.item_count - 1;
                if (hovered != old_hover) {
                    ctrl->dropdown.hovered_item = hovered;
                    needs_redraw = 1;
                }
            }
        } else {
            /* Mouse not over dropdown list */
            if (old_hover != -1) {
                ctrl->dropdown.hovered_item = -1;
                needs_redraw = 1;
            }
        }
    }

    return needs_redraw;
}

static int pump_handle_control_press(gui_form_t *form, int mx, int my, int ctrl_y_offset) {
    form->press_control_id = -1;
    int old_focus = form->focused_control_id;
    int clicked_on_focusable = 0;

    if (form->controls) {
        /* FIRST: Check if click is on any open dropdown list (highest priority) */
        for (int i = 0; i < form->ctrl_count; i++) {
            gui_control_t *ctrl = &form->controls[i];
            if (ctrl->type != CTRL_DROPDOWN || !ctrl->dropdown.dropdown_open) continue;

            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
            int list_h = ctrl->dropdown.item_count * 16;
            int list_y = abs_y + ctrl->h;
            
            /* Auto-flip: if list extends past screen bottom, render above control */
            if (list_y + list_h > GFX_HEIGHT) {
                list_y = abs_y - list_h;
                if (list_y < 0) {
                    list_y = 0;
                    list_h = abs_y;
                    if (list_h < 16) list_h = 16;
                }
            }

            /* Check if click is in dropdown list area */
            if (mx >= abs_x && mx < abs_x + ctrl->w && my >= list_y && my < list_y + list_h) {
                int item_h = 16;
                int visible_count = list_h / item_h;
                if (visible_count < 1) visible_count = 1;
                int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
                int need_scrollbar = ctrl->dropdown.item_count > visible_count;
                int sb_w = need_scrollbar ? 18 : 0;
                int content_w = ctrl->w - sb_w;

                /* Click landed inside inline scrollbar area? */
                if (need_scrollbar && mx >= abs_x + content_w && mx < abs_x + ctrl->w) {
                    int arrow_size = sb_w;
                    int track_len = list_h - 2 * arrow_size;
                    int thumb_size = 20;
                    if (thumb_size > track_len) thumb_size = track_len;
                    int thumb_pos = 0;
                    if (max_scroll > 0 && track_len > thumb_size)
                        thumb_pos = ((track_len - thumb_size) * ctrl->dropdown.dropdown_scroll) / max_scroll;

                    int rel_y = my - list_y; /* position within the list box */

                    /* Up arrow clicked */
                    if (rel_y < arrow_size) {
                        if (ctrl->dropdown.dropdown_scroll > 0) {
                            ctrl->dropdown.dropdown_scroll--;
                        }
                        /* show pressed state for inline scrollbar */
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = -2;
                        form->press_control_id = ctrl->id;
                        global_wm.needs_full_redraw = 1;
                        return 1;
                    }

                    /* Down arrow clicked */
                    if (rel_y >= list_h - arrow_size) {
                        if (ctrl->dropdown.dropdown_scroll < max_scroll) {
                            ctrl->dropdown.dropdown_scroll++;
                            if (ctrl->dropdown.dropdown_scroll > max_scroll) ctrl->dropdown.dropdown_scroll = max_scroll;
                        }
                        /* show pressed state for inline scrollbar */
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = -2;
                        form->press_control_id = ctrl->id;
                        global_wm.needs_full_redraw = 1;
                        return 1;
                    }

                    /* Thumb clicked -> begin dragging */
                    int thumb_y = arrow_size + thumb_pos;
                    if (rel_y >= thumb_y && rel_y < thumb_y + thumb_size) {
                        /* Start thumb drag for inline scrollbar */
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = -2; /* indicate scrollbar interaction */
                        ctrl->dropdown.scroll_offset = rel_y - thumb_y; /* store drag offset */
                        form->press_control_id = ctrl->id;
                        return 1;
                    }

                    /* Click on track (page/jump) */
                    if (rel_y >= arrow_size && rel_y < list_h - arrow_size) {
                        int track_y = arrow_size;
                        int rel_track = rel_y - track_y - thumb_size / 2;
                        if (rel_track < 0) rel_track = 0;
                        if (rel_track > track_len - thumb_size) rel_track = track_len - thumb_size;
                        int new_scroll = 0;
                        if (track_len - thumb_size > 0)
                            new_scroll = (rel_track * max_scroll) / (track_len - thumb_size);
                        if (new_scroll < 0) new_scroll = 0;
                        if (new_scroll > max_scroll) new_scroll = max_scroll;
                        if (new_scroll != ctrl->dropdown.dropdown_scroll) {
                            ctrl->dropdown.dropdown_scroll = new_scroll;
                            global_wm.needs_full_redraw = 1;
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = -2;
                        ctrl->dropdown.scroll_offset = thumb_size / 2;
                        form->press_control_id = ctrl->id;
                        return 1;
                    }
                }

                /* Click is in item region: map using dropdown_scroll */
                int rel_item = (my - list_y) / item_h;
                int clicked_item = ctrl->dropdown.dropdown_scroll + rel_item;
                if (clicked_item >= 0 && clicked_item < ctrl->dropdown.item_count) {
                    ctrl->dropdown.cursor_pos = clicked_item;
                    /* Generate event immediately when item is selected */
                    form->clicked_id = ctrl->id;
                }
                ctrl_hide_dropdown_list(&form->win, ctrl);
                form->press_control_id = -1;  /* Clear so release doesn't fire duplicate event */
                /* Signal desktop to do full redraw (clears artifacts outside window) */
                global_wm.needs_full_redraw = 1;
                return 1;  /* needs_redraw */
            }
            /* Check if click is on dropdown control itself */
            else if (mx >= abs_x && mx < abs_x + ctrl->w &&
                     my >= abs_y && my < abs_y + ctrl->h) {
                ctrl_hide_dropdown_list(&form->win, ctrl);
                form->press_control_id = ctrl->id;
                /* Signal desktop to do full redraw (clears artifacts outside window) */
                global_wm.needs_full_redraw = 1;
                return 1;  /* needs_redraw */
            }
            /* Click outside open dropdown - close it and consume the click */
            else {
                ctrl_hide_dropdown_list(&form->win, ctrl);
                /* Signal desktop to do full redraw (clears artifacts outside window) */
                global_wm.needs_full_redraw = 1;
                /* Return immediately - don't propagate click to underlying controls */
                return 1;
            }
        }

        /* Iterate backwards to check top-most (last drawn) controls first */
        for (int i = form->ctrl_count - 1; i >= 0; i--) {
            gui_control_t *ctrl = &form->controls[i];

            /* Skip non-interactive controls (treat picturebox as interactive so clicks are detected) */
            if (ctrl->type != CTRL_BUTTON &&
                ctrl->type != CTRL_LABEL &&
                ctrl->type != CTRL_CHECKBOX &&
                ctrl->type != CTRL_RADIOBUTTON &&
                ctrl->type != CTRL_TEXTBOX &&
                ctrl->type != CTRL_ICON &&
                ctrl->type != CTRL_DROPDOWN &&
                ctrl->type != CTRL_PICTUREBOX &&
                ctrl->type != CTRL_SCROLLBAR) {
                continue;
            }

            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

            /* For checkbox and radio - hit area includes the control and label */
            int hit_w = ctrl->w;
            int hit_h = ctrl->h;
            int hit_y_offset = 0;

            /* For checkbox/radio, if w/h are just the box size, expand hit area to include text */
            if (ctrl->type == CTRL_CHECKBOX && ctrl->w == 13) {
                /* Estimate text width - in practice, user should set w to include text */
                hit_w = 100;  /* Generous hit area including label */
            } else if (ctrl->type == CTRL_RADIOBUTTON && ctrl->w == 12) {
                hit_w = 100;  /* Generous hit area including label */
            } else if (ctrl->type == CTRL_ICON) {
                /* Icon hit area: 48x58 (32px icon + 24px label + 2px spacing) */
                hit_w = ctrl->w > 0 ? ctrl->w : 48;
                hit_h = ctrl->h > 0 ? ctrl->h : 58;
            } else if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                /* Extend hit area to include dropdown list */
                int list_h = ctrl->dropdown.item_count * 16;
                int list_y = abs_y + ctrl->h;
                
                /* Auto-flip: if list extends past screen bottom, it renders above control */
                if (list_y + list_h > GFX_HEIGHT) {
                    /* Rendered above - adjust hit area to start above control */
                    hit_y_offset = -(list_h);
                    hit_h = ctrl->h + list_h;
                } else {
                    /* Rendered below - extend hit area downward */
                    hit_h = ctrl->h + list_h;
                }
            }
            
            int hit_test_y = abs_y + (hit_y_offset != 0 ? hit_y_offset : 0);
            
            if (mx >= abs_x && mx < abs_x + hit_w &&
                my >= hit_test_y && my < hit_test_y + hit_h) {
                form->press_control_id = ctrl->id;

                if (ctrl->type == CTRL_BUTTON) {
                    ctrl->button.pressed = 1;
                    return 1;  /* needs_redraw */
                }
                /* For label, just record the press */
                else if (ctrl->type == CTRL_LABEL) {
                    return 0;  /* Press recorded, no visual change */
                }
                /* For checkbox and radio, just record the press */
                else if (ctrl->type == CTRL_CHECKBOX || ctrl->type == CTRL_RADIOBUTTON) {
                    return 0;  /* Press recorded, no visual change yet */
                }
                /* For textbox, set keyboard focus and cursor position */
                else if (ctrl->type == CTRL_TEXTBOX) {
                    form->focused_control_id = ctrl->id;
                    clicked_on_focusable = 1;

                    /* Calculate cursor position from mouse click */
                    extern bmf_font_t font_n;
                    int size = ctrl->font_size > 0 ? ctrl->font_size : 12;
                    int text_area_x = abs_x + 4;
                    int rel_x = mx - text_area_x;

                    int new_pos = textbox_pos_from_x(&font_n, size, ctrl->text,
                                                     ctrl->textbox.scroll_offset, rel_x);
                    ctrl->textbox.cursor_pos = new_pos;

                    /* Start mouse selection */
                    ctrl->textbox.sel_start = new_pos;
                    ctrl->textbox.sel_end = new_pos;
                    form->textbox_selecting = 1;

                    return 1;  /* Always redraw to show cursor position */
                }
                /* For icon, just record the press (selection happens on release) */
                else if (ctrl->type == CTRL_ICON) {
                    return 0;  /* Press recorded, no visual change yet */
                }
                /* For dropdown - open it (closing/selection handled in priority check above) */
                else if (ctrl->type == CTRL_DROPDOWN) {
                    if (!ctrl->dropdown.dropdown_open) {
                        /* Count items in text */
                        int count = 1;
                        const char *p = ctrl->text;
                        while (*p) {
                            if (*p == '|') count++;
                            p++;
                        }
                        ctrl->dropdown.item_count = count;

                        /* Ensure dropdown_scroll keeps current selection visible when opened */
                        int item_h = 16;
                        int list_h = ctrl->dropdown.item_count * item_h;
                        int list_y = abs_y + ctrl->h;
                        if (list_y + list_h > GFX_HEIGHT) {
                            /* clipped; compute available height above control */
                            list_h = abs_y;
                            if (list_h < item_h) list_h = item_h;
                        }
                        int visible_count = list_h / item_h;
                        if (visible_count < 1) visible_count = 1;
                        int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
                        int sel = ctrl->dropdown.cursor_pos;
                        if (sel < 0) sel = 0;
                        if (sel >= ctrl->dropdown.item_count) sel = ctrl->dropdown.item_count - 1;
                        if (sel > visible_count - 1) {
                            int s = sel - (visible_count - 1);
                            if (s > max_scroll) s = max_scroll;
                            ctrl->dropdown.dropdown_scroll = s;
                        } else {
                            ctrl->dropdown.dropdown_scroll = 0;
                        }

                        ctrl->dropdown.dropdown_open = 1;
                        return 1;  /* needs_redraw */
                    }
                }
                /* For scrollbar - detect which part was clicked (arrow, thumb, or track) */
                else if (ctrl->type == CTRL_SCROLLBAR) {
                    int vertical = !ctrl->scrollbar.checked;
                    int arrow_size = vertical ? ctrl->w : ctrl->h;
                    int track_len = vertical ? (ctrl->h - 2 * arrow_size) : (ctrl->w - 2 * arrow_size);
                    int thumb_size = 20;
                    if (thumb_size > track_len) thumb_size = track_len;
                    int max_val = ctrl->scrollbar.max_length > 0 ? ctrl->scrollbar.max_length : 100;
                    
                    /* Calculate current thumb position */
                    int thumb_pos = 0;
                    if (max_val > 0 && track_len > thumb_size) {
                        thumb_pos = ((track_len - thumb_size) * ctrl->scrollbar.cursor_pos) / max_val;
                    }
                    
                    if (vertical) {
                        int up_y = abs_y;
                        int down_y = abs_y + ctrl->h - arrow_size;
                        int track_y = abs_y + arrow_size;
                        int thumb_y = track_y + thumb_pos;
                        
                        if (my >= up_y && my < up_y + arrow_size) {
                            /* Up arrow clicked */
                            ctrl->scrollbar.hovered_item = 0;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos > 0) ctrl->scrollbar.cursor_pos--;
                            return 1;
                        } else if (my >= down_y && my < down_y + arrow_size) {
                            /* Down arrow clicked */
                            ctrl->scrollbar.hovered_item = 2;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos < max_val) ctrl->scrollbar.cursor_pos++;
                            return 1;
                        } else if (my >= thumb_y && my < thumb_y + thumb_size) {
                            /* Thumb clicked - start dragging, store offset */
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = my - thumb_y; /* Store drag offset */
                            return 1;
                        } else if (my >= track_y && my < track_y + track_len) {
                            /* Track clicked (not on thumb) - jump to position */
                            int rel_y = my - track_y - thumb_size / 2; /* Center thumb on click */
                            if (rel_y < 0) rel_y = 0;
                            if (rel_y > track_len - thumb_size) rel_y = track_len - thumb_size;
                            ctrl->scrollbar.cursor_pos = (rel_y * max_val) / (track_len - thumb_size);
                            if (ctrl->scrollbar.cursor_pos > max_val) ctrl->scrollbar.cursor_pos = max_val;
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = thumb_size / 2; /* Store drag offset at center */
                            return 1;
                        }
                    } else {
                        /* Horizontal scrollbar */
                        int left_x = abs_x;
                        int right_x = abs_x + ctrl->w - arrow_size;
                        int track_x = abs_x + arrow_size;
                        int thumb_x = track_x + thumb_pos;
                        
                        if (mx >= left_x && mx < left_x + arrow_size) {
                            /* Left arrow clicked */
                            ctrl->scrollbar.hovered_item = 0;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos > 0) ctrl->scrollbar.cursor_pos--;
                            return 1;
                        } else if (mx >= right_x && mx < right_x + arrow_size) {
                            /* Right arrow clicked */
                            ctrl->scrollbar.hovered_item = 2;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos < max_val) ctrl->scrollbar.cursor_pos++;
                            return 1;
                        } else if (mx >= thumb_x && mx < thumb_x + thumb_size) {
                            /* Thumb clicked - start dragging */
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = mx - thumb_x; /* Store drag offset */
                            return 1;
                        } else if (mx >= track_x && mx < track_x + track_len) {
                            /* Track clicked (not on thumb) - jump to position */
                            int rel_x = mx - track_x - thumb_size / 2;
                            if (rel_x < 0) rel_x = 0;
                            if (rel_x > track_len - thumb_size) rel_x = track_len - thumb_size;
                            ctrl->scrollbar.cursor_pos = (rel_x * max_val) / (track_len - thumb_size);
                            if (ctrl->scrollbar.cursor_pos > max_val) ctrl->scrollbar.cursor_pos = max_val;
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = thumb_size / 2;
                            return 1;
                        }
                    }
                }
                break;
            }
        }
    }

    /* If clicked outside any focusable control, clear focus */
    if (!clicked_on_focusable && old_focus != -1) {
        form->focused_control_id = -1;
        return 1;  /* Focus changed, needs redraw */
    }

    /* If no control was pressed, deselect all icons in the form */
    if (form->press_control_id == -1 && form->controls) {
        int deselected = 0;
        for (int i = 0; i < form->ctrl_count; i++) {
            if (form->controls[i].type == CTRL_ICON && form->controls[i].icon.checked) {
                form->controls[i].icon.checked = 0;
                deselected = 1;
            }
        }
        if (deselected) {
            return 1;  /* Icons deselected, needs redraw */
        }
    }

    return 0;
}

/* Helper function to find control by ID */
static gui_control_t* find_control_by_id(gui_form_t *form, int16_t id) {
    if (!form || !form->controls || id < 0) return NULL;
    for (int i = 0; i < form->ctrl_count; i++) {
        if (form->controls[i].id == id) {
            return &form->controls[i];
        }
    }
    return NULL;
}

/* Helper: delete selected text in textbox, return 1 if deleted */
static int textbox_delete_selection(gui_control_t *ctrl) {
    if (ctrl->textbox.sel_start < 0 || ctrl->textbox.sel_start == ctrl->textbox.sel_end) return 0;

    int sel_min = ctrl->textbox.sel_start < ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
    int sel_max = ctrl->textbox.sel_start > ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;

    int text_len = 0;
    while (ctrl->text[text_len]) text_len++;

    /* Shift text left to remove selected portion */
    int del_count = sel_max - sel_min;
    for (int i = sel_min; i <= text_len - del_count; i++) {
        ctrl->text[i] = ctrl->text[i + del_count];
    }

    ctrl->textbox.cursor_pos = sel_min;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

/* Handle keyboard input for focused textbox */
static int pump_handle_keyboard(gui_form_t *form) {
    if (form->focused_control_id < 0) return 0;

    gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    int key = kbd_getchar_nonblock();
    if (key == 0) return 0;  /* No key pressed */

    int text_len = 0;
    while (ctrl->text[text_len]) text_len++;

    int max_len = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
    int needs_redraw = 0;
    int has_selection = (ctrl->textbox.sel_start >= 0 && ctrl->textbox.sel_start != ctrl->textbox.sel_end);

    /* Handle special keys */
    if (key == KEY_LEFT) {
        if (has_selection) {
            /* Move cursor to start of selection, clear selection */
            int sel_min = ctrl->textbox.sel_start < ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = sel_min;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            ctrl->textbox.cursor_pos--;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_RIGHT) {
        if (has_selection) {
            /* Move cursor to end of selection, clear selection */
            int sel_max = ctrl->textbox.sel_start > ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = sel_max;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos < text_len) {
            ctrl->textbox.cursor_pos++;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_HOME) {
        ctrl->textbox.cursor_pos = 0;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == KEY_END) {
        ctrl->textbox.cursor_pos = text_len;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == '\b') {  /* Backspace */
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            /* Shift text left from cursor position */
            for (int i = ctrl->textbox.cursor_pos - 1; i < text_len; i++) {
                ctrl->text[i] = ctrl->text[i + 1];
            }
            ctrl->textbox.cursor_pos--;
            needs_redraw = 1;
        }
    }
    else if (key == KEY_DELETE) {
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->textbox.cursor_pos < text_len) {
            /* Shift text left from position after cursor */
            for (int i = ctrl->textbox.cursor_pos; i < text_len; i++) {
                ctrl->text[i] = ctrl->text[i + 1];
            }
            needs_redraw = 1;
        }
    }
    else if (key == '\t') {
        /* Tab could be used to move focus to next control - for now skip */
    }
    else if (key == '\n' || key == '\r') {
        /* Enter - generate event for the textbox */
        form->clicked_id = ctrl->id;
        needs_redraw = 1;
    }
    else if (key == KEY_ESC) {
        /* Escape - clear selection and focus */
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        form->focused_control_id = -1;
        needs_redraw = 1;
    }
    else if (key >= 0x20 && key < 0x80) {
        /* Printable ASCII character */
        /* Delete selection first if any */
        if (has_selection) {
            textbox_delete_selection(ctrl);
            /* Recalculate text length after deletion */
            text_len = 0;
            while (ctrl->text[text_len]) text_len++;
        }

        if (text_len < max_len - 1) {
            /* Shift text right from cursor position */
            for (int i = text_len; i >= ctrl->textbox.cursor_pos; i--) {
                ctrl->text[i + 1] = ctrl->text[i];
            }
            ctrl->text[ctrl->textbox.cursor_pos] = (char)key;
            ctrl->textbox.cursor_pos++;
            needs_redraw = 1;
        }
    }
    /* Ignore other special keys (function keys, alt combinations, etc.) */

    return needs_redraw;
}

static uint32_t handle_window(uint32_t al, uint32_t ebx,
                               uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: { /* SYS_WIN_MSGBOX - Modal message box */
            const char *msg = (const char*)ebx;
            const char *btn = (const char*)ecx;
            const char *title = (const char*)edx;

            if (buffer_valid) {
                mouse_restore();
                mouse_invalidate_buffer();
                gfx_swap_buffers();
            }

            /* Create msgbox (allocate dynamically to support multiple concurrent msgboxes) */
            msgbox_t *box = (msgbox_t*)kmalloc(sizeof(msgbox_t));
            if (!box) return -1;

            win_msgbox_create(box, msg, btn, title);
            win_msgbox_draw(box);

            /* Get initial mouse position and draw cursor */
            int mx = mouse_get_x();
            int my = mouse_get_y();

            /* Initial cursor draw (save new background) */
            mouse_save(mx, my);
            mouse_draw_cursor(mx, my);
            gfx_swap_buffers();

            /* Modal event loop */
            int dragging = 0;
            int drag_start_x = 0, drag_start_y = 0;
            uint8_t last_mb = 0;

            while (1) {
                mx = mouse_get_x();
                my = mouse_get_y();
                uint8_t mb = mouse_get_buttons();

                /* Detect button click */
                uint8_t button_pressed = (mb & 1) && !(last_mb & 1);
                uint8_t button_released = !(mb & 1) && (last_mb & 1);

                /* Handle dragging */
                if (button_pressed) {
                    if (win_is_titlebar(&box->base, mx, my)) {
                        dragging = 1;
                        drag_start_x = mx;
                        drag_start_y = my;
                    }
                }

                if (button_released) {
                    /* Check if button was clicked */
                    if (!dragging) {
                        int clicked = win_msgbox_handle_click(box, mx, my);
                        if (clicked) {
                                     /* Button clicked - restore cursor and window background */
                                     mouse_invalidate_buffer();
                                     win_restore_background(&box->base);
                                     gfx_swap_buffers();
                                     kfree(box);
                                     return clicked;
                        }
                    }
                    dragging = 0;
                }

                /* Perform dragging */
                if (dragging && (mb & 1)) {
                    int dx = mx - drag_start_x;
                    int dy = my - drag_start_y;

                    if (dx != 0 || dy != 0) {
                        win_move(&box->base, dx, dy);
                        drag_start_x = mx;
                        drag_start_y = my;

                        /* Redraw window and cursor (save new position during drag) */
                        win_msgbox_draw(box);
                        mouse_save(mx, my);
                        mouse_draw_cursor(mx, my);
                        gfx_swap_buffers();
                        continue;
                    }
                }

                last_mb = mb;

                /* Smart cursor update: restore old, save new, draw new */
                if (buffer_valid) {
                    mouse_restore();
                }
                mouse_save(mx, my);
                mouse_draw_cursor(mx, my);
                gfx_swap_buffers();
            }

            return 0;
        }

        case 0x05: { /* SYS_WIN_CREATE_FORM */
            /* Initialize window manager on first use */
            if (!wm_initialized) {
                wm_init(&global_wm);
                wm_initialized = 1;
            }

            gui_form_t *form = (gui_form_t*)kmalloc(sizeof(gui_form_t));
            if (!form) return (uint32_t)NULL;

            int x = (int16_t)(ecx >> 16);
            int y = (int16_t)(ecx & 0xFFFF);
            int w = (int16_t)(edx >> 16);
            int h = (int16_t)(edx & 0xFFFF);

            win_create(&form->win, x, y, w, h, (const char*)ebx);
            form->controls = NULL;
            form->ctrl_count = 0;
            form->clicked_id = -1;
            form->last_mouse_buttons = 0;
            form->press_control_id = -1;
            form->dragging = 0;
            form->drag_start_x = 0;
            form->drag_start_y = 0;
            form->resizing = 0;
            form->resize_start_w = 0;
            form->resize_start_h = 0;
            form->resize_start_mx = 0;
            form->resize_start_my = 0;
            form->icon_path[0] = '\0';
            form->focused_control_id = -1;  /* No control focused initially */
            form->textbox_selecting = 0;
            form->last_icon_click_time = 0;
            form->last_icon_click_id = -1;
            form->window_menu_initialized = 0;
            /* Initialize menu structure to safe defaults */
            form->window_menu.visible = 0;
            form->window_menu.saved_bg = NULL;
            form->window_menu.item_count = 0;
            form->window_menu.hovered_item = -1;
            /* Initialize menubar */
            form->menubar_enabled = 0;

            /* Track owner task for cleanup on process exit */
            task_t *current = task_get_current();
            form->owner_tid = current ? current->tid : 0;

            /* If the owning task has a default icon, apply it to the form */
            if (current && current->icon_path[0]) {
                strcpy_s(form->icon_path, current->icon_path, 64);
            }

            /* Register window with window manager */
            if (!wm_register_window(&global_wm, form)) {
                /* Failed to register - cleanup */
                kfree(form);
                return (uint32_t)NULL;
            }

            /* Draw the newly-created window into the back buffer and
               request a desktop redraw so the compositor includes the
               new window immediately. Without this the window may be
               overwritten until another action (e.g. moving Start
               Manager) forces a redraw. */
            compositor_draw_single(&global_wm, form);
            global_wm.needs_full_redraw = 1;

            return (uint32_t)form;
        }
              
        case 0x07: { /* SYS_WIN_PUMP_EVENTS */
            gui_form_t *form = (gui_form_t*)ebx;

            if (!form) return 0;

            /* Calculate Y offset for controls (titlebar + menubar if present) */
            int ctrl_y_offset = 20;
            if (form->menubar_enabled) {
                ctrl_y_offset += menubar_get_height(&form->menubar);
            }

            /* Reset clicked_id at start of event processing */
            form->clicked_id = -1;

            /* Get current mouse state */
            int mx = mouse_get_x();
            int my = mouse_get_y();
            uint8_t mb = mouse_get_buttons();

            /* Determine which window is topmost at mouse position */
            gui_form_t *topmost = wm_get_window_at(&global_wm, mx, my);

            /* Detect button transitions */
            uint8_t button_pressed = (mb & 1) && !(form->last_mouse_buttons & 1);
            uint8_t button_released = !(mb & 1) && (form->last_mouse_buttons & 1);

            /* Update last button state for next call */
            form->last_mouse_buttons = mb;

            int event_count = 0;
            int needs_redraw = 0;  /* Track if visual state changed */
            int z_order_changed = 0;  /* Track if window Z-order changed */
            int16_t changed_controls[32];  /* Track which specific controls changed */
            int changed_count = 0;

            /* Handle window menu if visible */
            if (form->window_menu.visible) {
                int action = menu_handle_mouse(&form->window_menu, mx, my,
                                               button_pressed, button_released);
                if (action > 0) {
                    /* Menu action selected */
                        if (action == MENU_ACTION_MAXIMIZE) {
                        form->dragging = 0;
                        form->resizing = 0;
                        win_maximize(form);
                        global_wm.needs_full_redraw = 1;
                        return -4;  /* Window resized, needs layout update */
                    } else if (action == MENU_ACTION_RESTORE) {
                        form->dragging = 0;
                        form->resizing = 0;
                        win_restore_from_maximize(form);
                        global_wm.needs_full_redraw = 1;
                        return -4;  /* Window resized, needs layout update */
                    } else if (action == MENU_ACTION_MINIMIZE) {
                        int icon_x, icon_y;
                        wm_get_next_icon_pos(&global_wm, &icon_x, &icon_y);
                        const char *icon_path = form->icon_path[0] ? form->icon_path : NULL;
                        win_minimize(form, icon_x, icon_y, icon_path);
                        /* Claim the icon slot */
                        wm_claim_icon_slot(&global_wm, icon_x, icon_y);
                        /* Let the desktop redraw run first to avoid compositing on stale wallpaper. */
                        global_wm.needs_full_redraw = 1;
                        return -2;  /* Window minimized (major state change) */
                    } else if (action == MENU_ACTION_CLOSE) {
                        /* Signal close request - return special value */
                        return -3;  /* Close requested */
                    }
                } else if (action == -1) {
                    /* Menu closed without selection */
                    needs_redraw = 1;
                } else if (button_pressed && !menu_contains_point(&form->window_menu, mx, my)) {
                    /* Click outside menu - close it */
                    menu_hide(&form->window_menu);
                    needs_redraw = 1;
                }
                /* Don't process other events while menu is visible */
                if (form->window_menu.visible || action != 0) {
                    if (needs_redraw) {
                        return (uint32_t)-1;
                    }
                    return 0;
                }
            }

            /* Handle menubar if enabled */
            if (form->menubar_enabled && form->menubar.visible) {
                int action = menubar_handle_mouse(&form->menubar, form->win.x, form->win.y,
                                                  mx, my, button_pressed, button_released);
                if (action > 0) {
                    /* Menu item selected - return action ID as event */
                    menubar_close_all(&form->menubar);
                    form->clicked_id = action;
                    event_count = 1;
                    needs_redraw = 1;
                } else if (action == -1) {
                    /* Menu closed without selection */
                    needs_redraw = 1;
                }
                /* Redraw if needed */
                if (needs_redraw) {
                    mouse_restore();
                    mouse_invalidate_buffer();
                    compositor_draw_single(&global_wm, form);
                    if (action > 0) {
                        return form->clicked_id;
                    }
                    return 0;
                }
            }

            /* Handle mouse button press */
            if (button_pressed) {
                /* If click landed on desktop (no window/icon), deselect any selected icons */
                if (topmost == NULL) {
                    pump_deselect_all_icons(&global_wm);
                    needs_redraw = 1;
                } else {
                    /* Only process window clicks if topmost is not NULL */

                    /* Ignore presses for windows that are not the topmost at the mouse position. This
                       prevents clicks on controls/titlebars of windows that are overlapped by another
                       (focused) window. Exception: allow clicks on open dropdown lists even if they
                       extend outside the window bounds. */
                    int click_on_dropdown = 0;
                    if (topmost != form && form->controls) {
                        /* Check if click is on an open dropdown list */
                        for (int i = 0; i < form->ctrl_count; i++) {
                            gui_control_t *ctrl = &form->controls[i];
                            if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                                int abs_x = form->win.x + ctrl->x;
                                int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
                                int list_h = ctrl->dropdown.item_count * 16;
                                int list_y = abs_y + ctrl->h;
                                
                                /* Account for auto-flip */
                                if (list_y + list_h > GFX_HEIGHT) {
                                    list_y = abs_y - list_h;
                                    if (list_y < 0) {
                                        list_y = 0;
                                        list_h = abs_y;
                                        if (list_h < 16) list_h = 16;
                                    }
                                }
                                
                                /* Check if click is within dropdown list area */
                                if (mx >= abs_x && mx < abs_x + ctrl->w &&
                                    my >= list_y && my < list_y + list_h) {
                                    click_on_dropdown = 1;
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (topmost != form && !click_on_dropdown) {
                        /* If click landed on another window, do not process this press for this form. */
                    } else if (form->win.is_minimized) {
                    /* Check for click on icon (single or double) */
                    int icon_result = pump_handle_icon_click(form, mx, my);
                    if (icon_result == 2) {
                        /* Double-click - window restored */
                        return -2;  /* Major state change - window restored */
                    } else if (icon_result == 1) {
                        /* Single click - selection changed */
                        needs_redraw = 1;
                    } else if (icon_result == 0) {
                        /* Click outside this icon - check if we need to deselect */
                        int icon_selected = 0;
                        if (form->win.minimized_icon_id != -1 && form->controls) {
                            gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                            if (ctrl && ctrl->icon.checked) icon_selected = 1;
                        }
                        if (icon_selected) {
                            /* Check if click was on ANY other icon */
                            int clicked_other_icon = 0;
                            for (int i = 0; i < global_wm.count; i++) {
                                if (global_wm.windows[i] &&
                                    global_wm.windows[i] != form &&
                                    global_wm.windows[i]->win.is_minimized &&
                                    win_is_icon_clicked(global_wm.windows[i], mx, my)) {
                                    clicked_other_icon = 1;
                                    break;
                                }
                            }
                            /* If no icon was clicked, deselect all */
                            if (!clicked_other_icon) {
                                pump_deselect_all_icons(&global_wm);
                                needs_redraw = 1;
                            }
                        }
                    }
                } else {
                    /* Click landed on topmost window - process normally */
                    /* Check if click is within window bounds (including open dropdown lists) */
                    int check_x = form->win.x;
                    int check_y = form->win.y;
                    int check_w = form->win.w;
                    int check_h = form->win.h;
                    
                    /* Expand bounds to include any open dropdown list */
                    if (form->controls) {
                        for (int i = 0; i < form->ctrl_count; i++) {
                            gui_control_t *ctrl = &form->controls[i];
                            if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                                int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
                                int list_h = ctrl->dropdown.item_count * 16;
                                int list_y = abs_y + ctrl->h;
                                
                                /* Account for auto-flip */
                                if (list_y + list_h > GFX_HEIGHT) {
                                    list_y = abs_y - list_h;
                                    if (list_y < 0) {
                                        list_y = 0;
                                        list_h = abs_y;
                                        if (list_h < 16) list_h = 16;
                                    }
                                }
                                
                                /* Expand bounds to include dropdown list */
                                if (list_y < check_y) {
                                    check_h += (check_y - list_y);
                                    check_y = list_y;
                                }
                                int list_bottom = list_y + list_h;
                                int window_bottom = form->win.y + form->win.h;
                                if (list_bottom > window_bottom) {
                                    int extra_h = list_bottom - window_bottom;
                                    if (check_h < form->win.h + extra_h) {
                                        check_h = form->win.h + extra_h;
                                    }
                                }
                            }
                        }
                    }
                    
                    if (mx >= check_x && mx < check_x + check_w &&
                        my >= check_y && my < check_y + check_h) {
                        z_order_changed = wm_bring_to_front(&global_wm, form);
                        if (z_order_changed)
                            global_wm.needs_full_redraw = 1;
                    }

                    /* Check if click is on an open dropdown list FIRST (highest priority, 
                       as dropdowns can extend above the window and overlap the titlebar) */
                    int dropdown_handled = 0;
                    if (form->controls) {
                        for (int i = 0; i < form->ctrl_count; i++) {
                            gui_control_t *ctrl = &form->controls[i];
                            if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                                int abs_x = form->win.x + ctrl->x;
                                int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
                                int list_h = ctrl->dropdown.item_count * 16;
                                int list_y = abs_y + ctrl->h;
                                
                                /* Account for auto-flip */
                                if (list_y + list_h > GFX_HEIGHT) {
                                    list_y = abs_y - list_h;
                                    if (list_y < 0) {
                                        list_y = 0;
                                        list_h = abs_y;
                                        if (list_h < 16) list_h = 16;
                                    }
                                }
                                
                                /* Check if click is within dropdown list area */
                                if (mx >= abs_x && mx < abs_x + ctrl->w &&
                                    my >= list_y && my < list_y + list_h) {
                                    /* Let pump_handle_control_press handle the dropdown click */
                                    dropdown_handled = 1;
                                    if (pump_handle_control_press(form, mx, my, ctrl_y_offset)) {
                                        needs_redraw = 1;
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    
                    if (!dropdown_handled) {
                        /* Check minimize button */
                        int min_result = pump_handle_minimize(form, mx, my);
                        if (min_result == 2) {
                            /* Menu shown */
                            needs_redraw = 1;
                        } else if (min_result == 1) {
                            return -1;  /* Window minimized directly */
                        }
                        /* Check if resize corner was clicked */
                        else if (pump_handle_resize_corner_click(form, mx, my)) {
                            /* Resizing started, continue */
                        }
                        /* Check if titlebar was clicked */
                        else if (pump_handle_titlebar_click(form, mx, my)) {
                            /* Dragging started, continue */
                        } else {
                            /* Find which control (if any) was pressed */
                            if (pump_handle_control_press(form, mx, my, ctrl_y_offset)) {
                                needs_redraw = 1;
                            }
                        }
                    }
                }
                }  /* Close the else block for topmost != NULL */
            }

            /* Check if an event was generated during button press (e.g., dropdown item selected) */
            if (form->clicked_id >= 0 && !button_released) {
                event_count = 1;
            }

            /* Handle mouse button release */
            if (button_released) {
                /* End window dragging - signal full redraw needed */
                int was_dragging = form->dragging;
                int was_resizing = form->resizing;
                if (form->dragging) {
                    form->dragging = 0;
                }
                if (form->resizing) {
                    form->resizing = 0;
                }

                /* End icon dragging - snap to slot */
                int was_icon_dragging = 0;
                if (form->win.is_minimized) {
                    if (form->win.minimized_icon_id != -1 && form->controls) {
                        gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                        if (ctrl && ctrl->icon.dragging) {
                            was_icon_dragging = 1;
                            /* Restore old position's background */
                            if (ctrl->icon.saved_bg) {
                                int old_bg_x = ctrl->x - 1;
                                int old_bg_y = ctrl->y - 1;
                                int old_bg_w = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
                                int label_lines = icon_count_label_lines(ctrl->text, 49);
                                int old_bg_h = icon_calc_total_height(32, label_lines);
                                if (old_bg_x >= 0 && old_bg_y >= 0 && 
                                    old_bg_x + old_bg_w + 2 <= WM_SCREEN_WIDTH && 
                                    old_bg_y + old_bg_h + 2 <= WM_SCREEN_HEIGHT && (old_bg_x & 1) == 0) {
                                    gfx_write_screen_region_packed(ctrl->icon.saved_bg, old_bg_w + 2, old_bg_h + 2, old_bg_x, old_bg_y);
                                }
                                kfree(ctrl->icon.saved_bg);
                                ctrl->icon.saved_bg = NULL;
                            }
                            /* Snap to nearest slot (slot already claimed during drag) */
                            int snap_x, snap_y;
                            wm_snap_to_slot(ctrl->x, ctrl->y, &snap_x, &snap_y);
                            /* If actually snapped to different position, update slot */
                            if (snap_x != ctrl->x || snap_y != ctrl->y) {
                                wm_release_icon_slot(&global_wm, ctrl->icon.original_x, ctrl->icon.original_y);
                                wm_claim_icon_slot(&global_wm, snap_x, snap_y);
                                ctrl->icon.original_x = snap_x;
                                ctrl->icon.original_y = snap_y;
                            }
                            ctrl->icon.dragging = 0;
                            ctrl_set_pos(form, ctrl->id, snap_x, snap_y);
                            /* Save snapped position's background */
                            int snap_bg_x = snap_x - 1;
                            int snap_bg_y = snap_y - 1;
                            int snap_bg_w = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
                            int snap_label_lines = icon_count_label_lines(ctrl->text, 49);
                            int snap_bg_h = icon_calc_total_height(32, snap_label_lines);
                            int snap_row_bytes = (snap_bg_w + 3) / 2;
                            ctrl->icon.saved_bg = (uint8_t*)kmalloc(snap_row_bytes * (snap_bg_h + 2));
                            if (ctrl->icon.saved_bg) {
                                if (snap_bg_x >= 0 && snap_bg_y >= 0 && 
                                    snap_bg_x + snap_bg_w + 2 <= WM_SCREEN_WIDTH && 
                                    snap_bg_y + snap_bg_h + 2 <= WM_SCREEN_HEIGHT && (snap_bg_x & 1) == 0) {
                                    gfx_read_screen_region_packed(ctrl->icon.saved_bg, snap_bg_w + 2, snap_bg_h + 2, snap_bg_x, snap_bg_y);
                                }
                            }
                        }
                    }
                }

                /* Check if release is on the same control that was pressed */
                if (form->press_control_id != -1 && form->controls) {
                    for (int i = 0; i < form->ctrl_count; i++) {
                        gui_control_t *ctrl = &form->controls[i];

                        if (ctrl->id == form->press_control_id) {
                            /* Clear pressed state for buttons */
                            if (ctrl->type == CTRL_BUTTON) {
                                ctrl->button.pressed = 0;
                                needs_redraw = 1;  /* Button visual changed */
                                if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                            }

                            /* Calculate absolute control position */
                            int abs_x = form->win.x + ctrl->x;
                            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

                            /* For checkbox and radio - hit area includes the control and label */
                            int hit_w = ctrl->w;
                            int hit_h = ctrl->h;

                            /* For checkbox/radio, if w/h are just the box size, expand hit area to include text */
                            if (ctrl->type == CTRL_CHECKBOX && ctrl->w == 13) {
                                hit_w = 100;  /* Generous hit area including label */
                            } else if (ctrl->type == CTRL_RADIOBUTTON && ctrl->w == 12) {
                                hit_w = 100;  /* Generous hit area including label */
                            } else if (ctrl->type == CTRL_ICON) {
                                /* Icon hit area: 48x58 (32px icon + 24px label + 2px spacing) */
                                hit_w = ctrl->w > 0 ? ctrl->w : 48;
                                hit_h = ctrl->h > 0 ? ctrl->h : 58;
                            } else if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                                /* Extended hit area when dropdown is open */
                                hit_h = ctrl->h + (ctrl->dropdown.item_count * 16);
                            }

                            /* Check if release is within same control */
                            if (mx >= abs_x && mx < abs_x + hit_w &&
                                my >= abs_y && my < abs_y + hit_h) {

                                /* Handle checkbox toggle */
                                if (ctrl->type == CTRL_CHECKBOX) {
                                    ctrl->checkbox.checked = !ctrl->checkbox.checked;
                                    needs_redraw = 1;
                                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                                    /* Don't set clicked_id for checkbox - just redraw */
                                }

                                /* Handle radio button selection */
                                else if (ctrl->type == CTRL_RADIOBUTTON) {
                                    /* Uncheck all radio buttons in the same group */
                                    for (int j = 0; j < form->ctrl_count; j++) {
                                        gui_control_t *other = &form->controls[j];
                                        if (other->type == CTRL_RADIOBUTTON &&
                                            other->checkbox.group_id == ctrl->checkbox.group_id) {
                                            other->icon.checked = 0;
                                            if (changed_count < 32) changed_controls[changed_count++] = other->id;
                                        }
                                    }
                                    /* Check this radio button */
                                    ctrl->checkbox.checked = 1;
                                    needs_redraw = 1;
                                    /* Don't set clicked_id for radio - just redraw */
                                }

                                /* Handle icon click/double-click */
                                else if (ctrl->type == CTRL_ICON) {
                                    uint32_t now = timer_get_ticks();

                                    /* Deselect all other icons first */
                                    for (int j = 0; j < form->ctrl_count; j++) {
                                        gui_control_t *other = &form->controls[j];
                                        if (other->type == CTRL_ICON && other->id != ctrl->id) {
                                            other->icon.checked = 0;
                                            if (changed_count < 32) changed_controls[changed_count++] = other->id;
                                        }
                                    }

                                    /* Check for double-click */
                                    if (form->last_icon_click_id == ctrl->id &&
                                        (now - form->last_icon_click_time) < WM_DOUBLECLICK_TICKS) {
                                        /* Double-click - activate the icon and deselect it */
                                        ctrl->icon.checked = 0;
                                        form->clicked_id = ctrl->id;
                                        event_count = 1;
                                        form->last_icon_click_id = -1;
                                    } else {
                                        /* Single click - just select */
                                        ctrl->icon.checked = 1;
                                        form->last_icon_click_time = now;
                                        form->last_icon_click_id = ctrl->id;
                                    }
                                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                                    needs_redraw = 1;
                                }

                                /* Handle dropdown - selection generates event */
                                else if (ctrl->type == CTRL_DROPDOWN) {
                                    /* Dropdown click was already handled in press, just generate event */
                                    /* Clear any temporary pressed state used for inline scrollbar dragging */
                                    ctrl->dropdown.pressed = 0;
                                    if (ctrl->dropdown.hovered_item == -2) ctrl->dropdown.hovered_item = -1;
                                    form->clicked_id = ctrl->id;
                                    event_count = 1;
                                    needs_redraw = 1;
                                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                                }

                                /* Handle scrollbar - clear pressed state and generate event */
                                else if (ctrl->type == CTRL_SCROLLBAR) {
                                    ctrl->scrollbar.pressed = 0;
                                    form->clicked_id = ctrl->id;
                                    event_count = 1;
                                    needs_redraw = 1;
                                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                                }

                                /* Valid click detected for buttons and other controls */
                                else {
                                    form->clicked_id = ctrl->id;
                                    event_count = 1;
                                }
                            }
                            break;
                        }
                    }
                }

                /* Clear all pressed states on any button that might still be pressed */
                if (form->controls) {
                    for (int i = 0; i < form->ctrl_count; i++) {
                        if (form->controls[i].type == CTRL_BUTTON && form->controls[i].button.pressed) {
                            form->controls[i].button.pressed = 0;
                            needs_redraw = 1;  /* Button visual changed */
                            if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                        }
                        if (form->controls[i].type == CTRL_SCROLLBAR && form->controls[i].scrollbar.pressed) {
                            form->controls[i].scrollbar.pressed = 0;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                        }
                        /* Clear dropdown inline scrollbar pressed state */
                        if (form->controls[i].type == CTRL_DROPDOWN && form->controls[i].dropdown.pressed) {
                            form->controls[i].dropdown.pressed = 0;
                            if (form->controls[i].dropdown.hovered_item == -2) form->controls[i].dropdown.hovered_item = -1;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                        }
                    }
                }

                /* Clear press state after release */
                form->press_control_id = -1;

                /* If we just finished dragging or resizing, signal full redraw needed */
                if (was_dragging || was_icon_dragging || was_resizing) {
                    global_wm.needs_full_redraw = 1;  /* Desktop will do full redraw */
                    if (was_resizing) {
                        return (uint32_t)-4;  /* -4 = resize ended */
                    }
                    return (uint32_t)-2;  /* -2 = drag ended */
                }
            }

            /* Handle icon dragging (start drag after threshold, update during drag) */
            if (form->win.is_minimized && (mb & 1)) {
                if (form->win.minimized_icon_id != -1 && form->controls) {
                    gui_control_t *icon_ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                    if (icon_ctrl && icon_ctrl->icon.checked) {
                        if (!icon_ctrl->icon.dragging) {
                            if (win_is_icon_clicked(form, icon_ctrl->icon.click_start_x, icon_ctrl->icon.click_start_y) ||
                                win_is_icon_clicked(form, mx, my)) {
                                int dx = mx - icon_ctrl->icon.click_start_x;
                                int dy = my - icon_ctrl->icon.click_start_y;
                                int threshold = WM_ICON_SLOT_WIDTH / 4;
                                if (dx * dx + dy * dy > threshold * threshold) {
                                    icon_ctrl->icon.dragging = 1;
                                    icon_ctrl->icon.drag_offset_x = mx - icon_ctrl->x;
                                    icon_ctrl->icon.drag_offset_y = my - icon_ctrl->y;
                                }
                            }
                        } else {
                            int new_x = mx - icon_ctrl->icon.drag_offset_x;
                            int new_y = my - icon_ctrl->icon.drag_offset_y;
                            if (new_x < 0) new_x = 0;
                            if (new_y < 0) new_y = 0;
                            if (new_x > WM_SCREEN_WIDTH - WM_ICON_TOTAL_WIDTH) new_x = WM_SCREEN_WIDTH - WM_ICON_TOTAL_WIDTH;
                            if (new_y > WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT - 48) new_y = WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT - 48;
                            if (new_x != icon_ctrl->x || new_y != icon_ctrl->y) {
                                int old_x = icon_ctrl->x;
                                int old_y = icon_ctrl->y;
                                int bg_w = icon_ctrl->w > 0 ? icon_ctrl->w : WM_ICON_TOTAL_WIDTH;
                                int label_lines = icon_count_label_lines(icon_ctrl->text, 49);
                                int bg_h = icon_calc_total_height(32, label_lines);
                                int row_bytes = (bg_w + 1) / 2;
                                uint8_t *old_saved_bg = icon_ctrl->icon.saved_bg;
                                /* Restore OLD position using existing saved_bg */
                                if (old_saved_bg) {
                                    int old_bg_x = old_x - 1;
                                    int old_bg_y = old_y - 1;
                                    for (int py = 0; py < bg_h + 2; py++) {
                                        uint8_t *row = old_saved_bg + py * row_bytes;
                                        for (int px = 0; px < bg_w + 2; px++) {
                                            int sx = old_bg_x + px;
                                            int sy = old_bg_y + py;
                                            if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                                                int byte_idx = px / 2;
                                                uint8_t packed = row[byte_idx];
                                                uint8_t pix = (px & 1) ? (packed & 0x0F) : (packed >> 4);
                                                gfx_putpixel(sx, sy, pix);
                                            }
                                        }
                                    }
                                }
                                /* Move icon to new position */
                                ctrl_set_pos(form, icon_ctrl->id, new_x, new_y);
                                /* Don't use compositor_draw_all - it would save bg WITH icon.
                                   Instead, save bg at new position and draw icon directly. */
                                uint8_t *new_saved_bg = (uint8_t*)kmalloc(row_bytes * (bg_h + 2));
                                if (new_saved_bg) {
                                    int new_bg_x = new_x - 1;
                                    int new_bg_y = new_y - 1;
                                    for (int py = 0; py < bg_h + 2; py++) {
                                        uint8_t *row = new_saved_bg + py * row_bytes;
                                        for (int b = 0; b < row_bytes; b++) row[b] = 0;
                                        for (int px = 0; px < bg_w + 2; px++) {
                                            int sx = new_bg_x + px;
                                            int sy = new_bg_y + py;
                                            uint8_t pix = 0;
                                            if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                                                pix = gfx_getpixel(sx, sy);
                                            }
                                            int byte_idx = px / 2;
                                            if (px & 1) row[byte_idx] = (row[byte_idx] & 0xF0) | (pix & 0x0F);
                                            else row[byte_idx] = (row[byte_idx] & 0x0F) | (pix << 4);
                                        }
                                    }
                                }
                                /* Free old saved_bg and use new one */
                                if (old_saved_bg) kfree(old_saved_bg);
                                icon_ctrl->icon.saved_bg = new_saved_bg;
                                /* Draw icon at new position directly */
                                ctrl_draw_icon(icon_ctrl, new_x, new_y, 0);
                                mouse_restore();
                                needs_redraw = 0;
                            }
                        }
                    }
                }
            }

            /* Handle active window dragging */
            if (form->dragging && (mb & 1)) {
                int dx = mx - form->drag_start_x;
                int dy = my - form->drag_start_y;

                if (dx != 0 || dy != 0) {
                    win_move(&form->win, dx, dy);
                    form->drag_start_x = mx;
                    form->drag_start_y = my;
                    mouse_restore();
                    compositor_draw_all(&global_wm);
                    needs_redraw = 0;  /* Already drawn */
                }
            }

            /* Handle active window resizing */
            if (form->resizing && (mb & 1)) {
                int dx = mx - form->resize_start_mx;
                int dy = my - form->resize_start_my;

                if (dx != 0 || dy != 0) {
                    int new_w = form->resize_start_w + dx;
                    int new_h = form->resize_start_h + dy;
                    win_resize(&form->win, new_w, new_h);
                    mouse_restore();
                    compositor_draw_all(&global_wm);
                    needs_redraw = 0;  /* Already drawn */
                }
            }

            /* Handle textbox mouse selection (dragging to select text) */
            if (form->textbox_selecting && (mb & 1) && form->focused_control_id >= 0) {
                gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
                if (ctrl && ctrl->type == CTRL_TEXTBOX) {
                    int abs_x = form->win.x + ctrl->x;
                    //int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

                    extern bmf_font_t font_n;
                    int size = ctrl->font_size > 0 ? ctrl->font_size : 12;
                    int text_area_x = abs_x + 4;
                    int rel_x = mx - text_area_x;

                    int new_pos = textbox_pos_from_x(&font_n, size, ctrl->text,
                                                     ctrl->textbox.scroll_offset, rel_x);

                    /* Update selection end and cursor */
                    if (new_pos != ctrl->textbox.sel_end) {
                        ctrl->textbox.sel_end = new_pos;
                        ctrl->textbox.cursor_pos = new_pos;
                        needs_redraw = 1;
                        if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                    }
                }
            }

            /* End textbox selection on mouse release */
            if (button_released && form->textbox_selecting) {
                form->textbox_selecting = 0;
                /* If sel_start == sel_end, clear selection */
                gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
                if (ctrl && ctrl->type == CTRL_TEXTBOX) {
                    if (ctrl->textbox.sel_start == ctrl->textbox.sel_end) {
                        ctrl->textbox.sel_start = -1;
                        ctrl->textbox.sel_end = -1;
                    }
                }
            }

            /* Handle scrollbar thumb dragging (including dropdown inline scrollbar) */
            if ((mb & 1) && form->press_control_id >= 0 && form->controls) {
                gui_control_t *ctrl = find_control_by_id(form, form->press_control_id);

                /* Regular scrollbar control dragging */
                if (ctrl && ctrl->type == CTRL_SCROLLBAR && ctrl->scrollbar.hovered_item == 1 && ctrl->scrollbar.pressed) {
                    int vertical = !ctrl->scrollbar.checked;
                    int arrow_size = vertical ? ctrl->w : ctrl->h;
                    int track_len = vertical ? (ctrl->h - 2 * arrow_size) : (ctrl->w - 2 * arrow_size);
                    int thumb_size = 20;
                    if (thumb_size > track_len) thumb_size = track_len;
                    int max_val = ctrl->scrollbar.max_length > 0 ? ctrl->scrollbar.max_length : 100;
                    
                    int abs_x = form->win.x + ctrl->x;
                    int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
                    
                    if (vertical) {
                        int track_y = abs_y + arrow_size;
                        int rel_y = my - track_y - ctrl->scrollbar.scroll_offset; /* Account for drag offset */
                        if (rel_y < 0) rel_y = 0;
                        if (rel_y > track_len - thumb_size) rel_y = track_len - thumb_size;
                        int new_val = (rel_y * max_val) / (track_len - thumb_size);
                        if (new_val > max_val) new_val = max_val;
                        if (new_val != ctrl->scrollbar.cursor_pos) {
                            ctrl->scrollbar.cursor_pos = new_val;
                            form->clicked_id = ctrl->id;
                            event_count = 1; /* Generate event during drag */
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }
                    } else {
                        int track_x = abs_x + arrow_size;
                        int rel_x = mx - track_x - ctrl->scrollbar.scroll_offset;
                        if (rel_x < 0) rel_x = 0;
                        if (rel_x > track_len - thumb_size) rel_x = track_len - thumb_size;
                        int new_val = (rel_x * max_val) / (track_len - thumb_size);
                        if (new_val > max_val) new_val = max_val;
                        if (new_val != ctrl->scrollbar.cursor_pos) {
                            ctrl->scrollbar.cursor_pos = new_val;
                            form->clicked_id = ctrl->id;
                            event_count = 1; /* Generate event during drag */
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }
                    }
                }

                /* Dropdown inline scrollbar dragging (ctrl->scrollbar.hovered_item == -2 used as special flag) */
                if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open && ctrl->dropdown.pressed && ctrl->dropdown.hovered_item == -2) {
                    int item_h = 16;
                    int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
                    int list_h = ctrl->dropdown.item_count * item_h;
                    int list_y = abs_y + ctrl->h;

                    /* Auto-flip and clipping like draw routine */
                    if (list_y + list_h > GFX_HEIGHT) {
                        list_y = abs_y - list_h;
                        if (list_y < 0) {
                            list_y = 0;
                            list_h = abs_y;
                            if (list_h < item_h) list_h = item_h;
                        }
                    }

                    int visible_count = list_h / item_h;
                    if (visible_count < 1) visible_count = 1;
                    int max_val = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
                    if (max_val > 0) {
                        int sb_w = 18;
                        int arrow_size = sb_w;
                        int track_len = list_h - 2 * arrow_size;
                        int thumb_size = 20;
                        if (thumb_size > track_len) thumb_size = track_len;
                        int track_y = list_y + arrow_size;

                        int rel_y = my - track_y - ctrl->dropdown.scroll_offset; /* scroll_offset used as drag offset */
                        if (rel_y < 0) rel_y = 0;
                        if (rel_y > track_len - thumb_size) rel_y = track_len - thumb_size;
                        int new_val = 0;
                        if (track_len - thumb_size > 0) new_val = (rel_y * max_val) / (track_len - thumb_size);
                        if (new_val > max_val) new_val = max_val;
                        if (new_val != ctrl->dropdown.dropdown_scroll) {
                            ctrl->dropdown.dropdown_scroll = new_val;
                            form->clicked_id = ctrl->id;
                            event_count = 1; /* Generate event during drag */
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }
                    }
                }
            }

            /* Update dropdown hover state (on any mouse move) */
            if (pump_update_dropdown_hover(form, mx, my, ctrl_y_offset)) {
                needs_redraw = 1;
                /* Find the dropdown control that's open and mark it changed */
                for (int i = 0; i < form->ctrl_count; i++) {
                    if (form->controls[i].type == CTRL_DROPDOWN && form->controls[i].dropdown.dropdown_open) {
                        if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                        break;
                    }
                }
            }

            /* Handle keyboard input for focused textbox. Only the globally focused window
               should receive keyboard events (prevents background windows from consuming keys) */
            if (!form->win.is_minimized && form->focused_control_id >= 0 &&
                global_wm.focused_index >= 0 && global_wm.windows[global_wm.focused_index] == form) {
                if (pump_handle_keyboard(form)) {
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->focused_control_id;
                }
            }

            /* Return clicked control ID directly (0 if none clicked) */
            if (event_count > 0) {
                /* If button was pressed, we need to redraw to show unpressed state */
                if (needs_redraw) {
                    mouse_restore();
                    mouse_invalidate_buffer();
                    if (changed_count > 0) {
                        /* Redraw only the specific controls that changed */
                        for (int i = 0; i < changed_count; i++) {
                            /* Check if this is a dropdown with hover change - only redraw list */
                            gui_control_t *ctrl = find_control_by_id(form, changed_controls[i]);
                            if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                                compositor_draw_dropdown_list_only(&global_wm, form, changed_controls[i]);
                            } else {
                                compositor_draw_control_by_id(&global_wm, form, changed_controls[i]);
                            }
                        }
                    } else if (z_order_changed) {
                        /* Z-order changed: full composite so no lower window paints over the new top */
                        compositor_draw_all(&global_wm);
                    } else {
                        /* Fallback to single window redraw if no specific controls tracked */
                        compositor_draw_single(&global_wm, form);
                    }
                }
                return form->clicked_id;
            }

            /* Return -1 if visual state changed and needs redraw */
            if (needs_redraw) {
                mouse_restore();
                mouse_invalidate_buffer();
                if (changed_count > 0) {
                    /* Redraw only the specific controls that changed */
                    for (int i = 0; i < changed_count; i++) {
                        /* Check if this is a dropdown with hover change - only redraw list */
                        gui_control_t *ctrl = find_control_by_id(form, changed_controls[i]);
                        if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                            compositor_draw_dropdown_list_only(&global_wm, form, changed_controls[i]);
                        } else {
                            compositor_draw_control_by_id(&global_wm, form, changed_controls[i]);
                        }
                    }
                } else if (z_order_changed) {
                    /* Z-order changed: full composite so no lower window paints over the new top */
                    compositor_draw_all(&global_wm);
                } else {
                    /* Fallback to single window redraw if no specific controls tracked */
                    compositor_draw_single(&global_wm, form);
                }
                /* Return clicked_id if it was set (e.g., scrollbar value changed), otherwise -1 */
                return (form->clicked_id >= 0) ? (uint32_t)form->clicked_id : (uint32_t)-1;
            }

            return 0;
        }

        case 0x08: { /* SYS_WIN_ADD_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            gui_control_t *ctrl = (gui_control_t*)ecx;

            if (!form || !ctrl) return 0;

            /* Allocate or expand controls array */
            if (!form->controls) {
                form->controls = (gui_control_t*)kmalloc(sizeof(gui_control_t) * WM_MAX_CONTROLS_PER_FORM);
                if (!form->controls) return 0;
            }

            /* Add control (simple append for now) */
            if (form->ctrl_count < WM_MAX_CONTROLS_PER_FORM) {
                /* Copy the control data */
                gui_control_t *dest = &form->controls[form->ctrl_count];
                dest->id = ctrl->id;
                dest->type = ctrl->type;
                dest->font_type = ctrl->font_type;
                dest->font_size = ctrl->font_size;
                dest->x = ctrl->x;
                dest->y = ctrl->y;
                dest->w = ctrl->w;
                dest->h = ctrl->h;
                dest->fg = ctrl->fg;
                dest->bg = ctrl->bg;
                dest->border = ctrl->border;
                dest->border_color = ctrl->border_color;
                strcpy_s(dest->text, ctrl->text, 256);
                
                /* Initialize union based on control type (mask off hidden flag 0x80) */
                switch (dest->type & 0x7F) {
                    case CTRL_BUTTON:
                        dest->button.cached_bitmap_orig = NULL;
                        dest->button.pressed = 0;
                        break;
                    case CTRL_PICTUREBOX:
                        dest->picturebox.cached_bitmap_orig = NULL;
                        dest->picturebox.cached_bitmap_scaled = NULL;
                        dest->picturebox.image_mode = ctrl->picturebox.image_mode;
                        dest->picturebox.load_failed = 0;
                        break;
                    case CTRL_CHECKBOX:
                        dest->checkbox.checked = 0;
                        dest->checkbox.group_id = ctrl->checkbox.group_id;
                        break;
                    case CTRL_RADIOBUTTON:
                        dest->radiobutton.checked = 0;
                        dest->radiobutton.group_id = ctrl->radiobutton.group_id;
                        break;
                    case CTRL_TEXTBOX:
                        dest->textbox.cursor_pos = 0;
                        dest->textbox.max_length = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
                        dest->textbox.scroll_offset = 0;
                        dest->textbox.is_focused = 0;
                        dest->textbox.sel_start = -1;
                        dest->textbox.sel_end = -1;
                        break;
                    case CTRL_DROPDOWN:
                        dest->dropdown.dropdown_open = 0;
                        dest->dropdown.item_count = ctrl->dropdown.item_count;
                        dest->dropdown.hovered_item = -1;
                        dest->dropdown.dropdown_saved_bg = NULL;
                        dest->dropdown.dropdown_saved_w = 0;
                        dest->dropdown.dropdown_saved_h = 0;
                        dest->dropdown.dropdown_saved_x = 0;
                        dest->dropdown.dropdown_saved_y = 0;
                        dest->dropdown.dropdown_scroll = 0;
                        dest->dropdown.pressed = 0;
                        dest->dropdown.scroll_offset = 0;
                        dest->dropdown.cursor_pos = 0;
                        break;
                    case CTRL_SCROLLBAR:
                        dest->scrollbar.hovered_item = -1;
                        dest->scrollbar.pressed = 0;
                        dest->scrollbar.cursor_pos = 0;
                        dest->scrollbar.max_length = 100;
                        dest->scrollbar.checked = 0;
                        break;
                    case CTRL_ICON:
                        dest->icon.cached_bitmap_orig = NULL;
                        dest->icon.saved_bg = NULL;
                        dest->icon.checked = 0;
                        dest->icon.dragging = 0;
                        dest->icon.drag_offset_x = 0;
                        dest->icon.drag_offset_y = 0;
                        dest->icon.original_x = 0;
                        dest->icon.original_y = 0;
                        dest->icon.click_start_x = 0;
                        dest->icon.click_start_y = 0;
                        break;
                    default:
                        /* No special initialization */
                        break;
                }

                /* For textbox, update cursor to end of text */
                if (dest->type == CTRL_TEXTBOX) {
                    int len = 0;
                    while (dest->text[len]) len++;
                    dest->textbox.cursor_pos = len;
                }

                form->ctrl_count++;
            }
            return 0;
        }

        case 0x09: { /* SYS_WIN_DRAW */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            /* Use compositor to draw with correct focus state */
            compositor_draw_single(&global_wm, form);
            return 0;
        }

        case 0x0A: { /* SYS_WIN_DESTROY_FORM */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;

            /* Release icon slot if window was minimized */
            if (form->win.is_minimized) {
                int icon_x = 0, icon_y = 0;
                if (form->win.minimized_icon_id != -1 && form->controls) {
                    gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                    if (ctrl) {
                        icon_x = ctrl->x;
                        icon_y = ctrl->y;
                    }
                }
                if (icon_x || icon_y) {
                    wm_release_icon_slot(&global_wm, icon_x, icon_y);
                }
            }

            /* Unregister from window manager */
            wm_unregister_window(&global_wm, form);

            /* Free cached bitmaps in controls and restore any open dropdown backgrounds */
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    gui_control_t *ctrl = &form->controls[i];

                    switch (ctrl->type) {
                        case CTRL_BUTTON:
                            if (ctrl->button.cached_bitmap_orig) {
                                bitmap_free(ctrl->button.cached_bitmap_orig);
                                ctrl->button.cached_bitmap_orig = NULL;
                            }
                            break;

                        case CTRL_PICTUREBOX:
                            if (ctrl->picturebox.cached_bitmap_orig) {
                                bitmap_free(ctrl->picturebox.cached_bitmap_orig);
                                ctrl->picturebox.cached_bitmap_orig = NULL;
                            }
                            if (ctrl->picturebox.cached_bitmap_scaled) {
                                bitmap_free(ctrl->picturebox.cached_bitmap_scaled);
                                ctrl->picturebox.cached_bitmap_scaled = NULL;
                            }
                            break;

                        case CTRL_ICON:
                            if (ctrl->icon.cached_bitmap_orig) {
                                bitmap_free(ctrl->icon.cached_bitmap_orig);
                                ctrl->icon.cached_bitmap_orig = NULL;
                            }
                            if (ctrl->icon.saved_bg) {
                                kfree(ctrl->icon.saved_bg);
                                ctrl->icon.saved_bg = NULL;
                            }
                            break;

                        case CTRL_DROPDOWN:
                            if (ctrl->dropdown.dropdown_saved_bg) {
                                ctrl_hide_dropdown_list(&form->win, ctrl);
                            }
                            break;

                        default:
                            break;
                    }
                }
                kfree(form->controls);
                form->controls = NULL;
            }

            /* Destroy window menu */
            if (form->window_menu_initialized) {
                menu_destroy(&form->window_menu);
            }

            /* Destroy window */
            win_destroy(&form->win);

            /* Free form */
            kfree(form);

            /* Signal desktop to do full redraw */
            global_wm.needs_full_redraw = 1;

            return 0;
        }

        case 0x0B: { /* SYS_WIN_SET_ICON */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *icon_path = (const char*)ecx;

            if (!form || !icon_path) return 0;

            /* Store icon path for later use when minimizing */
            strcpy_s(form->icon_path, icon_path, 64);

            /* Create a CTRL_ICON control on the form if not already created */
            #define FORM_ICON_CONTROL_ID 5000
            int existing_icon_id = -1;
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    /* Check for CTRL_ICON with or without hidden flag (0x80) */
                    if ((form->controls[i].type & 0x7F) == CTRL_ICON && 
                        form->controls[i].id == FORM_ICON_CONTROL_ID) {
                        existing_icon_id = i;
                        break;
                    }
                }
            }

            if (existing_icon_id == -1) {
                /* Create new icon control - hidden (0x80 flag makes it invisible) */
                gui_control_t icon_ctrl = {0};
                icon_ctrl.type = CTRL_ICON | 0x80;  /* Hidden flag */
                icon_ctrl.id = FORM_ICON_CONTROL_ID;
                icon_ctrl.x = -100;  /* Off-screen */
                icon_ctrl.y = -100;
                icon_ctrl.w = 0;
                icon_ctrl.h = 0;
                icon_ctrl.fg = 0;
                icon_ctrl.bg = 15;
                strcpy_s(icon_ctrl.text, form->win.title, 256);
                
                sys_win_add_control(form, &icon_ctrl);
                ctrl_set_image(form, FORM_ICON_CONTROL_ID, icon_path);
            } else {
                /* Update existing icon control */
                gui_control_t *ctrl = &form->controls[existing_icon_id];
                strcpy_s(ctrl->text, form->win.title, 256);
                ctrl_set_image(form, FORM_ICON_CONTROL_ID, icon_path);
            }

            return 0;
        }

        case 0x0C: { /* SYS_WIN_REDRAW_ALL */
            compositor_draw_all(&global_wm);
            return 0;
        }

        case 0x0D: { /* SYS_WIN_GET_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t id = (int16_t)ecx;

            if (!form) return 0;

            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == id) {
                    return (uint32_t)&form->controls[i];
                }
            }
            return 0;
        }

        case 0x0E: { /* SYS_WIN_CTRL_SET_PROP */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t ctrl_id = (int16_t)(ecx >> 16);
            int16_t prop_id = (int16_t)(ecx & 0xFFFF);
            uint32_t value = edx;

            if (!form) return 0;

            /* Find control */
            gui_control_t *ctrl = NULL;
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == ctrl_id) {
                    ctrl = &form->controls[i];
                    break;
                }
            }
            if (!ctrl) return 0;

            switch (prop_id) {
                case 0: /* PROP_TEXT - universal */
                    if (value) strcpy_s(ctrl->text, (const char*)value, 256);
                    break;
                case 1: /* PROP_CHECKED */
                    switch (ctrl->type) {
                        case CTRL_CHECKBOX:
                            ctrl->checkbox.checked = (uint8_t)value;
                            break;
                        case CTRL_RADIOBUTTON:
                            ctrl->radiobutton.checked = (uint8_t)value;
                            break;
                        case CTRL_ICON:
                            ctrl->icon.checked = (uint8_t)value;
                            break;
                        default:
                            break;
                    }
                    break;
                case 2: /* PROP_X - universal */
                    ctrl->x = (uint16_t)value;
                    break;
                case 3: /* PROP_Y - universal */
                    ctrl->y = (uint16_t)value;
                    break;
                case 4: /* PROP_W - universal */
                    ctrl->w = (uint16_t)value;
                    break;
                case 5: /* PROP_H - universal */
                    ctrl->h = (uint16_t)value;
                    break;
                case 6: /* PROP_VISIBLE - universal */
                    if (value)
                        ctrl->type &= 0x7F;
                    else
                        ctrl->type |= 0x80;
                    break;
                case 7: /* PROP_FG - universal */
                    ctrl->fg = (uint8_t)value;
                    break;
                case 8: /* PROP_BG - universal */
                    ctrl->bg = (uint8_t)value;
                    break;
                case 9: /* PROP_IMAGE */
                    if (value) {
                        const char *path = (const char*)value;

                        if (ctrl->type == CTRL_BUTTON) {
                            if (ctrl->button.cached_bitmap_orig) {
                                bitmap_free(ctrl->button.cached_bitmap_orig);
                                ctrl->button.cached_bitmap_orig = NULL;
                            }
                            ctrl->button.cached_bitmap_orig = bitmap_load_from_file(path);
                        } else if (ctrl->type == CTRL_ICON) {
                            if (ctrl->icon.cached_bitmap_orig) {
                                bitmap_free(ctrl->icon.cached_bitmap_orig);
                                ctrl->icon.cached_bitmap_orig = NULL;
                            }
                            ctrl->icon.cached_bitmap_orig = bitmap_load_from_file(path);
                        } else if (ctrl->type == CTRL_PICTUREBOX) {
                            if (ctrl->picturebox.cached_bitmap_orig) {
                                bitmap_free(ctrl->picturebox.cached_bitmap_orig);
                                ctrl->picturebox.cached_bitmap_orig = NULL;
                            }
                            if (ctrl->picturebox.cached_bitmap_scaled) {
                                bitmap_free(ctrl->picturebox.cached_bitmap_scaled);
                                ctrl->picturebox.cached_bitmap_scaled = NULL;
                            }
                            ctrl->picturebox.load_failed = 0;
                            ctrl->text[0] = '\0';
                            strcpy_s(ctrl->text, path, sizeof(ctrl->text));
                        }
                    }
                    break;
                case 10: /* PROP_ENABLED - picturebox image_mode */
                    if (ctrl->type == CTRL_PICTUREBOX) {
                        ctrl->picturebox.image_mode = (uint8_t)value;
                    } else {
                        // For now - nothing. TODO: disabled controls
                    }
                    break;
                default:
                    return 0;
            }
            return 1;
        }

        case 0x0F: { /* SYS_WIN_CTRL_GET_PROP */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t ctrl_id = (int16_t)(ecx >> 16);
            int16_t prop_id = (int16_t)(ecx & 0xFFFF);

            if (!form) return 0;

            /* Find control */
            gui_control_t *ctrl = NULL;
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == ctrl_id) {
                    ctrl = &form->controls[i];
                    break;
                }
            }
            if (!ctrl) return 0;

            switch (prop_id) {
                case 0: /* PROP_TEXT */
                    return (uint32_t)ctrl->text;
                case 1: /* PROP_CHECKED */
                    if (ctrl->type == CTRL_CHECKBOX) {
                        return ctrl->checkbox.checked;
                    } else if (ctrl->type == CTRL_RADIOBUTTON) {
                        return ctrl->radiobutton.checked;
                    } else if (ctrl->type == CTRL_ICON) {
                        return ctrl->icon.checked;
                    }
                    return 0;
                case 2: /* PROP_X */
                    return ctrl->x;
                case 3: /* PROP_Y */
                    return ctrl->y;
                case 4: /* PROP_W */
                    return ctrl->w;
                case 5: /* PROP_H */
                    return ctrl->h;
                case 6: /* PROP_VISIBLE */
                    return (ctrl->type & 0x80) ? 0 : 1;
                case 7: /* PROP_FG */
                    return ctrl->fg;
                case 8: /* PROP_BG */
                    return ctrl->bg;
                case 9: /* PROP_IMAGE */
                    if (ctrl->type == CTRL_BUTTON) {
                        return (uint32_t)ctrl->button.cached_bitmap_orig;
                    } else if (ctrl->type == CTRL_ICON) {
                        return (uint32_t)ctrl->icon.cached_bitmap_orig;
                    } else if (ctrl->type == CTRL_PICTUREBOX) {
                        return (uint32_t)ctrl->text;
                    }
                    return 0;
                case 10: /* PROP_ENABLED - picturebox image_mode */
                    if (ctrl->type == CTRL_PICTUREBOX) {
                        return ctrl->picturebox.image_mode;
                    }
                    return 0;
                default:
                    return 0;
            }
        }

        case 0x10: { /* SYS_WIN_INVALIDATE_ICONS */
            compositor_invalidate_icon_backgrounds(&global_wm);
            return 0;
        }

        case 0x11: { /* SYS_WIN_CHECK_REDRAW - Check for full or partial redraw */
            /* Return codes: 0 = none, 1 = full redraw, 2 = partial redraw (dirty rect available) */
            if (global_wm.needs_full_redraw) {
                global_wm.needs_full_redraw = 0;
                return 1; /* full redraw */
            }

            /* If compositor has a pending dirty rectangle, signal a partial redraw */
            if (global_wm.dirty_w > 0 && global_wm.dirty_h > 0) {
                return 2; /* partial redraw */
            }

            return 0;
        }

        case 0x12: { /* SYS_WIN_GET_DIRTY_RECT - Get dirty rectangle */
            int *out = (int*)ebx;
            if (out) {
                out[0] = global_wm.dirty_x;
                out[1] = global_wm.dirty_y;
                out[2] = global_wm.dirty_w;
                out[3] = global_wm.dirty_h;
            }
            return 0;
        }

        case 0x13: { /* SYS_WIN_GET_THEME - Get current theme pointer */
            return (uint32_t)theme_get_current();
        }

        case 0x14: { /* SYS_WIN_CYCLE_PREVIEW - Cycle focus to next window (preview only) */
            if (global_wm.count <= 1) return 0;
            int start = global_wm.focused_index;
            int i = start;
            for (int k = 0; k < global_wm.count; k++) {
                i = (i + 1) % global_wm.count;
                gui_form_t *f = global_wm.windows[i];
                if (f && !f->win.is_minimized && f->win.is_visible) {
                    /* Set focused index but DO NOT change z-order - preview behavior */
                    global_wm.focused_index = i;
                    global_wm.needs_full_redraw = 1;
                    return 1;
                }
            }
            return 0;
        }

        case 0x15: { /* SYS_WIN_CYCLE_COMMIT - Bring the currently focused window to front */
            if (global_wm.focused_index < 0 || global_wm.focused_index >= global_wm.count) return 0;
            gui_form_t *f = global_wm.windows[global_wm.focused_index];
            if (!f || f->win.is_minimized || !f->win.is_visible) return 0;
            wm_bring_to_front(&global_wm, f);
            global_wm.needs_full_redraw = 1;
            return 1;
        }

        case 0x16: { /* SYS_WIN_RESTORE_FORM - Restore given form (if minimized) and bring to front */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;

            if (form->win.is_minimized) {
                int icon_x = 0, icon_y = 0;
                if (form->win.minimized_icon_id != -1 && form->controls) {
                    gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                    if (ctrl) {
                        icon_x = ctrl->x;
                        icon_y = ctrl->y;
                    }
                }
                if (icon_x || icon_y) {
                    wm_release_icon_slot(&global_wm, icon_x, icon_y);
                }
                win_restore(form);
            }

            wm_bring_to_front(&global_wm, form);
            /* Safety: ensure focused_index points to restored window */
            if (global_wm.count > 0) global_wm.focused_index = global_wm.count - 1;
            global_wm.needs_full_redraw = 1;
            return 2; /* Indicate full redraw */
        }

        case 0x17: { /* SYS_WIN_FORCE_FULL_REDRAW - request a desktop full redraw */
            /* Clear any saved/serialized screen patches that were captured
               earlier (saved_bg for windows, dropdown/menu saved backgrounds)
               so they don't restore stale wallpaper portions after the desktop
               background has changed. */
            for (int i = 0; i < global_wm.count; i++) {
                gui_form_t *f = global_wm.windows[i];
                if (!f) continue;

                /* Free per-window saved background (will be re-captured on next draw) */
                if (f->win.saved_bg) {
                    kfree(f->win.saved_bg);
                    f->win.saved_bg = NULL;
                }

                /* Free any saved dropdown background for controls */
                if (f->controls) {
                    for (int j = 0; j < f->ctrl_count; j++) {
                        if (f->controls[j].type == CTRL_DROPDOWN && f->controls[j].dropdown.dropdown_saved_bg) {
                            kfree(f->controls[j].dropdown.dropdown_saved_bg);
                            f->controls[j].dropdown.dropdown_saved_bg = NULL;
                        }
                    }
                }

                /* Free any saved menu background */
                if (f->window_menu.saved_bg) {
                    kfree(f->window_menu.saved_bg);
                    f->window_menu.saved_bg = NULL;
                }
            }

            /* Also invalidate cached icon backgrounds */
            compositor_invalidate_icon_backgrounds(&global_wm);

            global_wm.needs_full_redraw = 1;
            return 1;
        }

        case 0x18: { /* SYS_WIN_IS_FOCUSED - return 1 if given form is focused/topmost */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            if (global_wm.focused_index >= 0 && global_wm.focused_index < global_wm.count &&
                global_wm.windows[global_wm.focused_index] == form) {
                return 1;
            }
            return 0;
        }

        case 0x19: { /* SYS_WIN_DRAW_BUFFER - draw a user buffer into a form but clipped to regions where the form is topmost */
            struct {
                gui_form_t *form;
                const uint8_t *buffer;
                int buf_w;
                int buf_h;
                int src_x;
                int src_y;
                int src_w;
                int src_h;
                int dest_x;
                int dest_y;
                int transparent;
            } *p = (void*)ebx;

            if (!p || !p->form || !p->buffer) return -1;

            /* Clip source rectangle to buffer bounds */
            int sx = p->src_x < 0 ? 0 : p->src_x;
            int sy = p->src_y < 0 ? 0 : p->src_y;
            int sw = p->src_w;
            int sh = p->src_h;
            if (sx + sw > p->buf_w) sw = p->buf_w - sx;
            if (sy + sh > p->buf_h) sh = p->buf_h - sy;
            if (sw <= 0 || sh <= 0) return -1;

            /* Destination is relative to the window client area (titlebar offset = 20) */
            int dest_base_x = p->form->win.x + p->dest_x;
            int dest_base_y = p->form->win.y + 20 + p->dest_y;

            for (int y = 0; y < sh; y++) {
                int screen_y = dest_base_y + y;
                if (screen_y < 0 || screen_y >= WM_SCREEN_HEIGHT) continue;
                for (int x = 0; x < sw; x++) {
                    int screen_x = dest_base_x + x;
                    if (screen_x < 0 || screen_x >= WM_SCREEN_WIDTH) continue;

                    /* Only draw if this form is topmost at this pixel */
                    gui_form_t *top = wm_get_window_at(&global_wm, screen_x, screen_y);
                    if (top != p->form) continue;

                    int px = p->buffer[(sy + y) * p->buf_w + (sx + x)];
                    if (p->transparent && px == 5) continue; /* color 5 as transparent for compatibility */
                    gfx_putpixel(screen_x, screen_y, (uint8_t)px);
                }
            }

            return 0;
        }

        case 0x1A: { /* SYS_WIN_MARK_DIRTY - mark window region as dirty and trigger compositor redraw with z-order */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form || !form->win.is_visible || form->win.is_minimized) return 0;

            /* Calculate window bounds including margins */
            int wx = form->win.x - WM_BG_MARGIN;
            int wy = form->win.y - WM_BG_MARGIN;
            int ww = form->win.w + (WM_BG_MARGIN * 2);
            int wh = form->win.h + (WM_BG_MARGIN * 2);

            /* Set dirty rect and trigger compositor redraw */
            compositor_set_dirty_rect(&global_wm, wx, wy, ww, wh);
            compositor_draw_all(&global_wm);

            return 0;
        }

        case 0x1B: { /* SYS_WIN_MARK_DIRTY_RECT - mark arbitrary screen rect as dirty and redraw overlapping windows */
            struct { int x; int y; int w; int h; } *p = (void*)ebx;
            if (!p) return 0;
            /* Sanitize/clamp rect to screen */
            int rx = p->x < 0 ? 0 : p->x;
            int ry = p->y < 0 ? 0 : p->y;
            int rw = p->w;
            int rh = p->h;
            if (rw <= 0 || rh <= 0) return 0;
            if (rx + rw > WM_SCREEN_WIDTH) rw = WM_SCREEN_WIDTH - rx;
            if (ry + rh > WM_SCREEN_HEIGHT) rh = WM_SCREEN_HEIGHT - ry;
            compositor_set_dirty_rect(&global_wm, rx, ry, rw, rh);
            compositor_draw_all(&global_wm);
            return 0;
        }

        case 0x1C: { /* SYS_WIN_MENUBAR_ENABLE */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            menubar_init(&form->menubar);
            form->menubar_enabled = 1;
            return 0;
        }

        case 0x1D: { /* SYS_WIN_MENUBAR_ADD_MENU */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *title = (const char*)ecx;
            if (!form || !title) return (uint32_t)-1;
            if (!form->menubar_enabled) return (uint32_t)-1;
            return (uint32_t)menubar_add_menu(&form->menubar, title);
        }

        case 0x1E: { /* SYS_WIN_MENUBAR_ADD_ITEM */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *text = (const char*)ecx;
            uint32_t packed = edx;
            int menu_index = (int)(packed >> 16);
            int action_id = (int)(packed & 0xFFFF);
            if (!form || !text) return (uint32_t)-1;
            if (!form->menubar_enabled) return (uint32_t)-1;
            menu_t *menu = menubar_get_menu(&form->menubar, menu_index);
            if (!menu) return (uint32_t)-1;
            return (uint32_t)menu_add_item(menu, text, action_id, MENU_ITEM_ENABLED);
        }

        case 0x1F: { /* SYS_WIN_SET_RESIZABLE */
            gui_form_t *form = (gui_form_t*)ebx;
            int resizable = (int)ecx;
            if (!form) return 0;
            form->win.resizable = resizable ? 1 : 0;
            return 0;
        }

        case 0x20: { /* SYS_WIN_DRAW_TASKBAR_BUTTON */
            struct { int x; int y; int w; int h; const char *icon; const char *label; int pressed; uint8_t color; } *params;
            params = (void*)ebx;
            if (!params) return 0;
            
            /* Create a temporary control to render with unified button logic */
            gui_control_t temp_ctrl = {0};
            temp_ctrl.type = CTRL_BUTTON;
            temp_ctrl.w = params->w;
            temp_ctrl.h = params->h;
            temp_ctrl.button.pressed = params->pressed ? 1 : 0;
            temp_ctrl.bg = params->color;
            temp_ctrl.fg = COLOR_BLACK;
            
            /* Set label if provided */
            if (params->label && params->label[0]) {
                int len = 0;
                while (params->label[len] && len < 255) {
                    temp_ctrl.text[len] = params->label[len];
                    len++;
                }
                temp_ctrl.text[len] = '\0';
            }
            
            /* Load icon if provided */
            if (params->icon && params->icon[0]) {
                temp_ctrl.button.cached_bitmap_orig = bitmap_load_from_file(params->icon);
            }
            
            /* Draw using unified control rendering */
            ctrl_draw_button(&temp_ctrl, params->x, params->y);
            
            /* Clean up loaded bitmap */
            if (temp_ctrl.button.cached_bitmap_orig) {
                bitmap_free(temp_ctrl.button.cached_bitmap_orig);
            }
            
            return 0;
        }

        case 0x21: { /* SYS_WIN_GET_TOPMOST_AT - return the topmost window at position (x, y) */
            int x = ebx;
            int y = ecx;
            gui_form_t *topmost = wm_get_window_at(&global_wm, x, y);
            return (uint32_t)topmost;
        }

        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_sound(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00:
            return (uint32_t)sb16_detected();

        case 0x01:
            sb16_play_tone((uint16_t)ebx, ecx, (uint8_t)edx);
            return 0;

        case 0x02:
            sb16_set_volume((uint8_t)ebx, (uint8_t)ecx);
            return 0;

        case 0x05:
            return (uint32_t)sb16_get_volume();

        case 0x03:
            sound_stop();
            return 0;

        case 0x04:
            return (uint32_t)sound_play_wav((const char *)ebx);

        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_power(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)ebx; (void)ecx; (void)edx;

    switch (al) {
        case 0x00: { /* SYS_POWER_SHUTDOWN */
            __asm__ volatile("cli");

            /* Try ACPI shutdown (QEMU, modern hardware) */
            outw(0x604, 0x2000);  /* QEMU */
            outw(0xB004, 0x2000); /* Bochs, older QEMU */

            /* Try VirtualBox */
            outw(0x4004, 0x3400);

            /* If still running, show message and halt */
            vga_clear();
            vga_set_color(0, 6);
            vga_set_cursor(19,11);
            printf("┌────────────────────────────────────────┐\n");
            for (int i = 0; i < 19; i++) printf(" ");
            printf("│It is now safe to turn off your computer│\n");
            for (int i = 0; i < 19; i++) printf(" ");
            printf("└────────────────────────────────────────┘");
            vga_set_cursor(0,0);
            for (;;) __asm__ volatile("hlt");
            return 0;
        }

        case 0x01: { /* SYS_POWER_REBOOT */
            __asm__ volatile("cli");

            /* Try keyboard controller reset (most reliable) */
            uint8_t temp;
            do {
                temp = inb(0x64);
                if (temp & 0x01) inb(0x60);
            } while (temp & 0x02);

            outb(0x64, 0xFE);  /* Pulse CPU reset line */

            /* If that didn't work, try ACPI reset */
            for (volatile int i = 0; i < 100000; i++);
            outb(0xCF9, 0x06);

            /* Last resort - halt */
            vga_clear();
            vga_set_color(0, 6);
            vga_set_cursor(20,11);
            printf("┌──────────────────────────────────────┐\n");
            for (int i = 0; i < 20; i++) printf(" ");
            printf("│Press the reset button on your PC unit│\n");
            for (int i = 0; i < 20; i++) printf(" ");
            printf("└──────────────────────────────────────┘");
            vga_set_cursor(0,0);
            for (;;) __asm__ volatile("hlt");
            return 0;
        }

        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_vconsole(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    task_t *cur = task_get_current();

    switch (al) {
        case 0x00: { /* SYS_VC_CREATE */
            if (!cur) return 0;
            vconsole_t *vc = vc_create(cur->tid);
            return (uint32_t)vc;
        }
        case 0x01: { /* SYS_VC_DESTROY */
            vconsole_t *vc = (vconsole_t*)ebx;
            if (!vc) return (uint32_t)-1;
            vc_destroy(vc);
            return 0;
        }
        case 0x02: { /* SYS_VC_ATTACH */
            vconsole_t *vc = (vconsole_t*)ebx;
            uint32_t tid = (uint32_t)ecx;
            if (!vc) return (uint32_t)-1;
            task_t *t = task_find_by_tid(tid);
            if (!t) return (uint32_t)-1;
            t->vconsole = (struct vconsole *)vc;
            return 0;
        }
        case 0x03: { /* SYS_VC_READ */
            vconsole_t *vc = (vconsole_t*)ebx;
            if (!vc || !ecx) return (uint32_t)-1;
            uint8_t *dst = (uint8_t*)ecx;
            memcpy_s(dst, vc->chars, VC_ROWS * VC_COLS);
            memcpy_s(dst + VC_ROWS * VC_COLS, vc->attrs, VC_ROWS * VC_COLS);
            int *cursor = (int*)(dst + 2 * VC_ROWS * VC_COLS);
            cursor[0] = vc->cursor_x;
            cursor[1] = vc->cursor_y;
            return 0;
        }
        case 0x04: { /* SYS_VC_SEND_KEY */
            vconsole_t *vc = (vconsole_t*)ebx;
            if (!vc) return (uint32_t)-1;
            vc_send_key(vc, (uint8_t)ecx);
            return 0;
        }
        case 0x05: { /* SYS_VC_DIRTY */
            vconsole_t *vc = (vconsole_t*)ebx;
            if (!vc) return 0;
            uint8_t was = vc->dirty;
            vc->dirty = 0;
            return was;
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
        case 0x0A: return handle_mouse(al, ebx, ecx, edx);
        case 0x0B: return handle_window(al, ebx, ecx, edx);
        case 0x0C: return handle_power(al, ebx, ecx, edx);
        case 0x0E: return handle_sound(al, ebx, ecx, edx);
        case 0x0F: return handle_vconsole(al, ebx, ecx, edx);
        case 0x0D: {
            /* Input namespace - subcodes in AL */
            switch (al) {
                case 0x00: /* SYS_GET_KEY_NONBLOCK */
                    return (uint32_t)kbd_getchar_nonblock();
                case 0x01: { /* SYS_GET_ALT_KEY - peek+consume only Alt+Tab / AltRelease */
                    int k = kbd_peek_nonblock();
                    if (k == 0) return 0;
                    if (k == KEY_ALT_TAB || k == KEY_ALT_RELEASE) {
                        /* Consume and return */
                        (void)kbd_getchar_nonblock();
                        return (uint32_t)k;
                    }
                    return 0;
                }
                default:
                    return (uint32_t)-1;
            }
        }

        default:
            return (uint32_t)-1;
    }
}

/* Cleanup all windows owned by a task (called on task exit) */
void wm_cleanup_task(uint32_t tid) {
    if (!wm_initialized) return;

    /* Iterate through all windows and destroy those owned by this task */
    int cleaned = 0;
    for (int i = global_wm.count - 1; i >= 0; i--) {
        gui_form_t *form = global_wm.windows[i];
        if (!form || form->owner_tid != tid) continue;

        /* Release icon slot if window was minimized */
        if (form->win.is_minimized) {
            int icon_x = 0, icon_y = 0;
            if (form->win.minimized_icon_id != -1 && form->controls) {
                gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                if (ctrl) {
                    icon_x = ctrl->x;
                    icon_y = ctrl->y;
                }
            }
            if (icon_x || icon_y) {
                wm_release_icon_slot(&global_wm, icon_x, icon_y);
            }
        }

        /* Unregister from window manager */
        wm_unregister_window(&global_wm, form);

        /* Free cached bitmaps in controls */
        if (form->controls) {
            for (int j = 0; j < form->ctrl_count; j++) {
                gui_control_t *ctrl = &form->controls[j];
                /* Free bitmaps based on control type */
                if (ctrl->type == CTRL_BUTTON && ctrl->button.cached_bitmap_orig) {
                    bitmap_free(ctrl->button.cached_bitmap_orig);
                    ctrl->button.cached_bitmap_orig = NULL;
                }
                if (ctrl->type == CTRL_PICTUREBOX) {
                    if (ctrl->picturebox.cached_bitmap_orig) {
                        bitmap_free(ctrl->picturebox.cached_bitmap_orig);
                        ctrl->picturebox.cached_bitmap_orig = NULL;
                    }
                    if (ctrl->picturebox.cached_bitmap_scaled) {
                        bitmap_free(ctrl->picturebox.cached_bitmap_scaled);
                        ctrl->picturebox.cached_bitmap_scaled = NULL;
                    }
                }
                if (ctrl->type == CTRL_ICON && ctrl->icon.cached_bitmap_orig) {
                    bitmap_free(ctrl->icon.cached_bitmap_orig);
                    ctrl->icon.cached_bitmap_orig = NULL;
                }
            }
            kfree(form->controls);
        }

        /* Destroy window menu */
        if (form->window_menu_initialized) {
            menu_destroy(&form->window_menu);
        }

        /* Destroy window (restores background) */
        win_destroy(&form->win);

        /* Free form */
        kfree(form);
        cleaned = 1;
    }
    /* If we removed any windows, request a full desktop redraw so wallpaper and
       desktop elements behind the removed windows are repainted. */
    if (cleaned) {
        global_wm.needs_full_redraw = 1;
        /* Request immediate redraw so wallpaper/background is repainted now */
        compositor_draw_all(&global_wm);
    }
}

void syscall_init(void) {
    fd_init();
}
