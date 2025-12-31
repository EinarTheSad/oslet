#pragma once
#include <stdint.h>
#include <stddef.h>
#include "rtc.h"
#include "win/window.h"

typedef rtc_time_t sys_time_t;

/* AH = 01h - Console I/O */
#define SYS_CONSOLE_OUT     0x0100
#define SYS_CONSOLE_IN      0x0101
#define SYS_CONSOLE_SETCOL  0x0102
#define SYS_CONSOLE_GETCHAR 0x0103
#define SYS_CONSOLE_CLEAR   0x0104
#define SYS_CONSOLE_SETCUR  0x0105
#define SYS_CONSOLE_GETCUR  0x0106

/* AH = 02h - Process Control */
#define SYS_PROC_EXIT       0x0200
#define SYS_PROC_EXEC       0x0201
#define SYS_PROC_GETPID     0x0202
#define SYS_PROC_SLEEP      0x0203
#define SYS_PROC_YIELD      0x0204
#define SYS_PROC_SPAWN      0x0205

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
#define SYS_INFO_HEAP       0x0803
#define SYS_INFO_SHELL      0x0804
#define SYS_INFO_SET_SHELL  0x0805

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

#define SYS_GFX_LOAD_BMP    0x090A
#define SYS_GFX_FILLRECT_GRADIENT 0x090B

/* AH = 0Ah - Mouse */
#define SYS_MOUSE_GET_STATE  0x0A00
#define SYS_MOUSE_DRAW_CURSOR 0x0A01

/* AH = 0Bh - Windows */
#define SYS_WIN_MSGBOX          0x0B00
#define SYS_WIN_CREATE_FORM     0x0B05
#define SYS_WIN_PUMP_EVENTS     0x0B07
#define SYS_WIN_ADD_CONTROL     0x0B08
#define SYS_WIN_DRAW            0x0B09
#define SYS_WIN_DESTROY_FORM    0x0B0A
#define SYS_WIN_SET_ICON        0x0B0B

#define MSG_QUEUE_SIZE 16
#define MSG_MAX_SIZE   128

/* Form controls */
#define CTRL_BUTTON 1
#define CTRL_LABEL 2
#define CTRL_PICTUREBOX 3

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_ITALIC 2
#define FONT_BOLD_ITALIC 3

typedef struct {
    uint8_t type;
    uint16_t x, y, w, h;
    uint8_t fg, bg;
    char text[256];
    uint16_t id;
    uint8_t font_type;
    uint8_t font_size;
    uint8_t border;
    uint8_t border_color;
    uint8_t *cached_bitmap;
    uint16_t bmp_width;
    uint16_t bmp_height;
    uint8_t pressed;
} gui_control_t;

typedef struct {
    window_t win;
    gui_control_t *controls;
    uint8_t ctrl_count;
    int16_t clicked_id;
    uint8_t last_mouse_buttons;
    int16_t press_control_id;
    uint8_t dragging;
    int16_t drag_start_x;
    int16_t drag_start_y;
} gui_form_t;

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
    uint32_t total_kb;
    uint32_t used_bytes;
    uint32_t free_bytes;
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t total_allocated;
    uint32_t total_freed;
} sys_heapinfo_t;

typedef struct {
    uint32_t tid;
    char name[32];
    uint8_t state;
    uint8_t priority;
} sys_taskinfo_t;

typedef struct {
    int x0, y0, x1, y1;
} gfx_line_params_t;

typedef struct {
    int x, y, w, h;
} gfx_rect_params_t;


void syscall_init(void);
uint32_t syscall_handler(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);
void fd_cleanup_task(uint32_t tid);

/* Inline syscall wrappers */
static inline void sys_write(const char *str) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_OUT), "b"(str) : "memory");
}

static inline int sys_getchar(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_CONSOLE_GETCHAR));
    return (unsigned char)ret; /* Force unsigned to prevent sign extension */
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

static inline void sys_setcur(int x, int y) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_SETCUR), "b"(x), "c"(y) : "memory");
}

static inline void sys_getcur(int *x, int *y) {
    __asm__ volatile("int $0x80" :: "a"(SYS_CONSOLE_GETCUR), "b"(x), "c"(y) : "memory");
}

static inline void sys_exit(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_PROC_EXIT));
}

static inline int sys_exec(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_PROC_EXEC), "b"(path));
    return ret;
}

static inline int sys_spawn(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_PROC_SPAWN), "b"(path));
    return ret;
}

static inline void* sys_malloc(size_t size) {
    void* ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_MEM_ALLOC), "b"(size));
    return ret;
}

static inline void sys_free(void* ptr) {
    __asm__ volatile("int $0x80" :: "a"(SYS_MEM_FREE), "b"(ptr));
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

static inline int sys_get_heapinfo(sys_heapinfo_t *info) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_INFO_HEAP), "b"(info));
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

static inline int sys_info_shell(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_INFO_SHELL));
    return ret;
}

static inline int sys_shell_set(const char *version) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_INFO_SET_SHELL), "b"(version));
    return ret;
}

static inline uint32_t sys_uptime(void) {
    uint32_t ticks;
    __asm__ volatile("int $0x80" : "=a"(ticks) : "a"(SYS_TIME_UPTIME));
    return ticks;
}

static inline void sys_gfx_enter(void) {
    register int dummy_eax __asm__("eax") = SYS_GFX_ENTER;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

static inline void sys_gfx_exit(void) {
    register int dummy_eax __asm__("eax") = SYS_GFX_EXIT;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

static inline void sys_gfx_clear(uint8_t color) {
    register int dummy_eax __asm__("eax") = SYS_GFX_CLEAR;
    register uint8_t dummy_ebx __asm__("ebx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx) :: "memory");
}

static inline void sys_gfx_swap(void) {
    register int dummy_eax __asm__("eax") = SYS_GFX_SWAP;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

static inline void sys_gfx_putpixel(int x, int y, uint8_t color) {
    register int dummy_eax __asm__("eax") = SYS_GFX_PUTPIXEL;
    register int dummy_ebx __asm__("ebx") = x;
    register int dummy_ecx __asm__("ecx") = y;
    register uint8_t dummy_edx __asm__("edx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_line(int x0, int y0, int x1, int y1, uint8_t color) {
    uint32_t start = ((uint32_t)x0 << 16) | (y0 & 0xFFFF);
    uint32_t end = ((uint32_t)x1 << 16) | (y1 & 0xFFFF);
    register int dummy_eax __asm__("eax") = SYS_GFX_LINE;
    register uint32_t dummy_ebx __asm__("ebx") = start;
    register uint32_t dummy_ecx __asm__("ecx") = end;
    register uint8_t dummy_edx __asm__("edx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_rect(int x, int y, int w, int h, uint8_t color) {
    uint32_t pos = ((uint32_t)x << 16) | (y & 0xFFFF);
    uint32_t size = ((uint32_t)w << 16) | (h & 0xFFFF);
    register int dummy_eax __asm__("eax") = SYS_GFX_RECT;
    register uint32_t dummy_ebx __asm__("ebx") = pos;
    register uint32_t dummy_ecx __asm__("ecx") = size;
    register uint8_t dummy_edx __asm__("edx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
    uint32_t pos = ((uint32_t)x << 16) | (y & 0xFFFF);
    uint32_t size = ((uint32_t)w << 16) | (h & 0xFFFF);
    register int dummy_eax __asm__("eax") = SYS_GFX_FILLRECT;
    register uint32_t dummy_ebx __asm__("ebx") = pos;
    register uint32_t dummy_ecx __asm__("ecx") = size;
    register uint8_t dummy_edx __asm__("edx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_circle(int cx, int cy, int r, uint8_t color) {
    uint32_t packed = ((uint32_t)r << 8) | color;
    register int dummy_eax __asm__("eax") = SYS_GFX_CIRCLE;
    register int dummy_ebx __asm__("ebx") = cx;
    register int dummy_ecx __asm__("ecx") = cy;
    register uint32_t dummy_edx __asm__("edx") = packed;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_fillrect_gradient(int x, int y, int w, int h,
                                             uint8_t c_start, uint8_t c_end,
                                             int orientation) {
    uint32_t coords = ((uint32_t)w << 16) | (h & 0xFFFF);
    uint32_t colors = ((uint32_t)c_start << 16) | ((uint32_t)c_end << 8) | orientation;
    uint32_t pos = ((uint32_t)x << 16) | (y & 0xFFFF);
    register int dummy_eax __asm__("eax") = SYS_GFX_FILLRECT_GRADIENT;
    register uint32_t dummy_ebx __asm__("ebx") = pos;
    register uint32_t dummy_ecx __asm__("ecx") = coords;
    register uint32_t dummy_edx __asm__("edx") = colors;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline int sys_get_time(sys_time_t *time) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_TIME_GET), "b"(time));
    return ret;
}

static inline int sys_gfx_load_bmp(const char *path, int x, int y) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_LOAD_BMP), "b"(path), "c"(x), "d"(y));
    return ret;
}

static inline void sys_get_mouse_state(int *x, int *y, unsigned char *buttons) {
    register int dummy_eax __asm__("eax") = SYS_MOUSE_GET_STATE;
    register int *dummy_ebx __asm__("ebx") = x;
    register int *dummy_ecx __asm__("ecx") = y;
    register unsigned char *dummy_edx __asm__("edx") = buttons;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
    /* GCC's inline assembly constraints weren't properly telling the compiler that syscalls modify their input registers */
}

static inline void sys_mouse_draw_cursor(int x, int y, int full_redraw) {
    register int dummy_eax __asm__("eax") = SYS_MOUSE_DRAW_CURSOR;
    register int dummy_ebx __asm__("ebx") = x;
    register int dummy_ecx __asm__("ecx") = y;
    register int dummy_edx __asm__("edx") = full_redraw;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline int sys_win_msgbox(const char *msg, const char *btn, const char *title) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_MSGBOX), "b"(msg), "c"(btn), "d"(title));
    return ret;
}

static inline void sys_busywait(uint32_t ms) {
    uint32_t start = sys_uptime();
    uint32_t end = start + (ms / 10);  /* uptime is in ticks (10ms) */
    while (sys_uptime() < end) {
        __asm__ volatile("pause");  /* CPU hint for spinloop */
    }
}

static inline void* sys_win_create_form(const char *title, int x, int y, int w, int h) {
    void *ret;
    uint32_t pos = ((uint32_t)x << 16) | (y & 0xFFFF);
    uint32_t size = ((uint32_t)w << 16) | (h & 0xFFFF);
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_CREATE_FORM), "b"(title), "c"(pos), "d"(size));
    return ret;
}

static inline void sys_win_add_control(void *form, gui_control_t *ctrl) {
    register int dummy_eax __asm__("eax") = SYS_WIN_ADD_CONTROL;
    register void *dummy_ebx __asm__("ebx") = form;
    register gui_control_t *dummy_ecx __asm__("ecx") = ctrl;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx));
    /* This has to be done, or else the controls don't show */
}

static inline int sys_win_pump_events(void *form) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_PUMP_EVENTS), "b"(form));
    return ret;
}

static inline void sys_win_draw(void *form) {
    register int dummy_eax __asm__("eax") = SYS_WIN_DRAW;
    register void *dummy_ebx __asm__("ebx") = form;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx) :: "memory");
}

static inline void sys_win_destroy_form(void *form) {
    register int dummy_eax __asm__("eax") = SYS_WIN_DESTROY_FORM;
    register void *dummy_ebx __asm__("ebx") = form;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx) :: "memory");
}

static inline void sys_win_set_icon(void *form, const char *icon_path) {
    register int dummy_eax __asm__("eax") = SYS_WIN_SET_ICON;
    register void *dummy_ebx __asm__("ebx") = form;
    register const char *dummy_ecx __asm__("ecx") = icon_path;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx) :: "memory");
}