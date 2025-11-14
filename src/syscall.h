#pragma once
#include <stdint.h>
#include <stddef.h>

/* AH = 01h - Console I/O */
#define SYS_CONSOLE_OUT     0x0100  /* ah=01h, al=00h - Write string */
#define SYS_CONSOLE_IN      0x0101  /* ah=01h, al=01h - Read line */
#define SYS_CONSOLE_SETCOL  0x0102  /* ah=01h, al=02h - Set color */

/* AH = 02h - Process Control */
#define SYS_PROC_EXIT       0x0200  /* ah=02h, al=00h - Exit process */
#define SYS_PROC_EXEC       0x0201  /* ah=02h, al=01h - Execute binary */
#define SYS_PROC_GETPID     0x0202  /* ah=02h, al=02h - Get PID */
#define SYS_PROC_SLEEP      0x0203  /* ah=02h, al=03h - Sleep ms */
#define SYS_PROC_YIELD      0x0204  /* ah=02h, al=04h - Yield CPU */

/* AH = 03h - File Operations */
#define SYS_FILE_OPEN       0x0300  /* ah=03h, al=00h - Open file */
#define SYS_FILE_CLOSE      0x0301  /* ah=03h, al=01h - Close file */
#define SYS_FILE_READ       0x0302  /* ah=03h, al=02h - Read file */
#define SYS_FILE_WRITE      0x0303  /* ah=03h, al=03h - Write file */
#define SYS_FILE_SEEK       0x0304  /* ah=03h, al=04h - Seek position */
#define SYS_FILE_DELETE     0x0305  /* ah=03h, al=05h - Delete file */

/* AH = 04h - Directory Operations */
#define SYS_DIR_CHDIR       0x0400  /* ah=04h, al=00h - Change dir */
#define SYS_DIR_GETCWD      0x0401  /* ah=04h, al=01h - Get current dir */
#define SYS_DIR_MKDIR       0x0402  /* ah=04h, al=02h - Make directory */
#define SYS_DIR_RMDIR       0x0403  /* ah=04h, al=03h - Remove directory */
#define SYS_DIR_LIST        0x0404  /* ah=04h, al=04h - List directory */

/* AH = 05h - IPC (Inter-Process Communication) */
#define SYS_IPC_SEND        0x0500  /* ah=05h, al=00h - Send message */
#define SYS_IPC_RECV        0x0501  /* ah=05h, al=01h - Receive message */

/* AH = 06h - Memory Management */
#define SYS_MEM_ALLOC       0x0600  /* ah=06h, al=00h - Allocate memory */
#define SYS_MEM_FREE        0x0601  /* ah=06h, al=01h - Free memory */

/* AH = 07h - Time/Date */
#define SYS_TIME_GET        0x0700  /* ah=07h, al=00h - Get time */
#define SYS_TIME_UPTIME     0x0701  /* ah=07h, al=01h - System uptime */

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
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

static inline void sys_write(const char *str) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_OUT), "b"(str));
}

static inline void sys_setcolor(uint8_t bg, uint8_t fg) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_SETCOL), "b"(bg), "c"(fg));
}

static inline void sys_exit(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_PROC_EXIT));
}

static inline int sys_exec(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_PROC_EXEC), "b"(path));
    return ret;
}

static inline uint32_t sys_getpid(void) {
    uint32_t pid;
    __asm__ volatile("int $0x80" : "=a"(pid) : "a"(SYS_PROC_GETPID));
    return pid;
}

static inline void sys_sleep(uint32_t ms) {
    __asm__ volatile("int $0x80" :: "a"(SYS_PROC_SLEEP), "b"(ms));
}

static inline void sys_yield(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_PROC_YIELD));
}

static inline int sys_open(const char *path, const char *mode) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_FILE_OPEN), "b"(path), "c"(mode));
    return ret;
}

static inline int sys_close(int fd) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_FILE_CLOSE), "b"(fd));
    return ret;
}

static inline int sys_read(int fd, void *buf, uint32_t size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_FILE_READ), "b"(fd), "c"(buf), "d"(size));
    return ret;
}

static inline int sys_write_file(int fd, const void *buf, uint32_t size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_FILE_WRITE), "b"(fd), "c"(buf), "d"(size));
    return ret;
}

static inline int sys_chdir(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_DIR_CHDIR), "b"(path));
    return ret;
}

static inline char* sys_getcwd(char *buf, uint32_t size) {
    char *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_DIR_GETCWD), "b"(buf), "c"(size));
    return ret;
}

static inline int sys_send_msg(uint32_t to_tid, const void *data, uint32_t size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_IPC_SEND), "b"(to_tid), "c"(data), "d"(size));
    return ret;
}

static inline int sys_recv_msg(message_t *msg) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_IPC_RECV), "b"(msg));
    return ret;
}