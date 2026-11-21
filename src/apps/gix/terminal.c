#include "../../syscall.h"
#include "../../lib/fonts.h"
#include "../../lib/string.h"
#include "../../lib/stdlib.h"
#include "../../lib/ini.h"
#include "../../lib/pathdlg.h"
#include "../../lib/app.h"
#include "../../lib/display.h"
#include "../../win/wm_config.h"
#include "../../drivers/keyboard.h"
#include "../../lib/elf.h"  /* needed for mode detection */

OSLET_APP("Terminal", OSLET_KIND_GIX, "C:/ICONS/TERMINAL.ICO", OSLET_APP_FLAG_NONE);

#define INITIAL_COLS 80
#define INITIAL_ROWS 25
#define TITLEBAR_H 20
#define BORDER     2
#define SCROLL_W   18
#define SCREEN_W   WM_SCREEN_WIDTH
#define SCREEN_H   WM_SCREEN_HEIGHT
#define TASKBAR_H  27

#define CURSOR_BLINK_TICKS 50
#define BG_CTRL_ID 1
#define SCROLL_CTRL_ID 2
#define CANVAS_CTRL_ID 3
#define MAX_CELLS (SYS_VC_MAX_COLS * SYS_VC_MAX_ROWS)
#define INI_PATH "C:/OSLET/AGIX.INI"
#define DEFAULT_FONT "C:/FONTS/MONO8X8.BMF"
#define INI_CAPACITY 16384
#define FONT_PATH_SIZE 240

enum {
    MENU_NEW = 1, MENU_EXIT, MENU_80X25, MENU_80X50, MENU_FONT
};

static uint8_t *pixbuf;
static uint8_t chars[MAX_CELLS], attrs[MAX_CELLS];
static uint8_t old_chars[MAX_CELLS], old_attrs[MAX_CELLS];
static usr_bmf_font_t font;
static void *form;
static void *vc;
static uint8_t font_pt;
static int glyph_w = 8;
static int glyph_h = 8;
static int buf_w;
static int buf_h;
static int console_cols;
static int console_rows;

static sys_vc_view_t screen;
static int cursor_visible;
static uint32_t cursor_blink_tick;
static int old_cursor_x = -1, old_cursor_y = -1;
static int scroll_offset;
static int scroll_lines;
static uint32_t scroll_sequence;
static int have_screen;
static int shell_tid = 0;
static char ini_data[INI_CAPACITY];
static char ini_output[INI_CAPACITY + FONT_PATH_SIZE + 32];

static int read_ini(void) {
    int fd = sys_open(INI_PATH, "r");
    if (fd < 0) return -1;
    int used = 0, n;
    while (used < INI_CAPACITY - 1) {
        n = sys_read(fd, ini_data + used, INI_CAPACITY - 1 - used);
        if (n <= 0) break;
        used += n;
    }
    if (used == INI_CAPACITY - 1) {
        char extra;
        n = sys_read(fd, &extra, 1);
        if (n != 0) n = -1;
    }
    int close_result = sys_close(fd);
    if (n < 0 || close_result != 0) return -1;
    for (int i = 0; i < used; i++)
        if (!ini_data[i]) return -1;
    ini_data[used] = '\0';
    return 0;
}

static int ini_missing(void) {
    sys_dirent_t *entries = malloc(256 * sizeof(*entries));
    if (!entries) return 0;
    int count = sys_readdir("C:/OSLET", entries, 256);
    int missing = count >= 0 && count < 256;
    for (int i = 0; i < count && i < 256; i++) {
        if (strcasecmp(entries[i].name, "AGIX.INI") == 0) {
            missing = 0;
            break;
        }
    }
    free(entries);
    return missing;
}

static int append_ini(int *used, const char *text, int len) {
    if (len < 0 || *used + len >= (int)sizeof(ini_output)) return -1;
    memcpy(ini_output + *used, text, len);
    *used += len;
    ini_output[*used] = '\0';
    return 0;
}

static int append_font(int *used, const char *path) {
    if (*used && ini_output[*used - 1] != '\n' && append_ini(used, "\n", 1) != 0)
        return -1;
    if (append_ini(used, "FONT=", 5) != 0 ||
        append_ini(used, path, strlen(path)) != 0 ||
        append_ini(used, "\n", 1) != 0)
        return -1;
    return 0;
}

static int save_font(const char *path) {
    if (read_ini() != 0) {
        if (!ini_missing()) return -1;
        ini_data[0] = '\0';
    }
    int used = 0, in_terminal = 0, seen_section = 0, written_font = 0;
    ini_output[0] = '\0';
    const char *line = ini_data;
    while (*line) {
        const char *end = line;
        while (*end && *end != '\n') end++;
        const char *next = *end ? end + 1 : end;
        int len = end - line;
        if (len >= INI_MAX_LINE) return -1;
        char text[INI_MAX_LINE];
        memcpy(text, line, len);
        text[len] = '\0';
        str_trim(text);
        if (text[0] == '[') {
            char *close = strchr(text + 1, ']');
            if (close) {
                if (in_terminal && !written_font) {
                    if (append_font(&used, path) != 0) return -1;
                    written_font = 1;
                }
                *close = '\0';
                in_terminal = strcasecmp(text + 1, "TERMINAL") == 0;
                if (in_terminal) seen_section = 1;
            }
        } else if (in_terminal && text[0] != ';' && text[0] != '#') {
            char *equals = strchr(text, '=');
            if (equals) {
                *equals = '\0';
                str_trim(text);
                if (strcasecmp(text, "FONT") == 0) {
                    if (append_font(&used, path) != 0) return -1;
                    written_font = 1;
                    line = next;
                    continue;
                }
            }
        }
        if (append_ini(&used, line, next - line) != 0) return -1;
        line = next;
    }
    if (!seen_section) {
        if (used && ini_output[used - 1] != '\n' && append_ini(&used, "\n", 1) != 0)
            return -1;
        if (append_ini(&used, "[TERMINAL]\n", 11) != 0) return -1;
    }
    if (!written_font && append_font(&used, path) != 0) return -1;
    int fd = sys_open(INI_PATH, "w");
    if (fd < 0) return -1;
    int n = sys_write_file(fd, ini_output, used);
    int close_result = sys_close(fd);
    return n == used && close_result == 0 ? 0 : -1;
}

static int font_size(usr_bmf_font_t *selected, int *width, int *height) {
    int max_width = 0;
    uint8_t pt = selected->sequences[0].point_size;
    for (int c = 0; c < 256; c++) {
        const usr_bmf_glyph_t *g = usr_bmf_get_glyph(selected, pt, c);
        if (g && g->width > max_width) max_width = g->width;
    }
    if (!max_width || !selected->sequences[0].height) return -1;
    *width = max_width;
    *height = selected->sequences[0].height;
    return 0;
}

static void render_cell(int row, int col, uint8_t ch, uint8_t attr, int draw_cursor) {
    uint8_t fg = attr & 0x0F;
    uint8_t bg = (attr >> 4) & 0x0F;

    const usr_bmf_glyph_t *g = usr_bmf_get_glyph(&font, font_pt, ch);
    int px = col * glyph_w;
    int py = row * glyph_h;

    for (int gy = 0; gy < glyph_h; gy++) {
        int off = (py + gy) * buf_w + px;
        for (int gx = 0; gx < glyph_w; gx++) {
            uint8_t color = bg;
            if (g && g->bitmap && gx < g->width) {
                int bi = gx >> 3;
                int bit = 7 - (gx & 7);
                if ((g->bitmap[gy * g->pitch + bi] >> bit) & 1)
                    color = fg;
            }
            if (px + gx >= 0 && px + gx < buf_w && py + gy >= 0 && py + gy < buf_h)
                pixbuf[off + gx] = color;
        }
    }

    if (draw_cursor) {
        for (int gy = glyph_h > 1 ? glyph_h - 2 : 0; gy < glyph_h; gy++) {
            int off = (py + gy) * buf_w + px;
            for (int gx = 0; gx < glyph_w; gx++) {
                if (px + gx >= 0 && px + gx < buf_w && py + gy >= 0 && py + gy < buf_h)
                    pixbuf[off + gx] = fg;
            }
        }
    }
}

static int resize(gui_form_t *f) {
    int cw = f->win.w - BORDER * 2;
    int ch = f->win.h - TITLEBAR_H - MENUBAR_HEIGHT - BORDER;
    if (cw < SCROLL_W + glyph_w) cw = SCROLL_W + glyph_w;
    if (ch < glyph_h) ch = glyph_h;
    int cols = (cw - SCROLL_W) / glyph_w;
    int rows = ch / glyph_h;
    if (cols > SYS_VC_MAX_COLS) cols = SYS_VC_MAX_COLS;
    if (rows > SYS_VC_MAX_ROWS) rows = SYS_VC_MAX_ROWS;

    if (!pixbuf || cols != console_cols || rows != console_rows ||
        cols * glyph_w != buf_w || rows * glyph_h != buf_h) {
        int w = cols * glyph_w, h = rows * glyph_h;
        uint8_t *pixels = malloc(w * h);
        if (!pixels) return -1;
        memset(pixels, 0, w * h);
        sys_picturebox_buffer_t buffer = { pixels, w, h };
        int result = sys_ctrl_set_prop(form, CANVAS_CTRL_ID, PROP_BUFFER, (uint32_t)&buffer);
        free(pixels);
        gui_control_t *canvas = sys_win_get_control(form, CANVAS_CTRL_ID);
        if (result <= 0 || !canvas || !canvas->picturebox.buffer ||
            canvas->picturebox.buffer_w != w || canvas->picturebox.buffer_h != h)
            return -1;
        if (sys_vc_resize(vc, cols, rows) != 0) {
            return -1;
        }
        pixbuf = canvas->picturebox.buffer;
        buf_w = w;
        buf_h = h;
        console_cols = cols;
        console_rows = rows;
        have_screen = 0;
        ctrl_set_size(form, CANVAS_CTRL_ID, w, h);
    }

    ctrl_set_size(form, BG_CTRL_ID, cw, ch);
    sys_ctrl_set_prop(form, SCROLL_CTRL_ID, PROP_X, BORDER + cw - SCROLL_W);
    sys_ctrl_set_prop(form, SCROLL_CTRL_ID, PROP_H, ch);
    return 0;
}

static int read_screen(void) {
    screen.chars = chars;
    screen.attrs = attrs;
    screen.capacity = MAX_CELLS;
    screen.offset = scroll_offset;
    if (sys_vc_read_view(vc, &screen) != 0) return -1;
    if (scroll_offset > 0 && screen.history_sequence != scroll_sequence) {
        uint32_t added = screen.history_sequence - scroll_sequence;
        if (added > (uint32_t)screen.history_lines) added = screen.history_lines;
        screen.offset = scroll_offset + (int)added;
        if (sys_vc_read_view(vc, &screen) != 0) return -1;
    }
    scroll_offset = screen.offset;
    scroll_sequence = screen.history_sequence;
    return 0;
}

static int update_scrollbar(void) {
    gui_control_t *bar = sys_win_get_control(form, SCROLL_CTRL_ID);
    if (!bar) return -1;
    int visible = screen.history_lines > 0;
    int was_visible = !(bar->type & 0x80);
    int pos = screen.history_lines - scroll_offset;
    int changed = bar->scrollbar.max_length != screen.history_lines ||
                  bar->scrollbar.cursor_pos != pos || visible != was_visible;
    bar->scrollbar.max_length = screen.history_lines;
    bar->scrollbar.cursor_pos = pos;
    if (visible != was_visible) {
        ctrl_set_visible(form, SCROLL_CTRL_ID, visible);
        if (!visible) sys_win_redraw_control(form, BG_CTRL_ID);
    }
    if (changed && visible) sys_win_redraw_control(form, SCROLL_CTRL_ID);
    scroll_lines = screen.history_lines;
    return was_visible && !visible;
}

static void render(int full) {
    int cx = cursor_visible && scroll_offset == 0 ? screen.cursor_x : -1;
    int cy = cursor_visible && scroll_offset == 0 ? screen.cursor_y : -1;
    if (cx == screen.cols) cx--;
    int cursor_changed = cx != old_cursor_x || cy != old_cursor_y;
    for (int row = 0; row < screen.rows; row++) {
        int start = -1;
        for (int col = 0; col <= screen.cols; col++) {
            int idx = row * screen.cols + col;
            int changed = col < screen.cols &&
                (!have_screen || chars[idx] != old_chars[idx] || attrs[idx] != old_attrs[idx] ||
                 (cursor_changed && ((row == cy && col == cx) ||
                                     (row == old_cursor_y && col == old_cursor_x))));
            if (changed) {
                render_cell(row, col, chars[idx], attrs[idx], row == cy && col == cx);
                old_chars[idx] = chars[idx];
                old_attrs[idx] = attrs[idx];
                if (start < 0) start = col;
            } else if (start >= 0) {
                if (!full)
                    sys_win_draw_buffer(form, pixbuf, buf_w, buf_h, start * glyph_w,
                                        row * glyph_h, (col - start) * glyph_w, glyph_h,
                                        BORDER + start * glyph_w, MENUBAR_HEIGHT + row * glyph_h, 0);
                start = -1;
            }
        }
    }
    have_screen = 1;
    old_cursor_x = cx;
    old_cursor_y = cy;
    if (full)
        sys_win_draw_buffer(form, pixbuf, buf_w, buf_h, 0, 0, buf_w, buf_h, BORDER, MENUBAR_HEIGHT, 0);
}

static int task_alive(int tid) {
    sys_taskinfo_t tasks[32];
    int n = sys_get_tasks(tasks, 32);
    for (int i = 0; i < n; i++)
        if ((int)tasks[i].tid == tid && tasks[i].state != 4)
            return 1;
    return 0;
}

static void stop_shell(void) {
    if (shell_tid <= 0)
        return;

    sys_kill(shell_tid);
    while (task_alive(shell_tid))
        sys_sleep(10);
    shell_tid = 0;
}

static void terminal_cleanup(void) {
    if (vc) {
        sys_vc_destroy(vc);
        vc = NULL;
    }
    if (form) {
        sys_win_destroy_form(form);
        form = NULL;
    }
    pixbuf = NULL;
    usr_bmf_free(&font);
}

static int select_font(void) {
    char path[FONT_PATH_SIZE];
    if (!gui_show_path_dialog_filtered("Open Font", "C:/FONTS/", "*.BMF",
                                       path, sizeof(path)))
        return 0;
    usr_bmf_font_t selected = {0};
    int width, height;
    if (usr_bmf_import(&selected, path) != 0 ||
        font_size(&selected, &width, &height) != 0) {
        usr_bmf_free(&selected);
        sys_win_msgbox("Unable to load the selected font.", "OK", "Terminal");
        return 0;
    }
    gui_form_t *f = form;
    if (width > f->win.w - BORDER * 2 - SCROLL_W ||
        height > f->win.h - TITLEBAR_H - MENUBAR_HEIGHT - BORDER) {
        usr_bmf_free(&selected);
        sys_win_msgbox("The font is too large for this window.", "OK", "Terminal");
        return 0;
    }
    usr_bmf_free(&font);
    font = selected;
    font_pt = font.sequences[0].point_size;
    glyph_w = width;
    glyph_h = height;
    have_screen = 0;
    if (resize(f) != 0) {
        sys_win_msgbox("Unable to resize the console.", "OK", "Terminal");
        return 2;
    }
    if (save_font(path) != 0)
        sys_win_msgbox("The font was changed, but could not be saved.", "OK", "Terminal");
    return 1;
}

static int menu_action(int action) {
    if (action == MENU_EXIT) {
        stop_shell();
        return -1;
    }
    if (action == MENU_NEW) {
        if (sys_spawn_async("C:/OSLET/START/TERMINAL.ELF") <= 0)
            sys_win_msgbox("Unable to open a new terminal.", "OK", "Terminal");
    } else if (action == MENU_80X25 || action == MENU_80X50) {
        int rows = action == MENU_80X25 ? 25 : 50;
        int w = 80 * glyph_w + SCROLL_W + BORDER * 2;
        int h = rows * glyph_h + TITLEBAR_H + MENUBAR_HEIGHT + BORDER;
        if (w > SCREEN_W || h > SCREEN_H - TASKBAR_H) {
            sys_win_msgbox("This size does not fit on the display with the selected font.",
                           "OK", "Terminal");
        } else if (sys_win_set_size(form, w, h) != 0) {
            sys_win_msgbox("Unable to resize the window.", "OK", "Terminal");
        } else if (resize((gui_form_t*)form) != 0) {
            sys_win_msgbox("Unable to resize the console.", "OK", "Terminal");
            return 2;
        } else {
            return 1;
        }
    } else if (action == MENU_FONT) {
        return select_font();
    }
    return 0;
}

static int handle_event(void *form_arg, const sys_win_event_t *event, void *userdata)
{
    (void)userdata;
    gui_form_t *f = (gui_form_t*)form_arg;
    int full = 0;
    int update = 0;
    if (event->type == SYS_WIN_EVENT_MENU) {
        int result = menu_action(event->id);
        if (result < 0) return 1;
        if (result == 2) {
            stop_shell();
            return 1;
        }
        full = 1;
        update = result == 1;
        if (update) {
            sys_win_draw(form);
            sys_win_force_full_redraw();
            sys_win_invalidate_icons();
        }
    } else if (event->type == SYS_WIN_EVENT_RESIZE) {
        if (resize(f) != 0) {
            sys_win_msgbox("Unable to resize the console.", "OK", "Terminal");
            stop_shell();
            return 1;
        }
        sys_win_draw(form);
        sys_win_force_full_redraw();
        sys_win_invalidate_icons();
        full = update = 1;
    } else if (event->type == SYS_WIN_EVENT_WINDOW_CHANGED) {
        sys_win_draw(form);
        sys_win_force_full_redraw();
        sys_win_invalidate_icons();
        full = 1;
    } else if (event->type == SYS_WIN_EVENT_REDRAW ||
               (event->type == SYS_WIN_EVENT_CONTROL &&
                (event->id == BG_CTRL_ID || event->id == CANVAS_CTRL_ID))) {
        full = 1;
    } else if (event->type == SYS_WIN_EVENT_CONTROL && event->id == SCROLL_CTRL_ID) {
        gui_control_t *bar = sys_win_get_control(form, SCROLL_CTRL_ID);
        if (bar) {
            scroll_offset = scroll_lines - bar->scrollbar.cursor_pos;
            update = 1;
        }
    }

    if (sys_win_is_focused(form)) {
        int key = sys_get_key_nonblock();
        while (key > 0) {
            if (key == 14 || key == 24) {
                if (menu_action(key == 14 ? MENU_NEW : MENU_EXIT) < 0) return 1;
                key = sys_get_key_nonblock();
                continue;
            }
            if (scroll_offset) {
                scroll_offset = 0;
                update = 1;
            }
            sys_vc_send_key(vc, key);
            key = sys_get_key_nonblock();
        }
    }

    if (!task_alive(shell_tid))
        return 1;

    if (!f->win.is_minimized) {
        int dirty = sys_vc_dirty(vc);
        if (dirty < 0) {
            sys_win_msgbox("Unable to read the console.", "OK", "Terminal");
            stop_shell();
            return 1;
        }
        if (dirty || update || !have_screen) {
            if (read_screen() != 0) {
                sys_win_msgbox("Unable to read the console.", "OK", "Terminal");
                stop_shell();
                return 1;
            }
            int result = update_scrollbar();
            if (result < 0) {
                sys_win_msgbox("Unable to update the scrollbar.", "OK", "Terminal");
                stop_shell();
                return 1;
            }
            if (result) full = 1;
            update = 1;
        }
        uint32_t now = sys_uptime();
        if (now - cursor_blink_tick >= CURSOR_BLINK_TICKS) {
            cursor_visible = !cursor_visible;
            cursor_blink_tick = now;
            update = 1;
        }
        if (full || update) render(full);
    }

    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    if (oslet_display_init() != 0) {
        sys_win_msgbox("Unable to query display dimensions.", "OK", "Terminal");
        sys_exit();
        return;
    }

    char font_path[FONT_PATH_SIZE];
    strcpy(font_path, DEFAULT_FONT);
    if (read_ini() == 0) {
        ini_parser_t ini;
        char configured[FONT_PATH_SIZE];
        ini_init(&ini, ini_data);
        if (ini_get_into(&ini, "TERMINAL", "FONT", configured, sizeof(configured)) == 0 &&
            configured[0])
            strcpy(font_path, configured);
    }

    if (usr_bmf_import(&font, font_path) != 0 ||
        font_size(&font, &glyph_w, &glyph_h) != 0) {
        usr_bmf_free(&font);
        if (usr_bmf_import(&font, DEFAULT_FONT) != 0 ||
            font_size(&font, &glyph_w, &glyph_h) != 0) {
            sys_win_msgbox("Failed to load font.", "OK", "Terminal");
            usr_bmf_free(&font);
            sys_exit();
            return;
        }
    }
    font_pt = font.sequences[0].point_size;
    int win_w = INITIAL_COLS * glyph_w + SCROLL_W + BORDER * 2;
    int win_h = INITIAL_ROWS * glyph_h + TITLEBAR_H + MENUBAR_HEIGHT + BORDER;
    if (win_w > SCREEN_W) win_w = SCREEN_W;
    if (win_h > SCREEN_H - TASKBAR_H) win_h = SCREEN_H - TASKBAR_H;
    int win_x = (SCREEN_W - win_w) / 2;
    int win_y = (SCREEN_H - TASKBAR_H - win_h) / 2;
    if (win_x < 0) win_x = 0;
    if (win_y < 0) win_y = 0;
    
    form = sys_win_create_form("Terminal", win_x, win_y, win_w, win_h);
    if (!form) {
        usr_bmf_free(&font);
        sys_exit();
        return;
    }
    sys_win_set_icon(form, "C:/ICONS/TERMINAL.ICO");
    sys_win_menubar_enable(form);
    int file_menu = sys_win_menubar_add_menu(form, "File");
    int window_menu = sys_win_menubar_add_menu(form, "Window");
    if (file_menu < 0 || window_menu < 0 ||
        sys_win_menubar_add_item(form, file_menu, "New window\tCtrl+N", MENU_NEW) < 0 ||
        sys_win_menubar_add_separator(form, file_menu) < 0 ||
        sys_win_menubar_add_item(form, file_menu, "Exit\tCtrl+X", MENU_EXIT) < 0 ||
        sys_win_menubar_add_item(form, window_menu, "80x25", MENU_80X25) < 0 ||
        sys_win_menubar_add_item(form, window_menu, "80x50", MENU_80X50) < 0 ||
        sys_win_menubar_add_separator(form, window_menu) < 0 ||
        sys_win_menubar_add_item(form, window_menu, "Font...", MENU_FONT) < 0) {
        sys_win_msgbox("Unable to create the menu.", "OK", "Terminal");
        terminal_cleanup();
        sys_exit();
        return;
    }

    gui_control_t bg_ctrl = {0};
    bg_ctrl.type = CTRL_PICTUREBOX;
    bg_ctrl.id = BG_CTRL_ID;
    bg_ctrl.x = BORDER;
    bg_ctrl.y = 0;
    bg_ctrl.w = win_w - BORDER * 2;
    bg_ctrl.h = win_h - TITLEBAR_H - MENUBAR_HEIGHT - BORDER;
    bg_ctrl.bg = 0;
    bg_ctrl.fg = 0;
    bg_ctrl.border = 0;
    sys_win_add_control(form, &bg_ctrl);

    gui_control_t canvas = {0};
    canvas.type = CTRL_PICTUREBOX;
    canvas.id = CANVAS_CTRL_ID;
    canvas.x = BORDER;
    canvas.bg = 0;
    sys_win_add_control(form, &canvas);

    gui_control_t bar = {0};
    bar.type = CTRL_SCROLLBAR | 0x80;
    bar.id = SCROLL_CTRL_ID;
    bar.x = win_w - BORDER - SCROLL_W;
    bar.y = 0;
    bar.w = SCROLL_W;
    bar.h = win_h - TITLEBAR_H - MENUBAR_HEIGHT - BORDER;
    sys_win_add_control(form, &bar);

    vc = sys_vc_create();
    if (!vc) {
        sys_win_destroy_form(form);
        usr_bmf_free(&font);
        sys_exit();
        return;
    }
    if (resize((gui_form_t*)form) != 0) {
        sys_win_msgbox("Unable to create the console.", "OK", "Terminal");
        terminal_cleanup();
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
        while (args[i] && args[i] != ' ' && args[i] != '\t' && j < (int)sizeof(spawn_path) - 1) {
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
        sys_win_msgbox("Unable to start the program.", "OK", "Terminal");
        terminal_cleanup();
        sys_exit();
        return;
    }

    if (sys_vc_attach(vc, shell_tid) != 0) {
        sys_win_msgbox("Unable to attach the console.", "OK", "Terminal");
        stop_shell();
        terminal_cleanup();
        sys_exit();
        return;
    }

    cursor_visible = 1;
    cursor_blink_tick = sys_uptime();

    int running = 1;
    while (running) {
        sys_win_event_t event;
        if (sys_win_pump_events(form, &event) != 0) {
            running = 0;
            continue;
        }

        if (event.type == SYS_WIN_EVENT_CLOSE) {
            stop_shell();
            running = 0;
            continue;
        }

        if (handle_event(form, &event, NULL) != 0)
            running = 0;

        sys_sleep(10);
    }

    terminal_cleanup();
    sys_exit();
}
