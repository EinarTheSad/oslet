#pragma once
#include <stdint.h>
#include <stddef.h>
#include "rtc.h"
#include "win/window.h"
#include "win/menu.h"

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
#define SYS_PROC_SPAWN_ASYNC 0x0206

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
#define SYS_GFX_LOAD_BMP_EX 0x090C
#define SYS_GFX_CACHE_BMP   0x090D
#define SYS_GFX_DRAW_CACHED 0x090E
#define SYS_GFX_FREE_CACHED 0x090F

/* AH = 0Ah - Mouse */
#define SYS_MOUSE_GET_STATE  0x0A00
#define SYS_MOUSE_DRAW_CURSOR 0x0A01

/* AH = 0Ch - Power Management */
#define SYS_POWER_SHUTDOWN  0x0C00
#define SYS_POWER_REBOOT    0x0C01

/* AH = 0Bh - Windows */
#define SYS_WIN_MSGBOX          0x0B00
#define SYS_WIN_CREATE_FORM     0x0B05
#define SYS_WIN_PUMP_EVENTS     0x0B07
#define SYS_WIN_ADD_CONTROL     0x0B08
#define SYS_WIN_DRAW            0x0B09
#define SYS_WIN_DESTROY_FORM    0x0B0A
#define SYS_WIN_SET_ICON        0x0B0B
#define SYS_WIN_REDRAW_ALL      0x0B0C
#define SYS_WIN_GET_CONTROL     0x0B0D
#define SYS_WIN_CTRL_SET_PROP   0x0B0E
#define SYS_WIN_CTRL_GET_PROP   0x0B0F
#define SYS_WIN_INVALIDATE_ICONS 0x0B10
#define SYS_WIN_CHECK_REDRAW     0x0B11
#define SYS_WIN_GET_DIRTY_RECT   0x0B12

/* Control property IDs for sys_ctrl_set/get */
#define PROP_TEXT       0   /* char* - text content */
#define PROP_CHECKED    1   /* int - checkbox/radio state */
#define PROP_X          2   /* int - x position */
#define PROP_Y          3   /* int - y position */
#define PROP_W          4   /* int - width */
#define PROP_H          5   /* int - height */
#define PROP_VISIBLE    6   /* int - visibility (0/1) */
#define PROP_FG         7   /* int - foreground color */
#define PROP_BG         8   /* int - background color */
#define PROP_IMAGE      9   /* char* - image path (picturebox) */
#define PROP_ENABLED   10   /* int - enabled state */

#define MSG_QUEUE_SIZE 16
#define MSG_MAX_SIZE   128

/* Form controls */
#define CTRL_BUTTON 1
#define CTRL_LABEL 2
#define CTRL_PICTUREBOX 3
#define CTRL_CHECKBOX 4
#define CTRL_RADIOBUTTON 5
#define CTRL_TEXTBOX 6
#define CTRL_FRAME 7
#define CTRL_ICON 8

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_ITALIC 2
#define FONT_BOLD_ITALIC 3

// Forward declaration
typedef struct bitmap_s bitmap_t;

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
    bitmap_t *cached_bitmap;  // Changed from uint8_t*
    uint8_t pressed;
    uint8_t checked;  // For checkbox and radio button
    uint16_t group_id;  // For radio button groups
    /* Textbox-specific fields */
    uint16_t cursor_pos;  // Cursor position in text
    uint16_t max_length;  // Maximum text length (0 = use default 255)
    uint16_t scroll_offset;  // For horizontal scrolling when text overflows
    uint8_t is_focused;  // Set by form before drawing (for textbox cursor)
    /* Text selection */
    int16_t sel_start;  // Selection start (-1 = no selection)
    int16_t sel_end;    // Selection end
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
    char icon_path[64];  // Icon path for minimized window
    /* Focus tracking for keyboard input */
    int16_t focused_control_id;  // ID of control with keyboard focus (-1 = none)
    /* Textbox mouse selection tracking */
    uint8_t textbox_selecting;  // Currently selecting text with mouse
    /* Icon double-click tracking */
    uint32_t last_icon_click_time;
    int16_t last_icon_click_id;
    /* Window menu (shown when clicking minimize button) */
    menu_t window_menu;
    uint8_t window_menu_initialized;
    /* Owner task ID for cleanup on process exit */
    uint32_t owner_tid;
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
    char name[256];
    uint32_t size;
    uint32_t first_cluster;
    uint16_t mtime;  /* FAT time format */
    uint16_t mdate;  /* FAT date format */
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
void wm_cleanup_task(uint32_t tid);

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

static inline int sys_spawn_async(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_PROC_SPAWN_ASYNC), "b"(path));
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

static inline void sys_shutdown(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_POWER_SHUTDOWN));
}

static inline void sys_reboot(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_POWER_REBOOT));
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
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_TIME_GET), "b"(time) : "memory");
    return ret;
}

static inline int sys_gfx_load_bmp(const char *path, int x, int y) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_LOAD_BMP), "b"(path), "c"(x), "d"(y));
    return ret;
}

static inline int sys_gfx_load_bmp_ex(const char *path, int x, int y, int transparent) {
    int ret;
    uint32_t pos = ((uint32_t)x << 16) | ((uint32_t)y & 0xFFFF);
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_LOAD_BMP_EX), "b"(path), "c"(pos), "d"(transparent));
    return ret;
}

/* Cached BMP handle (returned by sys_gfx_cache_bmp) */
typedef struct {
    void *data;
    int width;
    int height;
} gfx_cached_bmp_t;

/* Load BMP into memory cache, returns 0 on success */
static inline int sys_gfx_cache_bmp(const char *path, gfx_cached_bmp_t *out) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_CACHE_BMP), "b"(path), "c"(out) : "memory");
    return ret;
}

/* Draw cached BMP at position */
static inline void sys_gfx_draw_cached(gfx_cached_bmp_t *bmp, int x, int y, int transparent) {
    uint32_t pos = ((uint32_t)x << 16) | ((uint32_t)y & 0xFFFF);
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_DRAW_CACHED), "b"(bmp), "c"(pos), "d"(transparent));
}

/* Free cached BMP */
static inline void sys_gfx_free_cached(gfx_cached_bmp_t *bmp) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_FREE_CACHED), "b"(bmp));
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

static inline void sys_win_redraw_all(void) {
    register int dummy_eax __asm__("eax") = SYS_WIN_REDRAW_ALL;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

static inline gui_control_t* sys_win_get_control(void *form, int16_t id) {
    gui_control_t *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_GET_CONTROL), "b"(form), "c"(id));
    return ret;
}

/* Low-level property syscalls - pack control_id and prop_id into ecx */
static inline void sys_ctrl_set_prop(void *form, int16_t ctrl_id, int prop_id, uint32_t value) {
    uint32_t packed = ((uint32_t)ctrl_id << 16) | (prop_id & 0xFFFF);
    register int dummy_eax __asm__("eax") = SYS_WIN_CTRL_SET_PROP;
    register void *dummy_ebx __asm__("ebx") = form;
    register uint32_t dummy_ecx __asm__("ecx") = packed;
    register uint32_t dummy_edx __asm__("edx") = value;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline uint32_t sys_ctrl_get_prop(void *form, int16_t ctrl_id, int prop_id) {
    uint32_t packed = ((uint32_t)ctrl_id << 16) | (prop_id & 0xFFFF);
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_CTRL_GET_PROP), "b"(form), "c"(packed));
    return ret;
}

/*
 * High-level control property API
 * Usage examples:
 *   ctrl_set_text(form, 3, "Hello");           // Label.Text = "Hello"
 *   ctrl_set_text(form, 5, textbox->text);     // Label.Text = TextBox.Text
 *   ctrl_set_image(form, 2, "/path/to.bmp");   // PictureBox.Image = path
 *   ctrl_set_checked(form, 4, 1);              // CheckBox.Checked = true
 *   int checked = ctrl_get_checked(form, 4);   // if (CheckBox.Checked)
 */

/* Text property (PROP_TEXT) */
static inline void ctrl_set_text(void *form, int16_t id, const char *text) {
    sys_ctrl_set_prop(form, id, PROP_TEXT, (uint32_t)text);
}
static inline const char* ctrl_get_text(void *form, int16_t id) {
    return (const char*)sys_ctrl_get_prop(form, id, PROP_TEXT);
}

/* Checked property (PROP_CHECKED) */
static inline void ctrl_set_checked(void *form, int16_t id, int checked) {
    sys_ctrl_set_prop(form, id, PROP_CHECKED, checked);
}
static inline int ctrl_get_checked(void *form, int16_t id) {
    return (int)sys_ctrl_get_prop(form, id, PROP_CHECKED);
}

/* Image path property (PROP_IMAGE) - for PictureBox */
static inline void ctrl_set_image(void *form, int16_t id, const char *path) {
    sys_ctrl_set_prop(form, id, PROP_IMAGE, (uint32_t)path);
}

/* Position properties */
static inline void ctrl_set_pos(void *form, int16_t id, int x, int y) {
    sys_ctrl_set_prop(form, id, PROP_X, x);
    sys_ctrl_set_prop(form, id, PROP_Y, y);
}

/* Size properties */
static inline void ctrl_set_size(void *form, int16_t id, int w, int h) {
    sys_ctrl_set_prop(form, id, PROP_W, w);
    sys_ctrl_set_prop(form, id, PROP_H, h);
}

/* Color properties */
static inline void ctrl_set_fg(void *form, int16_t id, int color) {
    sys_ctrl_set_prop(form, id, PROP_FG, color);
}
static inline void ctrl_set_bg(void *form, int16_t id, int color) {
    sys_ctrl_set_prop(form, id, PROP_BG, color);
}

/* Visibility */
static inline void ctrl_set_visible(void *form, int16_t id, int visible) {
    sys_ctrl_set_prop(form, id, PROP_VISIBLE, visible);
}

static inline void sys_win_invalidate_icons(void) {
    register int dummy_eax __asm__("eax") = SYS_WIN_INVALIDATE_ICONS;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

/* Check if full redraw is needed (window was destroyed) - returns 1=full, 2=dirty rect */
static inline int sys_win_check_redraw(void) {
    register int result __asm__("eax") = SYS_WIN_CHECK_REDRAW;
    __asm__ volatile("int $0x80" : "+r"(result) :: "memory");
    return result;
}

/* Get dirty rectangle (x, y, w, h) */
static inline void sys_win_get_dirty_rect(int *out) {
    register int eax __asm__("eax") = SYS_WIN_GET_DIRTY_RECT;
    register int ebx __asm__("ebx") = (int)out;
    __asm__ volatile("int $0x80" : "+r"(eax) : "r"(ebx) : "memory");
}