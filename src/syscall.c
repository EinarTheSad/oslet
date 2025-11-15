#include "syscall.h"
#include "task.h"
#include "console.h"
#include "timer.h"
#include "drivers/fat32.h"
#include "exec.h"
#include <stddef.h>

extern void vga_set_color(uint8_t background, uint8_t foreground);

static void memcpy_safe(void *dst, const void *src, size_t n) {
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
}

/* Validate user pointer - must be in userspace range */
static int validate_ptr(uint32_t ptr) {
    task_t *current = task_get_current();
    if (!current) return 0;
    
    /* Kernel tasks can access all memory */
    if (!current->user_mode) {
        return (ptr >= 0x200000 && ptr < 0xC0000000);
    }
    
    /* User tasks: only user memory */
    return (ptr >= EXEC_LOAD_ADDR && ptr < 0xC0000000);
}

static uint32_t handle_console(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: /* Write string */
            if (!validate_ptr(ebx)) return -1;
            printf("%s", (const char*)ebx);
            return 0;
            
        case 0x02: /* Set color */
            vga_set_color((uint8_t)ebx, (uint8_t)ecx);
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
            if (!validate_ptr(ebx) || !validate_ptr(ecx)) return 0;
            fat32_file_t *f = fat32_open((const char*)ebx, (const char*)ecx);
            return (uint32_t)f;
        }
            
        case 0x01: /* Close */
            if (ebx) fat32_close((fat32_file_t*)ebx);
            return 0;
            
        case 0x02: /* Read */
            if (!validate_ptr(ecx)) return -1;
            return fat32_read((fat32_file_t*)ebx, (void*)ecx, edx);
            
        case 0x03: /* Write */
            if (!validate_ptr(ecx)) return -1;
            return fat32_write((fat32_file_t*)ebx, (const void*)ecx, edx);
            
        default:
            return -1;
    }
}

static uint32_t handle_dir(uint32_t al, uint32_t ebx, uint32_t ecx, uint32_t edx) {
    (void)edx;
    
    switch (al) {
        case 0x00: /* Chdir */
            if (!validate_ptr(ebx)) return -1;
            return fat32_chdir((const char*)ebx);
            
        case 0x01: /* Getcwd */
            if (!validate_ptr(ebx)) return 0;
            return (uint32_t)fat32_getcwd((char*)ebx, ecx);
            
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
            memcpy_safe(msg->data, (const void*)ecx, edx);
            
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
            memcpy_safe((message_t*)ebx, msg, sizeof(message_t));
            
            q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
            q->count--;
            return 0;
        }
            
        default:
            return -1;
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
        
        default:
            printf("Unknown syscall: AH=%02Xh AL=%02Xh\n", ah, al);
            return (uint32_t)-1;
    }
}

void syscall_init(void) {
    /* Syscall interface ready */
}