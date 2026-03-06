#include "../../syscall.h"
#include "../../lib/fonts.h"
#include "../../lib/string.h"
#include "../../lib/stdlib.h"
#include "../../lib/ini.h"
#include "../../drivers/keyboard.h"

#define TERM_COLS  80
#define TERM_ROWS  25
#define MAX_GLYPH_H 16
#define MAX_BUF_W  (TERM_COLS * 8)
#define MAX_BUF_H  (TERM_ROWS * MAX_GLYPH_H)
#define TITLEBAR_H 20
#define BORDER     2
#define SCREEN_W   640
#define SCREEN_H   480
#define TASKBAR_H  27

#define CURSOR_BLINK_TICKS 50
#define BG_CTRL_ID 1

static uint8_t pixbuf[MAX_BUF_H * MAX_BUF_W];
static usr_bmf_font_t font;
static void *form;
static void *vc;
static uint8_t font_pt;
static int glyph_w = 8;
static int glyph_h = 8;
static int buf_w;
static int buf_h;

static sys_vc_screen_t screen;
static int cursor_visible;
static uint32_t cursor_blink_tick;
static int need_cursor = 0;
static int shell_tid = 0;

static void redraw(void) {
    gui_form_t *f = (gui_form_t*)form;

    int cw = f->win.w - BORDER*2;
    int ch = f->win.h - TITLEBAR_H - BORDER;

    if (cw > buf_w) cw = buf_w;
    if (ch > buf_h) ch = buf_h;

    sys_win_draw_buffer(form, pixbuf, buf_w, buf_h, 0, 0, cw, ch, BORDER, 0, 0);
}

static void recalc_buf(void) {
    buf_w = TERM_COLS * glyph_w;
    buf_h = TERM_ROWS * glyph_h;
}

static void resize(gui_form_t *f) {
    int cw = f->win.w - BORDER*2;
    int ch = f->win.h - TITLEBAR_H - BORDER;

    if (cw < 0) cw = 0;
    if (ch < 0) ch = 0;

    sys_ctrl_set_prop(form, BG_CTRL_ID, PROP_W, cw);
    sys_ctrl_set_prop(form, BG_CTRL_ID, PROP_H, ch);

    recalc_buf();
}

static void render(void) {
    for (int row = 0; row < TERM_ROWS; row++) {
        for (int col = 0; col < TERM_COLS; col++) {
            int idx = row * TERM_COLS + col;
            uint8_t ch = screen.chars[idx];
            uint8_t attr = screen.attrs[idx];
            uint8_t fg = attr & 0x0F;
            uint8_t bg = (attr >> 4) & 0x0F;

            const usr_bmf_glyph_t *g = usr_bmf_get_glyph(&font, font_pt, ch);

            int px = col * glyph_w;
            int py = row * glyph_h;

            for (int gy = 0; gy < glyph_h; gy++) {
                int off = (py + gy) * buf_w + px;
                for (int gx = 0; gx < glyph_w; gx++) {
                    uint8_t color = bg;
                    if (g && g->bitmap) {
                        int bi = gx >> 3;
                        int bit = 7 - (gx & 7);
                        if ((g->bitmap[gy * g->pitch + bi] >> bit) & 1)
                            color = fg;
                    }
                    pixbuf[off + gx] = color;
                }
            }
        }
    }

    if (cursor_visible) {
        int cx = screen.cursor_x;
        int cy = screen.cursor_y;
        if (cx >= 0 && cx < TERM_COLS && cy >= 0 && cy < TERM_ROWS) {
            int idx = cy * TERM_COLS + cx;
            uint8_t fg = screen.attrs[idx] & 0x0F;
            int px = cx * glyph_w;
            int py = cy * glyph_h;
            for (int gy = glyph_h - 2; gy < glyph_h; gy++) {
                int off = (py + gy) * buf_w + px;
                for (int gx = 0; gx < glyph_w; gx++)
                    pixbuf[off + gx] = fg;
            }
        }
    }
}

static void refresh_cursor(void) {
    int cx = screen.cursor_x;
    int cy = screen.cursor_y;
    if (cx < 0 || cx >= TERM_COLS || cy < 0 || cy >= TERM_ROWS) return;

    int idx = cy * TERM_COLS + cx;
    uint8_t ch = screen.chars[idx];
    uint8_t attr = screen.attrs[idx];
    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x0F;

    const usr_bmf_glyph_t *g = usr_bmf_get_glyph(&font, font_pt, ch);
    int px = cx * glyph_w;
    int py = cy * glyph_h;

    for (int gy = 0; gy < glyph_h; gy++) {
        int off = (py + gy) * buf_w + px;
        for (int gx = 0; gx < glyph_w; gx++) {
            uint8_t color = bg;
            if (g && g->bitmap) {
                int bi = gx >> 3;
                int bit = 7 - (gx & 7);
                if ((g->bitmap[gy * g->pitch + bi] >> bit) & 1)
                    color = fg;
            }
            pixbuf[off + gx] = color;
        }
    }

    if (cursor_visible) {
        for (int gy = glyph_h - 2; gy < glyph_h; gy++) {
            int off = (py + gy) * buf_w + px;
            for (int gx = 0; gx < glyph_w; gx++)
                pixbuf[off + gx] = fg;
        }
    }

    sys_win_draw_buffer(form, pixbuf, buf_w, buf_h,
                        px, py, glyph_w, glyph_h,
                        px + BORDER, py, 0);
}

static int task_alive(int tid) {
    sys_taskinfo_t tasks[32];
    int n = sys_get_tasks(tasks, 32);
    for (int i = 0; i < n; i++)
        if ((int)tasks[i].tid == tid && tasks[i].state != 4)
            return 1;
    return 0;
}

static int handle_event(void *form_arg, int event, void *userdata)
{
    (void)userdata;
    gui_form_t *f = (gui_form_t*)form_arg;

    if (event == -3) {
        sys_kill(shell_tid);
        /* Wait for shell to actually terminate before destroying vconsole */
        while (task_alive(shell_tid)) {
            sys_yield();
        }
        sys_vc_destroy(vc);
        sys_win_destroy_form(form);
        usr_bmf_free(&font);
        sys_exit();
    }

    if (event == -4) {
        resize(f);
    }

    if (sys_win_is_focused(form)) {
        int key = sys_get_key_nonblock();
        while (key > 0) {
            sys_vc_send_key(vc, key);
            key = sys_get_key_nonblock();
        }
    }

    if (!task_alive(shell_tid))
        return 1;

    if (sys_vc_dirty(vc)) {
        sys_vc_read(vc, &screen);
        render();
        redraw();
    }

    uint32_t now = sys_uptime();
    if (now - cursor_blink_tick >= CURSOR_BLINK_TICKS) {
        cursor_visible = !cursor_visible;
        cursor_blink_tick = now;
        need_cursor = 1;
        render();
        redraw();
    }

    if (need_cursor) {
        refresh_cursor();
        need_cursor = 0;
    }

    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    /* Determine font path from INI or use default */
    char font_path[64];
    strcpy(font_path, "C:/FONTS/MONO8X8.BMF");

    char ini_buf[512];
    int fd = sys_open("C:/OSLET/AGIX.INI", "r");
    if (fd >= 0) {
        int n = sys_read(fd, ini_buf, sizeof(ini_buf) - 1);
        sys_close(fd);
        if (n > 0) {
            ini_buf[n] = '\0';
            ini_parser_t ini;
            ini_init(&ini, ini_buf);
            const char *val = ini_get(&ini, "TERMINAL", "FONT");
            if (val && val[0])
                strncpy(font_path, val, sizeof(font_path) - 1);
        }
    }

    if (usr_bmf_import(&font, font_path) != 0) {
        if (usr_bmf_import(&font, "C:/FONTS/MONO8X8.BMF") != 0) {
            sys_win_msgbox("Failed to load font.", "OK", "Terminal");
            sys_exit();
            return;
        }
    }
    font_pt = font.sequences[0].point_size;
    glyph_h = font.sequences[0].height;
    if (glyph_h < 1) glyph_h = 8;
    if (glyph_h > MAX_GLYPH_H) glyph_h = MAX_GLYPH_H;
    recalc_buf();

    int win_w = buf_w + BORDER * 2;
    int win_h = buf_h + TITLEBAR_H + BORDER;
    int win_x = (SCREEN_W - win_w) / 2;
    int win_y = (SCREEN_H - TASKBAR_H - win_h) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    
    /* If window is wider than screen, reduce buffer to fit */
    if (win_w > SCREEN_W) {
        buf_w = SCREEN_W - BORDER * 2;
        recalc_buf();
        win_w = SCREEN_W;
        win_x = 0;
    }

    form = sys_win_create_form("Terminal", win_x, win_y, win_w, win_h);
    if (!form) {
        usr_bmf_free(&font);
        sys_exit();
        return;
    }

    gui_control_t bg_ctrl = {0};
    bg_ctrl.type = CTRL_PICTUREBOX;
    bg_ctrl.id = BG_CTRL_ID;
    bg_ctrl.x = BORDER;
    bg_ctrl.y = 0;
    bg_ctrl.w = win_w - BORDER * 2;
    bg_ctrl.h = win_h - TITLEBAR_H - BORDER;
    bg_ctrl.bg = 0;
    bg_ctrl.fg = 0;
    bg_ctrl.border = 0;
    sys_win_add_control(form, &bg_ctrl);

    vc = sys_vc_create();
    if (!vc) {
        sys_win_destroy_form(form);
        usr_bmf_free(&font);
        sys_exit();
        return;
    }

    char spawn_path[128];
    char spawn_args[256];
    strcpy(spawn_path, "C:/SHELL.ELF");
    spawn_args[0] = '\0';
    
    char args[256];
    if (sys_getargs(args, sizeof(args)) && args[0]) {
        /* Parse: first token is the program path, rest are its arguments */
        int i = 0;
        int j = 0;
        
        /* Skip leading whitespace */
        while (args[i] == ' ' || args[i] == '\t') i++;
        
        /* Extract program path (until space or end) */
        while (args[i] && args[i] != ' ' && args[i] != '\t' && j < sizeof(spawn_path) - 1) {
            spawn_path[j++] = args[i++];
        }
        spawn_path[j] = '\0';
        
        /* Skip whitespace before arguments */
        while (args[i] == ' ' || args[i] == '\t') i++;
        
        /* Copy remaining as program arguments */
        if (args[i]) {
            strncpy(spawn_args, &args[i], sizeof(spawn_args) - 1);
            spawn_args[sizeof(spawn_args) - 1] = '\0';
        }
    }

    shell_tid = sys_spawn_async_args(spawn_path, spawn_args);
    if (shell_tid <= 0) {
        sys_vc_destroy(vc);
        sys_win_destroy_form(form);
        usr_bmf_free(&font);
        sys_exit();
        return;
    }

    sys_vc_attach(vc, shell_tid);
    sys_vc_read(vc, &screen);

    cursor_visible = 1;
    cursor_blink_tick = sys_uptime();
    memset(pixbuf, 0, buf_w * buf_h);

    sys_win_run_event_loop(form, handle_event, NULL);
}
