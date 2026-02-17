#pragma once
#include <stdint.h>
#include <stddef.h>
#include "rtc.h"
#include "win/window.h"
#include "win/menu.h"

typedef rtc_time_t sys_time_t;

#define PACK_XY(x, y) (((uint32_t)(x) << 16) | ((y) & 0xFFFF))
#define PACK_WH(w, h) (((uint32_t)(w) << 16) | ((h) & 0xFFFF))

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
#define SYS_PROC_SET_ICON   0x0207

static inline int sys_proc_set_icon(int tid, const char *icon_path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_PROC_SET_ICON), "b"(tid), "c"(icon_path));
    return ret;
}

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
#define SYS_GFX_DRAW_CACHED_PARTIAL 0x0910 /* Draw a sub-rect of a cached BMP */
#define SYS_GFX_LOAD_BMP_SCALED 0x0911 /* Load BMP and draw scaled-to-region (no transparency) */
#define SYS_GFX_CACHE_BMP_SCALED 0x0912 /* Load BMP, scale to target dimensions, and cache it */

/* AH = 0Ah - Mouse */
#define SYS_MOUSE_GET_STATE  0x0A00
#define SYS_MOUSE_DRAW_CURSOR 0x0A01
#define SYS_MOUSE_INVALIDATE 0x0A02

/* AH = 0Ch - Power Management */
#define SYS_POWER_SHUTDOWN  0x0C00
#define SYS_POWER_REBOOT    0x0C01

/* AH = 0Dh - Input */
#define SYS_GET_KEY_NONBLOCK 0x0D00
#define SYS_GET_ALT_KEY      0x0D01  /* Peek+consume only Alt+Tab / AltRelease */

/* AH = 0Eh - Sound */
#define SYS_SOUND_DETECTED   0x0E00
#define SYS_SOUND_PLAY_TONE  0x0E01
#define SYS_SOUND_SET_VOLUME 0x0E02
#define SYS_SOUND_STOP       0x0E03
#define SYS_SOUND_PLAY_WAV   0x0E04
#define SYS_SOUND_GET_VOLUME 0x0E05

/* Waveform types */
#define WAVE_SQUARE    0
#define WAVE_TRIANGLE  1
#define WAVE_SINE      2
#define WAVE_SAWTOOTH  3

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
#define SYS_WIN_GET_THEME        0x0B13
#define SYS_WIN_CYCLE_PREVIEW    0x0B14
#define SYS_WIN_CYCLE_COMMIT     0x0B15
#define SYS_WIN_RESTORE_FORM     0x0B16
#define SYS_WIN_FORCE_FULL_REDRAW 0x0B17
#define SYS_WIN_IS_FOCUSED 0x0B18
#define SYS_WIN_DRAW_BUFFER 0x0B19  /* Draw a raw pixel buffer into a form with clipping to avoid overwriting other windows */
#define SYS_WIN_MARK_DIRTY 0x0B1A   /* Mark window region as dirty and trigger compositor redraw with z-order */
#define SYS_WIN_MARK_DIRTY_RECT 0x0B1B /* Mark a given screen rect dirty so compositor redraws overlapping windows */

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
#define PROP_ENABLED   10   /* int - enabled state (unused / repurposed for picturebox image-mode) */

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
#define CTRL_DROPDOWN 9
#define CTRL_CLOCK 10
#define CTRL_SCROLLBAR 11

#define FONT_NORMAL 0
#define FONT_BOLD 1
#define FONT_ITALIC 2
#define FONT_BOLD_ITALIC 3

// Forward declaration
typedef struct bitmap_s bitmap_t;

typedef struct {
    uint8_t type;
    uint16_t x, y, w, h;
    int fg, bg;
    char text[256];
    uint16_t id;
    uint8_t font_type;
    uint8_t font_size;
    uint8_t border;
    uint8_t border_color;
    bitmap_t *cached_bitmap_orig;    /* Original loaded bitmap */
    bitmap_t *cached_bitmap_scaled;  /* Scaled bitmap cached for current control size */
    uint8_t image_mode;              /* 0=center (default), 1=stretch */
    uint8_t pressed;
    uint8_t checked;
    uint16_t group_id;
    /* Textbox-specific fields */
    uint16_t cursor_pos;
    uint16_t max_length;
    uint16_t scroll_offset;
    uint8_t is_focused;
    int16_t sel_start;
    int16_t sel_end;
    /* Dropdown-specific fields (cursor_pos used as selected_index) */
    uint8_t dropdown_open;
    uint8_t item_count;
    int8_t hovered_item;
    /* Dropdown saved background for area that may extend outside window */
    uint8_t *dropdown_saved_bg;
    uint16_t dropdown_saved_w;
    uint16_t dropdown_saved_h;
    int16_t dropdown_saved_x;
    int16_t dropdown_saved_y;
    uint16_t dropdown_scroll; /* first visible item index when dropdown list is scrolled */
    uint8_t load_failed; /* set when image loading failed to avoid retry storm */
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
    char icon_path[64];
    int16_t focused_control_id;
    uint8_t textbox_selecting;
    uint32_t last_icon_click_time;
    int16_t last_icon_click_id;
    menu_t window_menu;
    uint8_t window_menu_initialized;
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

typedef struct {
    void *data;
    int width;
    int height;
} gfx_cached_bmp_t;

typedef struct {
    uint8_t bg_color;
    uint8_t titlebar_color;
    uint8_t titlebar_inactive;
    uint8_t frame_dark;
    uint8_t frame_light;
    uint8_t text_color;
    uint8_t icon_text_color; /* desktop icon label color */
    uint8_t button_color;
    uint8_t taskbar_color;
    uint8_t start_button_color;
    uint8_t desktop_color;
} sys_theme_t;

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

#define SYS_PROC_KILL      0x0208
static inline int sys_kill(int tid) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_PROC_KILL), "b"(tid));
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
    register int dummy_eax __asm__("eax") = SYS_GFX_LINE;
    register uint32_t dummy_ebx __asm__("ebx") = PACK_XY(x0, y0);
    register uint32_t dummy_ecx __asm__("ecx") = PACK_XY(x1, y1);
    register uint8_t dummy_edx __asm__("edx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_rect(int x, int y, int w, int h, uint8_t color) {
    register int dummy_eax __asm__("eax") = SYS_GFX_RECT;
    register uint32_t dummy_ebx __asm__("ebx") = PACK_XY(x, y);
    register uint32_t dummy_ecx __asm__("ecx") = PACK_WH(w, h);
    register uint8_t dummy_edx __asm__("edx") = color;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx), "+r"(dummy_edx) :: "memory");
}

static inline void sys_gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
    register int dummy_eax __asm__("eax") = SYS_GFX_FILLRECT;
    register uint32_t dummy_ebx __asm__("ebx") = PACK_XY(x, y);
    register uint32_t dummy_ecx __asm__("ecx") = PACK_WH(w, h);
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
    uint32_t colors = ((uint32_t)c_start << 16) | ((uint32_t)c_end << 8) | orientation;
    register int dummy_eax __asm__("eax") = SYS_GFX_FILLRECT_GRADIENT;
    register uint32_t dummy_ebx __asm__("ebx") = PACK_XY(x, y);
    register uint32_t dummy_ecx __asm__("ecx") = PACK_WH(w, h);
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
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_LOAD_BMP_EX), "b"(path), "c"(PACK_XY(x, y)), "d"(transparent));
    return ret;
}

static inline int sys_gfx_cache_bmp(const char *path, gfx_cached_bmp_t *out) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_CACHE_BMP), "b"(path), "c"(out) : "memory");
    return ret;
}

static inline void sys_gfx_draw_cached(gfx_cached_bmp_t *bmp, int x, int y, int transparent) {
    __asm__ volatile("int $0x80" :: "a"(SYS_GFX_DRAW_CACHED), "b"(bmp), "c"(PACK_XY(x, y)), "d"(transparent));
}

/* Draw a sub-rectangle of a cached bitmap
   Parameters packed into a small struct on the stack and pointer passed in EBX. Returns 0 on success, -1 on error. */
static inline int sys_gfx_draw_cached_partial(gfx_cached_bmp_t *bmp, int dest_x, int dest_y,
                                              int src_x, int src_y, int src_w, int src_h,
                                              int transparent) {
    int ret;
    struct {
        gfx_cached_bmp_t *bmp;
        int dest_x;
        int dest_y;
        int src_x;
        int src_y;
        int src_w;
        int src_h;
        int transparent;
    } params;

    params.bmp = bmp;
    params.dest_x = dest_x;
    params.dest_y = dest_y;
    params.src_x = src_x;
    params.src_y = src_y;
    params.src_w = src_w;
    params.src_h = src_h;
    params.transparent = transparent;

    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_DRAW_CACHED_PARTIAL), "b"(&params) : "memory");
    return ret;
}

/* Load BMP and draw scaled to region (no transparency). Returns 0 on success, -1 on error. */
static inline int sys_gfx_load_bmp_scaled(const char *path, int dest_x, int dest_y, int dest_w, int dest_h) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_LOAD_BMP_SCALED), "b"(path), "c"(PACK_XY(dest_x, dest_y)), "d"(PACK_WH(dest_w, dest_h)));
    return ret;
}

/* Load BMP, scale to target dimensions, and cache it. Returns 0 on success, -1 on error. */
static inline int sys_gfx_cache_bmp_scaled(const char *path, int target_w, int target_h, gfx_cached_bmp_t *out) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GFX_CACHE_BMP_SCALED), "b"(path), "c"(PACK_WH(target_w, target_h)), "d"(out) : "memory");
    return ret;
}


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

static inline void sys_mouse_invalidate(void) {
    register int dummy_eax __asm__("eax") = SYS_MOUSE_INVALIDATE;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}
static inline int sys_win_msgbox(const char *msg, const char *btn, const char *title) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_MSGBOX), "b"(msg), "c"(btn), "d"(title));
    return ret;
}

static inline void sys_busywait(uint32_t ms) {
    uint32_t start = sys_uptime();
    uint32_t end = start + (ms / 10);
    while (sys_uptime() < end) {
        __asm__ volatile("pause");
    }
}

static inline void* sys_win_create_form(const char *title, int x, int y, int w, int h) {
    void *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_CREATE_FORM), "b"(title), "c"(PACK_XY(x, y)), "d"(PACK_WH(w, h)));
    return ret;
}

static inline void sys_win_add_control(void *form, gui_control_t *ctrl) {
    register int dummy_eax __asm__("eax") = SYS_WIN_ADD_CONTROL;
    register void *dummy_ebx __asm__("ebx") = form;
    register gui_control_t *dummy_ecx __asm__("ecx") = ctrl;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx), "+r"(dummy_ecx));
}

static inline int sys_win_pump_events(void *form) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_PUMP_EVENTS), "b"(form));
    return ret;
}

static inline int sys_win_cycle_preview(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_CYCLE_PREVIEW));
    return ret;
}

static inline int sys_win_cycle_commit(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_CYCLE_COMMIT));
    return ret;
}

static inline int sys_win_restore_form(void *form) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_RESTORE_FORM), "b"(form));
    return ret;
}

static inline void sys_win_force_full_redraw(void) {
    register int dummy_eax __asm__("eax") = SYS_WIN_FORCE_FULL_REDRAW;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

static inline int sys_win_is_focused(void *form) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_IS_FOCUSED), "b"(form));
    return ret;
}

/* Draw a raw pixel buffer into a form. Buffer format: one byte per pixel (color index 0-15).
   Parameters: form, buffer pointer, buffer width/height, src_x,src_y,src_w,src_h, dest_x,dest_y (relative to client area), transparent (0=no,1=skip color 5)
   Returns 0 on success or -1 on error. */
static inline int sys_win_draw_buffer(void *form, const uint8_t *buffer, int buf_w, int buf_h,
                                      int src_x, int src_y, int src_w, int src_h,
                                      int dest_x, int dest_y, int transparent) {
    int ret;
    struct {
        void *form;
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
    } params;

    params.form = form;
    params.buffer = buffer;
    params.buf_w = buf_w;
    params.buf_h = buf_h;
    params.src_x = src_x;
    params.src_y = src_y;
    params.src_w = src_w;
    params.src_h = src_h;
    params.dest_x = dest_x;
    params.dest_y = dest_y;
    params.transparent = transparent;

    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_DRAW_BUFFER), "b"(&params) : "memory");
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

static inline void sys_win_mark_dirty(void *form) {
    register int dummy_eax __asm__("eax") = SYS_WIN_MARK_DIRTY;
    register void *dummy_ebx __asm__("ebx") = form;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx) :: "memory");
}

/* Mark an arbitrary screen rectangle dirty and trigger compositor redraw for
   windows that intersect it. Parameters: x, y, w, h. */
static inline void sys_win_mark_dirty_rect(int x, int y, int w, int h) {
    register int dummy_eax __asm__("eax") = SYS_WIN_MARK_DIRTY_RECT;
    struct { int x; int y; int w; int h; } params;
    params.x = x; params.y = y; params.w = w; params.h = h;
    register void *dummy_ebx __asm__("ebx") = &params;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax), "+r"(dummy_ebx) :: "memory");
}

static inline int sys_get_key_nonblock(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GET_KEY_NONBLOCK));
    return ret;
}

static inline int sys_get_alt_key(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_GET_ALT_KEY));
    return ret;
}

static inline gui_control_t* sys_win_get_control(void *form, int16_t id) {
    gui_control_t *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_GET_CONTROL), "b"(form), "c"(id));
    return ret;
}

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


static inline void ctrl_set_text(void *form, int16_t id, const char *text) {
    sys_ctrl_set_prop(form, id, PROP_TEXT, (uint32_t)text);
}
static inline const char* ctrl_get_text(void *form, int16_t id) {
    return (const char*)sys_ctrl_get_prop(form, id, PROP_TEXT);
}

static inline void ctrl_set_checked(void *form, int16_t id, int checked) {
    sys_ctrl_set_prop(form, id, PROP_CHECKED, checked);
}
static inline int ctrl_get_checked(void *form, int16_t id) {
    return (int)sys_ctrl_get_prop(form, id, PROP_CHECKED);
}

static inline void ctrl_set_image(void *form, int16_t id, const char *path) {
    sys_ctrl_set_prop(form, id, PROP_IMAGE, (uint32_t)path);
}

static inline void ctrl_set_pos(void *form, int16_t id, int x, int y) {
    sys_ctrl_set_prop(form, id, PROP_X, x);
    sys_ctrl_set_prop(form, id, PROP_Y, y);
}

static inline void ctrl_set_size(void *form, int16_t id, int w, int h) {
    sys_ctrl_set_prop(form, id, PROP_W, w);
    sys_ctrl_set_prop(form, id, PROP_H, h);
}

static inline void ctrl_set_fg(void *form, int16_t id, int color) {
    sys_ctrl_set_prop(form, id, PROP_FG, color);
}
static inline void ctrl_set_bg(void *form, int16_t id, int color) {
    sys_ctrl_set_prop(form, id, PROP_BG, color);
}

static inline void ctrl_set_visible(void *form, int16_t id, int visible) {
    sys_ctrl_set_prop(form, id, PROP_VISIBLE, visible);
}

static inline void ctrl_set_enabled(void *form, int16_t id, int enabled) {
    /* Repurposed: used by PictureBox to store image-mode (0=center,1=stretch).
       Also serves as a general 'enabled' flag placeholder (currently unused elsewhere). */
    sys_ctrl_set_prop(form, id, PROP_ENABLED, enabled);
}
static inline int ctrl_get_enabled(void *form, int16_t id) {
    return (int)sys_ctrl_get_prop(form, id, PROP_ENABLED);
}

static inline void sys_win_invalidate_icons(void) {
    register int dummy_eax __asm__("eax") = SYS_WIN_INVALIDATE_ICONS;
    __asm__ volatile("int $0x80" : "+r"(dummy_eax) :: "memory");
}

static inline int sys_win_check_redraw(void) {
    register int result __asm__("eax") = SYS_WIN_CHECK_REDRAW;
    __asm__ volatile("int $0x80" : "+r"(result) :: "memory");
    return result;
}

static inline void sys_win_get_dirty_rect(int *out) {
    register int eax __asm__("eax") = SYS_WIN_GET_DIRTY_RECT;
    register int ebx __asm__("ebx") = (int)out;
    __asm__ volatile("int $0x80" : "+r"(eax) : "r"(ebx) : "memory");
}

static inline sys_theme_t* sys_win_get_theme(void) {
    sys_theme_t *ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_WIN_GET_THEME));
    return ret;
}

static inline int sys_sound_detected(void) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_SOUND_DETECTED));
    return ret;
}

static inline void sys_sound_play_tone(uint16_t frequency, uint32_t duration_ms, uint8_t waveform) {
    __asm__ volatile("int $0x80" :: "a"(SYS_SOUND_PLAY_TONE), "b"(frequency), "c"(duration_ms), "d"(waveform));
}

static inline void sys_sound_set_volume(uint8_t left, uint8_t right) {
    __asm__ volatile("int $0x80" :: "a"(SYS_SOUND_SET_VOLUME), "b"(left), "c"(right));
}

static inline void sys_sound_stop(void) {
    __asm__ volatile("int $0x80" :: "a"(SYS_SOUND_STOP));
}

static inline int sys_sound_play_wav(const char *path) {
    int ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_SOUND_PLAY_WAV), "b"(path));
    return ret;
}

static inline uint32_t sys_sound_get_volume(void) {
    uint32_t ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "a"(SYS_SOUND_GET_VOLUME));
    return ret;
}

/* Event loop helper for standard applications */
typedef int (*sys_event_handler_t)(void *form, int event, void *userdata);

static inline void sys_win_run_event_loop(void *form, sys_event_handler_t handler, void *userdata) {
    int running = 1;
    while (running) {
        int event = sys_win_pump_events(form);
        
        if (event == -3) {
            running = 0;
            continue;
        }
        
        if (event == -1 || event == -2) {
            /* Ensure the form is marked dirty after minimize/restore so
               userland controls that depend on visibility get an immediate redraw. */
            sys_win_mark_dirty(form);
        }
        
        /* Call the handler for positive control events *and* for idle ticks
           (event == 0). This lets applications receive periodic callbacks
           without spinning their own event loop (useful for animations, clocks, etc.). */
        if (event >= 0 && handler && handler(form, event, userdata) != 0) {
            running = 0;
        }
        
        sys_yield();
    }
}