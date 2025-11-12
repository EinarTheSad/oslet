#pragma once
#include <stdint.h>
#include <stddef.h>

#define SYS_EXIT      1
#define SYS_WRITE     2
#define SYS_SLEEP     3
#define SYS_YIELD     4
#define SYS_SEND_MSG  5
#define SYS_RECV_MSG  6
#define SYS_GETPID    7
#define SYS_SPAWN     8
#define SYS_SETCOLOR  9

#define MSG_QUEUE_SIZE 16
#define MSG_MAX_SIZE   128

typedef struct {
    uint32_t from_tid;
    uint32_t to_tid;
    uint32_t size;
    char data[MSG_MAX_SIZE];
} message_t;

typedef struct {
    message_t msgs[MSG_QUEUE_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
} msg_queue_t;

void syscall_init(void);
uint32_t syscall_handler(uint32_t syscall_num, uint32_t arg1, uint32_t arg2, uint32_t arg3);

/* User-space wrappers */
static inline void sys_exit(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_EXIT));
}

static inline void sys_write(const char *str) {
    __asm__ volatile("int $0x80" :: "a"(SYS_WRITE), "b"(str));
}

static inline void sys_sleep(uint32_t ms) {
    __asm__ volatile("int $0x80" :: "a"(SYS_SLEEP), "b"(ms));
}

static inline void sys_yield(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_YIELD));
}

static inline int sys_send_msg(uint32_t to_tid, const void *data, uint32_t size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_SEND_MSG), "b"(to_tid), "c"(data), "d"(size));
    return ret;
}

static inline int sys_recv_msg(message_t *msg) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_RECV_MSG), "b"(msg));
    return ret;
}

static inline uint32_t sys_getpid(void) {
    uint32_t tid;
    __asm__ volatile("int $0x80" : "=a"(tid) : "a"(SYS_GETPID));
    return tid;
}

static inline int sys_setcolor(uint8_t background, uint8_t foreground) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_SETCOLOR), "b"(background), "c"(foreground));
    return ret;
}