#include "syscall.h"
#include "task.h"
#include "console.h"
#include "timer.h"
#include "heap.h"
#include <stddef.h>

static void memcpy_safe(void *dst, const void *src, size_t n) {
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
}

void syscall_init(void) {
    /* printf("Syscall interface initialized\n"); */
}

static int sys_send_msg_impl(uint32_t to_tid, const void *data, uint32_t size) {
    if (size > MSG_MAX_SIZE) return -1;
    if (!data) return -2;
    
    task_t *sender = task_get_current();
    if (!sender) return -3;
    
    task_t *receiver = task_find_by_tid(to_tid);
    if (!receiver) return -4;
    
    msg_queue_t *q = &receiver->msg_queue;
    
    if (q->count >= MSG_QUEUE_SIZE) return -5; /* Queue full */
    
    message_t *msg = &q->msgs[q->head];
    msg->from_tid = sender->tid;
    msg->to_tid = to_tid;
    msg->size = size;
    memcpy_safe(msg->data, data, size);
    
    q->head = (q->head + 1) % MSG_QUEUE_SIZE;
    q->count++;
    
    /* Wake up receiver if sleeping */
    if (receiver->state == TASK_BLOCKED) {
        receiver->state = TASK_READY;
    }
    
    return 0;
}

static int sys_recv_msg_impl(message_t *out_msg) {
    if (!out_msg) return -1;
    
    task_t *current = task_get_current();
    if (!current) return -2;
    
    msg_queue_t *q = &current->msg_queue;
    
    if (q->count == 0) {
        /* No messages - block until one arrives */
        current->state = TASK_BLOCKED;
        task_yield();
        /* When we wake up, check again */
        if (q->count == 0) return -3;
    }
    
    message_t *msg = &q->msgs[q->tail];
    memcpy_safe(out_msg, msg, sizeof(message_t));
    
    q->tail = (q->tail + 1) % MSG_QUEUE_SIZE;
    q->count--;
    
    return 0;
}

uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3) {
    switch (syscall_num) {
        case SYS_EXIT:
            task_exit();
            return 0;
            
        case SYS_WRITE:
            if (arg1) printf("%s", (const char*)arg1);
            return 0;
            
        case SYS_SLEEP:
            task_sleep(arg1);
            return 0;
            
        case SYS_YIELD:
            task_yield();
            return 0;
            
        case SYS_SEND_MSG:
            return sys_send_msg_impl(arg1, (const void*)arg2, arg3);
            
        case SYS_RECV_MSG:
            return sys_recv_msg_impl((message_t*)arg1);
            
        case SYS_GETPID: {
            task_t *current = task_get_current();
            return current ? current->tid : 0;
        }

        case SYS_SETCOLOR:
            sys_setcolor((uint8_t)arg1, (uint8_t)arg2);
            
        default:
            printf("Unknown syscall: %u (%X)\n", syscall_num, syscall_num);
            return (uint32_t)-1;
    }
}