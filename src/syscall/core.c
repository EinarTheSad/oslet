#include "internal.h"

#define MAX_OPEN_FILES 32
static char shell_version_buf[64] = "";
static int textmode_requested = 0;

typedef struct {
    void *data;
    int width;
    int height;
} cached_bmp_t;

#define FD_CRITICAL_BEGIN \
    uint32_t _fd_eflags; \
    __asm__ volatile("pushfl\n\tmovl (%%esp), %0\n\taddl $4, %%esp\n\tcli" : "=r"(_fd_eflags) :: "memory")
#define FD_CRITICAL_END \
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(_fd_eflags) : "cc", "memory")
uint32_t sys_irq_save(void) {
    uint32_t eflags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(eflags) :: "memory");
    return eflags;
}

void sys_irq_restore(uint32_t eflags) {
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(eflags) : "cc", "memory");
}

int sys_range_mapped(uint32_t addr, size_t size) {
    if (size == 0) return 1;
    if (!addr) return 0;

    uint32_t end = addr + (uint32_t)size - 1;
    if (end < addr) return 0;

    uint32_t page = addr & ~(PAGE_SIZE - 1);
    uint32_t last = end & ~(PAGE_SIZE - 1);
    while (1) {
        if (!paging_is_mapped(page)) return 0;
        if (page == last) break;
        if (page > 0xFFFFFFFFu - PAGE_SIZE) return 0;
        page += PAGE_SIZE;
    }
    return 1;
}

int sys_copy_string(char *dst, uint32_t src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return -1;

    for (size_t i = 0; i < dst_size - 1; i++) {
        if (!sys_range_mapped(src + (uint32_t)i, 1)) {
            dst[0] = '\0';
            return -1;
        }

        char ch = *(const char*)(uintptr_t)(src + (uint32_t)i);
        dst[i] = ch;
        if (ch == '\0') return 0;
    }

    dst[dst_size - 1] = '\0';
    return -1;
}

static int task_name_is(task_t *task, const char *name) {
    if (!task || !name) return 0;

    const char *a = task->name;
    const char *b = name;
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int current_task_can_manage_process(task_t *current) {
    return task_name_is(current, "SHELL.ELF") ||
           task_name_is(current, "DESKTOP.ELF") ||
           task_name_is(current, "SHELL") ||
           task_name_is(current, "DESKTOP");
}

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
    fat32_file_t *to_close[MAX_OPEN_FILES];
    int close_count = 0;

    uint32_t eflags = sys_irq_save();
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (fd_table[i].in_use && fd_table[i].owner_tid == tid) {
            if (fd_table[i].file) {
                to_close[close_count++] = fd_table[i].file;
            }
            fd_table[i].in_use = 0;
            fd_table[i].file = NULL;
            fd_table[i].owner_tid = 0;
        }
    }
    sys_irq_restore(eflags);

    for (int i = 0; i < close_count; i++) {
        fat32_close(to_close[i]);
    }
}

static uint32_t handle_console(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    char text_buf[1024];

    task_t *cur = task_get_current();
    if (cur && cur->vconsole) {
        vconsole_t *vc = (vconsole_t *)cur->vconsole;
        switch (al) {
            case 0x00:
                if (sys_copy_string(text_buf, ebx, sizeof(text_buf)) != 0) return (uint32_t)-1;
                vc_write(vc, text_buf);
                return 0;
            case 0x01:
                if (!sys_range_mapped(ebx, ecx)) return (uint32_t)-1;
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
                if (!sys_range_mapped(ebx, sizeof(int)) || !sys_range_mapped(ecx, sizeof(int))) return (uint32_t)-1;
                vc_get_cursor(vc, (int*)ebx, (int*)ecx);
                return 0;
            case 0x07:
                if (!sys_range_mapped(ebx, VC_COLS * VC_ROWS) || !sys_range_mapped(ecx, VC_COLS * VC_ROWS)) return (uint32_t)-1;
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
            if (sys_copy_string(text_buf, ebx, sizeof(text_buf)) != 0) return -1;
            for (const char *s = text_buf; *s; s++) putchar(*s);
            return 0;

        case 0x01:
            if (!sys_range_mapped(ebx, ecx)) return -1;
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
            if (!sys_range_mapped(ebx, sizeof(int)) || !sys_range_mapped(ecx, sizeof(int))) return -1;
            vga_get_cursor((int*)ebx, (int*)ecx);
            return 0;

        case 0x07: { /* Blit screen buffer */
            if (!sys_range_mapped(ebx, 80 * 25) || !sys_range_mapped(ecx, 80 * 25)) return -1;
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
    char path[FAT32_MAX_PATH];
    char args[256];
    
    switch (al) {
        case 0x00:
            task_exit();
            return 0;
            
        case 0x01: {
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            exec_image_t image;
            if (exec_load(path, &image) != 0) return -1;
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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            if (ecx && sys_copy_string(args, ecx, sizeof(args)) != 0) return -1;
            if (!ecx) args[0] = '\0';
            return task_spawn_and_wait(path, args);

        case 0x06:  /* SYS_PROC_SPAWN_ASYNC - spawn without waiting */
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            if (ecx && sys_copy_string(args, ecx, sizeof(args)) != 0) return -1;
            if (!ecx) args[0] = '\0';
            return task_spawn(path, args);

        case 0x07: { /* SYS_PROC_SET_ICON */
            if (!ebx || sys_copy_string(path, ecx, sizeof(path)) != 0) return (uint32_t)-1;
            task_t *t = task_find_by_tid((uint32_t)ebx);
            if (!t) return (uint32_t)-1;
            strcpy_s(t->icon_path, path, 64);
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
            if (current && t->parent_tid != current->tid && !current_task_can_manage_process(current)) {
                return (uint32_t)-1;
            }

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
            task_t *current = task_get_current();
            if (!current) return (uint32_t)NULL;
            
            char *buf = (char*)ebx;
            int len = (int)ecx;
            
            if (len > 0) {
                size_t copy_len = (len > (int)sizeof(current->args)) ? sizeof(current->args) : (size_t)len;
                if (!sys_range_mapped(ebx, copy_len)) return (uint32_t)NULL;
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
    char path[FAT32_MAX_PATH];
    char path2[FAT32_MAX_PATH];
    char mode[8];

    switch (al) {
        case 0x00: {
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            if (sys_copy_string(mode, ecx, sizeof(mode)) != 0) return (uint32_t)-1;
            
            fat32_file_t *file = fat32_open(path, mode);
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
            if (!sys_range_mapped(ecx, edx)) return (uint32_t)-1;
            
            fat32_file_t *file = fd_get((int)ebx);
            if (!file) return (uint32_t)-1;
            
            int result = fat32_read(file, (void*)ecx, (size_t)edx);
            return (uint32_t)result;
        }
            
        case 0x03: {
            if (!sys_range_mapped(ecx, edx)) return (uint32_t)-1;
            
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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            int result = fat32_unlink(path);
            return (uint32_t)result;
        }
        
        case 0x06: {
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            if (sys_copy_string(path2, ecx, sizeof(path2)) != 0) return (uint32_t)-1;
            int result = fat32_rename(path, path2);
            return (uint32_t)result;
        }
            
        default:
            return (uint32_t)-1;
    }
}

static uint32_t handle_dir(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    char path[FAT32_MAX_PATH];

    switch (al) {
        case 0x00:
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            return fat32_chdir(path);
            
        case 0x01: {
            if (ecx == 0 || !sys_range_mapped(ebx, ecx)) return -1;
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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            return fat32_mkdir(path);
        
        case 0x03:
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            return fat32_rmdir(path);
        
        case 0x04: {
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return -1;
            if (edx > 256) edx = 256;
            if (edx == 0 || !sys_range_mapped(ecx, sizeof(sys_dirent_t) * edx)) return -1;
            fat32_dirent_t *fat_entries = (fat32_dirent_t*)kmalloc(sizeof(fat32_dirent_t) * edx);
            if (!fat_entries) return -1;
            
            int count = fat32_list_dir(path, fat_entries, (int)edx);
            
            if (count > 0) {
                sys_dirent_t *sys_entries = (sys_dirent_t*)ecx;
                int max_copy = (count < (int)edx) ? count : (int)edx;
                for (int i = 0; i < max_copy; i++) {
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
            if (edx > MSG_MAX_SIZE) return -1;
            if (!sys_range_mapped(ecx, edx)) return -2;
            
            task_t *sender = task_get_current();
            if (!sender) return -3;
            
            task_t *receiver = task_find_by_tid(ebx);
            if (!receiver) return -4;

            message_t local_msg;
            local_msg.from_tid = sender->tid;
            local_msg.to_tid = ebx;
            local_msg.size = edx;
            if (edx > 0) {
                memcpy_s(local_msg.data, (const void*)ecx, edx);
            }
            
            uint32_t eflags = sys_irq_save();
            msg_queue_t *q = &receiver->msg_queue;
            if (q->count >= MSG_QUEUE_SIZE) {
                sys_irq_restore(eflags);
                return -5;
            }
            
            message_t *msg = &q->msgs[q->head];
            memcpy_s(msg, &local_msg, sizeof(message_t));
            
            q->head = (q->head + 1) % MSG_QUEUE_SIZE;
            q->count++;
            
            if (receiver->state == TASK_BLOCKED) {
                receiver->state = TASK_READY;
            }
            sys_irq_restore(eflags);
            return 0;
        }
            
        case 0x01: {
            if (!sys_range_mapped(ebx, sizeof(message_t))) return -1;
            
            task_t *current = task_get_current();
            if (!current) return -2;
            
            msg_queue_t *q = &current->msg_queue;
            uint32_t eflags = sys_irq_save();
            if (q->count == 0) {
                current->state = TASK_BLOCKED;
                sys_irq_restore(eflags);
                task_yield();
                eflags = sys_irq_save();
                if (q->count == 0) {
                    sys_irq_restore(eflags);
                    return -3;
                }
            }
            
            message_t local_msg;
            message_t *msg = &q->msgs[q->tail];
            memcpy_s(&local_msg, msg, sizeof(message_t));
            
            q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
            q->count--;
            sys_irq_restore(eflags);

            memcpy_s((void*)ebx, &local_msg, sizeof(message_t));
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
            if (!sys_range_mapped(ebx, sizeof(sys_time_t))) return -1;
            
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
            if (!sys_range_mapped(ebx, sizeof(sys_meminfo_t))) return -1;
            
            sys_meminfo_t *info = (sys_meminfo_t*)ebx;
            size_t total = pmm_total_frames();
            size_t free = pmm_count_free_frames();
            
            info->total_kb = (total * 4096) / 1024;
            info->free_kb = (free * 4096) / 1024;
            info->used_kb = info->total_kb - info->free_kb;
            return 0;
        }
        
        case 0x01: {
            int max = (int)ecx;
            if (max <= 0) return 0;
            if (max > 64) max = 64;
            if (!sys_range_mapped(ebx, sizeof(sys_taskinfo_t) * (size_t)max)) return -1;
            
            sys_taskinfo_t *tasks = (sys_taskinfo_t*)ebx;
            int count = 0;
            
            task_t *start = task_get_current();
            if (!start) return 0;
            
            uint32_t eflags = sys_irq_save();
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
            sys_irq_restore(eflags);
            
            return count;
        }

        case 0x02:
            return (uint32_t)(uintptr_t)kernel_version;

        case 0x03: {
            if (!sys_range_mapped(ebx, sizeof(sys_heapinfo_t))) return -1;
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
            return (uint32_t)(uintptr_t)shell_version_buf;

        case 0x05:
            if (sys_copy_string(shell_version_buf, ebx, sizeof(shell_version_buf)) != 0) return -1;
            shell_version = shell_version_buf;
            return 0;

        case 0x06: { /* SYS_INFO_CPU */
            if (!sys_range_mapped(ebx, sizeof(sys_cpuinfo_t))) return -1;
            sys_cpuinfo_t *out = (sys_cpuinfo_t*)ebx;

            for (int i = 0; i < (int)sizeof(out->vendor); i++) out->vendor[i] = '\0';
            for (int i = 0; i < (int)sizeof(out->model); i++) out->model[i] = '\0';
            out->mhz = 0;

            uint32_t a, b, c, d;
            char vbuf[13];
            __asm__ volatile("cpuid" : "=a"(a), "=b"(b), "=c"(c), "=d"(d) : "a"(0));
            ((uint32_t*)vbuf)[0] = b;
            ((uint32_t*)vbuf)[1] = d;
            ((uint32_t*)vbuf)[2] = c;
            vbuf[12] = '\0';
            for (int i = 0; i < 13; i++) out->vendor[i] = vbuf[i];

            __asm__ volatile("cpuid" : "=a"(a) : "a"(0x80000000) : "ebx","ecx","edx");
            if (a >= 0x80000004) {
                uint32_t regs[12];
                __asm__ volatile("cpuid" : "=a"(regs[0]), "=b"(regs[1]), "=c"(regs[2]), "=d"(regs[3]) : "a"(0x80000002));
                __asm__ volatile("cpuid" : "=a"(regs[4]), "=b"(regs[5]), "=c"(regs[6]), "=d"(regs[7]) : "a"(0x80000003));
                __asm__ volatile("cpuid" : "=a"(regs[8]), "=b"(regs[9]), "=c"(regs[10]), "=d"(regs[11]) : "a"(0x80000004));

                char brand[49];
                for (int ii = 0; ii < 12; ii++) {
                    uint32_t v = regs[ii];
                    brand[ii*4 + 0] = (char)(v & 0xFF);
                    brand[ii*4 + 1] = (char)((v >> 8) & 0xFF);
                    brand[ii*4 + 2] = (char)((v >> 16) & 0xFF);
                    brand[ii*4 + 3] = (char)((v >> 24) & 0xFF);
                }
                brand[48] = '\0';

                /* Brand strings often carry a usable MHz/GHz token on older CPUs. */
                int freq_idx = -1;
                uint32_t brand_mhz = 0;
                for (int p = 0; brand[p]; p++) {
                    if (brand[p] >= '0' && brand[p] <= '9') {
                        uint32_t intval = 0;
                        int q = p;
                        while (brand[q] >= '0' && brand[q] <= '9') { intval = intval * 10 + (brand[q] - '0'); q++; }

                        uint32_t frac = 0; int frac_d = 0;
                        if (brand[q] == '.') {
                            q++;
                            while (brand[q] >= '0' && brand[q] <= '9' && frac_d < 3) { frac = frac * 10 + (brand[q] - '0'); frac_d++; q++; }
                        }

                        while (brand[q] == ' ' || brand[q] == '\t') q++;

                        if (brand[q] == 'M' || brand[q] == 'm') {
                            brand_mhz = intval;
                            freq_idx = p;
                            break;
                        } else if (brand[q] == 'G' || brand[q] == 'g') {
                            uint32_t add = intval * 1000u;
                            if (frac_d > 0) {
                                uint32_t scale = 1000u;
                                for (int s = 0; s < frac_d; s++) scale /= 10u;
                                add += frac * scale;
                            }
                            brand_mhz = add;
                            freq_idx = p;
                            break;
                        }
                    }
                }

                /* Trim vendor and trademark clutter from the CPUID brand string. */
                char vendor_norm[16];
                int vnlen = 0;
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
                int skip = 1;
                if (vnlen > 0) {
                    skip = 1;
                    for (int _k = 0; _k < vnlen; _k++) { if (brand[_k] != vendor_norm[_k]) { skip = 0; break; } }
                    if (skip) {
                        idx = vnlen;
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

                int stop_at = -1;
                if (freq_idx >= 0) stop_at = freq_idx;
                for (int p = idx; brand[p]; p++) {
                    if ((brand[p] == 'C' || brand[p] == 'c') && (brand[p+1] == 'P' || brand[p+1] == 'p') && (brand[p+2] == 'U' || brand[p+2] == 'u')) {
                        stop_at = (stop_at < 0) ? p : (stop_at < p ? stop_at : p);
                        break;
                    }
                }
                if (stop_at < 0) stop_at = 48;

                for (int p = idx; p < stop_at && ti < (int)sizeof(tmp)-1; p++) {
                    if (brand[p] == '(') {
                        while (p < stop_at && brand[p] && brand[p] != ')') p++;
                        continue;
                    }
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
                    int ci = 0; for (; ci < (int)sizeof(out->model)-1 && out->vendor[ci]; ci++) out->model[ci] = out->vendor[ci]; out->model[ci] = '\0';
                }

                if (brand_mhz > 0) {
                    out->mhz = brand_mhz;
                    return 0;
                }
            } else {
                /* Some Pentium-era BIOSes have no extended CPUID brand string. */
                const char *smb_ent = NULL;
                for (uintptr_t addr = 0xF0000; addr < 0x100000; addr += 16) {
                    const char *p = (const char*)(uintptr_t)addr;
                    if (p[0] == '_' && p[1] == 'S' && p[2] == 'M' && p[3] == '_') { smb_ent = p; break; }
                }

                int smbios_used = 0;
                if (smb_ent) {
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
                                        const char *st = (const char*)siter;
                                        for (int i = 0; st[i]; i++) {
                                            if ((st[i] == 'C' || st[i] == 'c') && (st[i+1] == 'e' || st[i+1] == 'E')) {
                                                if ((st[i+0] == 'C' || st[i+0]=='c') && (st[i+1]=='e'||st[i+1]=='E') && (st[i+2]=='l'||st[i+2]=='L')) {
                                                    int ci = 0; int si = i;
                                                    while (st[si] && ci < (int)sizeof(out->model)-1 && st[si] != '\r' && st[si] != '\n') out->model[ci++] = st[si++];
                                                    out->model[ci] = '\0';
                                                    for (int k = 0; st[k]; k++) {
                                                        if (st[k] >= '0' && st[k] <= '9') {
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
                                            if ((st[i] == 'P' || st[i]=='p') && (st[i+1]=='e' || st[i+1]=='E') && (st[i+2]=='n' || st[i+2]=='N')) {
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

                                        for (int k = 0; st[k]; k++) {
                                            if (st[k] >= '0' && st[k] <= '9') {
                                                uint32_t intval = 0; int q = k; while (st[q] >= '0' && st[q] <= '9') { intval = intval*10 + (st[q]-'0'); q++; }
                                                while (st[q] == ' ' || st[q] == '\t') q++;
                                                if (st[q] == 'M' || st[q] == 'm') { if (out->mhz == 0) out->mhz = intval; }
                                                else if (st[q] == 'G' || st[q] == 'g') { if (out->mhz == 0) out->mhz = intval * 1000u; }
                                                break;
                                            }
                                        }

                                        while (*siter) siter++;
                                        siter++;
                                        if (found_model) break;
                                    }

                                    if (found_model) {
                                        smbios_used = 1;
                                        break;
                                    }
                                }

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

                    /* A small map is enough for the old CPUs osLET commonly runs on. */
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

            if (out->mhz == 0) {
                /* Newer Intel CPUs may report base MHz directly in CPUID.0x16. */
                uint32_t a16=0, b16=0, c16=0, d16=0;
                __asm__ volatile("cpuid" : "=a"(a16), "=b"(b16), "=c"(c16), "=d"(d16) : "a"(0x16));
                if (a16 && a16 < 0x80000) {
                    out->mhz = a16;
                    return 0;
                }

                /* NOTE: CPUID.0x15 handling omitted (avoids 64-bit div helpers). */

                const uint32_t sample_ticks = 20; /* about 200ms at the default PIT rate */

                unsigned int lo1, hi1, lo2, hi2;
                uint64_t t1, t2, cycles64;

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

        case 0x07:
            textmode_requested = 1;
            return 0;

        case 0x08: {
            int pending = textmode_requested;
            textmode_requested = 0;
            return pending;
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
    char path[FAT32_MAX_PATH];

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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            return gfx_load_bmp_4bit(path, (int)ecx, (int)edx);

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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            int x = (int)(ecx >> 16);
            int y = (int)(ecx & 0xFFFF);
            return gfx_load_bmp_4bit_ex(path, x, y, (int)edx);
        }

        case 0x0D: { /* Cache BMP to memory */
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            if (!sys_range_mapped(ecx, sizeof(cached_bmp_t))) return (uint32_t)-1;
            cached_bmp_t *out = (cached_bmp_t*)ecx;
            int w, h;
            uint8_t *data = gfx_load_bmp_to_buffer(path, &w, &h);
            if (!data) return (uint32_t)-1;
            out->data = data;
            out->width = w;
            out->height = h;
            return 0;
        }

        case 0x0E: { /* Draw cached BMP */
            if (!sys_range_mapped(ebx, sizeof(cached_bmp_t))) return (uint32_t)-1;
            cached_bmp_t bmp;
            memcpy_s(&bmp, (const void*)ebx, sizeof(bmp));
            if (!bmp.data) return (uint32_t)-1;
            int x = (int)(ecx >> 16);
            int y = (int)(ecx & 0xFFFF);
            gfx_draw_cached_bmp_ex(bmp.data, bmp.width, bmp.height, x, y, (int)edx);
            return 0;
        }

        case 0x0F: { /* Free cached BMP */
            if (!sys_range_mapped(ebx, sizeof(cached_bmp_t))) return (uint32_t)-1;
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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            int dest_x = (int)(ecx >> 16);
            int dest_y = (int)(ecx & 0xFFFF);
            int dest_w = (int)(edx >> 16);
            int dest_h = (int)(edx & 0xFFFF);

            int src_w, src_h;
            uint8_t *bmp = gfx_load_bmp_to_buffer(path, &src_w, &src_h);
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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            if (!sys_range_mapped(edx, sizeof(cached_bmp_t))) return (uint32_t)-1;
            int target_w = (int)(ecx >> 16);
            int target_h = (int)(ecx & 0xFFFF);
            cached_bmp_t *out = (cached_bmp_t*)edx;

            int src_w, src_h;
            uint8_t *bmp = gfx_load_bmp_to_buffer(path, &src_w, &src_h);
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

            if (!sys_range_mapped(ebx, sizeof(draw_region_t))) return (uint32_t)-1;
            draw_region_t r;
            memcpy_s(&r, (const void*)ebx, sizeof(r));
            if (!r.bmp) return (uint32_t)-1;

            if (!sys_range_mapped((uint32_t)(uintptr_t)r.bmp, sizeof(cached_bmp_t))) return (uint32_t)-1;
            cached_bmp_t bmp;
            memcpy_s(&bmp, r.bmp, sizeof(bmp));
            if (!bmp.data) return (uint32_t)-1;

            int sx = r.src_x;
            int sy = r.src_y;
            int sw = r.src_w;
            int sh = r.src_h;

            /* Clip source to bitmap bounds */
            if (sx < 0) { sw += sx; sx = 0; }
            if (sy < 0) { sh += sy; sy = 0; }
            if (sx >= bmp.width || sy >= bmp.height) return 0;
            if (sx + sw > bmp.width) sw = bmp.width - sx;
            if (sy + sh > bmp.height) sh = bmp.height - sy;
            if (sw <= 0 || sh <= 0) return 0;

            gfx_draw_cached_bmp_region((uint8_t*)bmp.data, bmp.width, bmp.height,
                                       r.dest_x, r.dest_y,
                                       sx, sy, sw, sh, r.transparent);
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
            if (!sys_range_mapped(ebx, sizeof(int)) ||
                !sys_range_mapped(ecx, sizeof(int)) ||
                !sys_range_mapped(edx, sizeof(uint8_t))) return (uint32_t)-1;
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

static uint32_t handle_sound(uint32_t al, uint32_t ebx, uint32_t ecx) {
    char path[FAT32_MAX_PATH];

    switch (al) {
        case 0x00:
            return (uint32_t)sb16_detected();

        case 0x01:
            // Removed
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
            if (sys_copy_string(path, ebx, sizeof(path)) != 0) return (uint32_t)-1;
            return (uint32_t)sound_play_wav(path);

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

            /* The keyboard controller reset works on many old PC targets. */
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
        case 0x0E: return handle_sound(al, ebx, ecx);
        case 0x0F: return handle_vconsole(al, ebx, ecx, edx);
        case 0x0D: {
            /* Input namespace - subcodes in AL */
            switch (al) {
                case 0x00: /* SYS_GET_KEY_NONBLOCK */
                    if (!current_task_owns_focused_window())
                        return 0;
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

void syscall_init(void) {
    fd_init();
}
