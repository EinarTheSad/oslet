#pragma once
#include <stdint.h>
#include <stddef.h>

/* AH = 01h - Console I/O */
#define SYS_CONSOLE_OUT     0x0100
#define SYS_CONSOLE_IN      0x0101
#define SYS_CONSOLE_SETCOL  0x0102
#define SYS_CONSOLE_GETCHAR 0x0103
#define SYS_CONSOLE_CLEAR   0x0104

/* AH = 02h - Process Control */
#define SYS_PROC_EXIT       0x0200
#define SYS_PROC_EXEC       0x0201
#define SYS_PROC_GETPID     0x0202
#define SYS_PROC_SLEEP      0x0203
#define SYS_PROC_YIELD      0x0204

/* AH = 03h - File Operations */
#define SYS_FILE_OPEN       0x0300
#define SYS_FILE_CLOSE      0x0301
#define SYS_FILE_READ       0x0302
#define SYS_FILE_WRITE      0x0303
#define SYS_FILE_SEEK       0x0304
#define SYS_FILE_DELETE     0x0305

/* AH = 04h - Directory Operations */
#define SYS_DIR_CHDIR       0x0400
#define SYS_DIR_GETCWD      0x0401
#define SYS_DIR_MKDIR       0x0402
#define SYS_DIR_RMDIR       0x0403
#define SYS_DIR_LIST        0x0404

/* AH = 05h - IPC */
#define SYS_IPC_SEND        0x0500
#define SYS_IPC_RECV        0x0501

/* AH = 06h - Memory Management */
#define SYS_MEM_ALLOC       0x0600
#define SYS_MEM_FREE        0x0601

/* AH = 07h - Time/Date */
#define SYS_TIME_GET        0x0700
#define SYS_TIME_UPTIME     0x0701

/* AH = 08h - System Info */
#define SYS_INFO_MEM        0x0800
#define SYS_INFO_TASKS      0x0801
#define SYS_INFO_VERSION    0x0802

/* AH = 09h - Graphics */
#define SYS_GFX_ENTER       0x0900
#define SYS_GFX_EXIT        0x0901
#define SYS_GFX_CLEAR       0x0902
#define SYS_GFX_SWAP        0x0903
#define SYS_GFX_PUTPIXEL    0x0904
#define SYS_GFX_LINE        0x0905
#define SYS_GFX_RECT        0x0906
#define SYS_GFX_FILLRECT    0x0907
#define SYS_GFX_CIRCLE      0x0908
#define SYS_GFX_PRINT       0x0909

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

typedef struct {
    char name[13];
    uint32_t size;
    uint32_t first_cluster;
    uint8_t is_directory;
    uint8_t attr;
} sys_dirent_t;

typedef struct {
    uint32_t total_kb;
    uint32_t free_kb;
    uint32_t used_kb;
} sys_meminfo_t;

typedef struct {
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} sys_time_t;

typedef struct {
    uint32_t tid;
    char name[32];
    uint8_t state;
    uint8_t priority;
    uint8_t user_mode;
} sys_taskinfo_t;

typedef struct {
    int x0, y0, x1, y1;
} gfx_line_params_t;

typedef struct {
    int x, y, w, h;
} gfx_rect_params_t;


void syscall_init(void);
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);

/* Inline syscall wrappers */
static inline void sys_write(const char *str) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_OUT), "b"(str));
}

static inline int sys_getchar(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_CONSOLE_GETCHAR));
    return ret;
}

static inline int sys_readline(char *buf, uint32_t size) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_CONSOLE_IN), "b"(buf), "c"(size));
    return ret;
}

static inline void sys_clear(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_CLEAR));
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

static inline int sys_unlink(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_FILE_DELETE), "b"(path));
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

static inline int sys_mkdir(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_DIR_MKDIR), "b"(path));
    return ret;
}

static inline int sys_rmdir(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_DIR_RMDIR), "b"(path));
    return ret;
}

static inline int sys_readdir(const char *path, sys_dirent_t *entries, int max) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_DIR_LIST), "b"(path), "c"(entries), "d"(max));
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

static inline int sys_get_meminfo(sys_meminfo_t *info) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_INFO_MEM), "b"(info));
    return ret;
}

static inline int sys_get_tasks(sys_taskinfo_t *tasks, int max) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_INFO_TASKS), "b"(tasks), "c"(max));
    return ret;
}

static inline int sys_version(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_INFO_VERSION));
    return ret;
}

static inline uint32_t sys_uptime(void) {
    uint32_t ticks;
    __asm__ volatile("int $0x80" : "=a"(ticks) : "a"(SYS_TIME_UPTIME));
    return ticks;
}

static inline void sys_gfx_enter(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_ENTER));
}

static inline void sys_gfx_exit(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_EXIT));
}

static inline void sys_gfx_clear(uint8_t color) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_CLEAR), "b"(color));
}

static inline void sys_gfx_swap(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_SWAP));
}

static inline void sys_gfx_putpixel(int x, int y, uint8_t color) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_PUTPIXEL), "b"(x), "c"(y), "d"(color));
}

static inline void sys_gfx_line(int x0, int y0, int x1, int y1, uint8_t color) {
    gfx_line_params_t params = {x0, y0, x1, y1};
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_LINE), "b"(&params), "c"(color));
}

static inline void sys_gfx_rect(int x, int y, int w, int h, uint8_t color) {
    gfx_rect_params_t params = {x, y, w, h};
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_RECT), "b"(&params), "c"(color));
}

static inline void sys_gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
    gfx_rect_params_t params = {x, y, w, h};
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_FILLRECT), "b"(&params), "c"(color));
}

static inline void sys_gfx_circle(int cx, int cy, int r, uint8_t color) {
    uint32_t packed = ((uint32_t)r << 8) | color;
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_CIRCLE), "b"(cx), "c"(cy), "d"(packed));
}

static inline void sys_gfx_print(int x, int y, const char *text, uint8_t fg) {
    uint32_t packed = ((uint32_t)y << 16) | ((uint32_t)fg << 8);
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_PRINT), "b"(x), "c"(packed), "d"(text));
}

static inline int sys_get_time(sys_time_t *time) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_TIME_GET), "b"(time));
    return ret;
}