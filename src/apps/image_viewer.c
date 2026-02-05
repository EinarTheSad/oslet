#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
#include "../drivers/keyboard.h"
#include "../win/wm_config.h"

/* Control IDs */
#define ID_PICTURE    1
#define ID_PREV       6
#define ID_NEXT       7
#define ID_FULLSCREEN 8

/* Form2 IDs */
#define ID_PATH_TEXT  1
#define ID_OK         3
#define ID_CANCEL     4

/* Controls for Form1 */
static gui_control_t Form1_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 8, .y = 5, .w = 467, .h = 350, .fg = 0, .bg = 7, .text = "", .id = ID_PICTURE, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_BUTTON, .x = 126, .y = 369, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "Previous", .id = ID_PREV, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_BUTTON, .x = 292, .y = 369, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "Next", .id = ID_NEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_BUTTON, .x = 203, .y = 360, .w = 82, .h = 40, .fg = 0, .bg = -1, .text = "Full screen", .id = ID_FULLSCREEN, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 }
};

/* Controls for Form2 */
static gui_control_t Form2_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 46, .y = 5, .w = 198, .h = 20, .fg = 0, .bg = -1, .text = "", .id = ID_PATH_TEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_LABEL, .x = 9, .y = 6, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "Path:", .id = 2, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_BUTTON, .x = 56, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "OK", .id = ID_OK, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 },
    { .type = CTRL_BUTTON, .x = 130, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "Cancel", .id = ID_CANCEL, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .cached_bitmap_orig = NULL, .cached_bitmap_scaled = NULL, .pressed = 0, .checked = 0, .group_id = 0, .cursor_pos = 0, .max_length = 255, .scroll_offset = 0, .is_focused = 0, .sel_start = -1, .sel_end = -1, .dropdown_open = 0, .item_count = 0, .hovered_item = -1 }
};

static int gather_bmps(const char *dir, sys_dirent_t *entries, int max_entries) {
    int n = sys_readdir(dir, entries, max_entries);
    if (n <= 0) return 0;
    int out = 0;
    for (int i = 0; i < n && out < max_entries; i++) {
        if (!entries[i].is_directory) {
            if (str_ends_with_icase(entries[i].name, ".bmp")) {
                entries[out++] = entries[i];
            }
        }
    }
    return out;
}

static void toggle_fullscreen(void *form, int *fullscreen_ptr, const char *cur_path, void **fs_form_ptr) {
    if (!cur_path || !cur_path[0]) return;

    if (!*fullscreen_ptr) {
        *fullscreen_ptr = 1;
        sys_mouse_invalidate();

        /* Create a full-screen form positioned so its titlebar is off-screen to hide decorations. */
        void *fs = sys_win_create_form("Image Viewer (full screen)", 0, -WM_TITLEBAR_HEIGHT-2, WM_SCREEN_WIDTH+2, WM_SCREEN_HEIGHT + WM_TITLEBAR_HEIGHT+4);
        if (fs) {
            /* Add a picturebox control that fills the client area (client area height = WM_SCREEN_HEIGHT) */
            gui_control_t pic = {0};
            pic.type = CTRL_PICTUREBOX;
            pic.x = 0;
            pic.y = 0;
            pic.w = WM_SCREEN_WIDTH;
            pic.h = WM_SCREEN_HEIGHT;
            pic.fg = 0;
            pic.bg = 7;
            pic.id = ID_PICTURE;
            pic.border = 0;

            sys_win_add_control(fs, &pic);
            sys_win_set_icon(fs, "C:/ICONS/VIEWER.ICO");

            ctrl_set_image(fs, ID_PICTURE, cur_path);

            sys_win_draw(fs);
            sys_win_redraw_all();

            *fs_form_ptr = fs;
        } else {
            /* fallback to direct draw */
            sys_gfx_fillrect(0, 0, WM_SCREEN_WIDTH, WM_SCREEN_HEIGHT, COLOR_DARK_GRAY);
            sys_gfx_load_bmp_scaled(cur_path, 0, 0, WM_SCREEN_WIDTH, WM_SCREEN_HEIGHT);
            sys_mouse_invalidate();
            sys_gfx_swap();
            *fs_form_ptr = NULL;
        }

    } else {
        *fullscreen_ptr = 0;
        if (fs_form_ptr && *fs_form_ptr) {
            sys_win_destroy_form(*fs_form_ptr);
            *fs_form_ptr = NULL;
        }

        sys_win_restore_form(form);
        sys_win_draw(form);
        sys_win_force_full_redraw();
        sys_mouse_invalidate();
        sys_gfx_swap();
    }
}

/* Helper: navigate directory by delta (-1 previous, +1 next). If fullscreen is true
   the function will also redraw the fullscreen overlay with the new image. */
static void navigate_dir(void *form, int delta, int fullscreen, void *fs_form) {
    const char *cur = NULL;
    /* Prefer main form text, but if the main form is destroyed while fullscreen, read from fs_form */
    if (form) cur = ctrl_get_text(form, ID_PICTURE);
    if (!cur && fs_form) cur = ctrl_get_text(fs_form, ID_PICTURE);
    if (!cur || !cur[0]) return;

    const char *last_sep_ptr = strrchr(cur, '/');
    if (!last_sep_ptr || last_sep_ptr == cur) return;

    char dir[256];
    int dirlen = last_sep_ptr - cur;
    if (dirlen >= (int)sizeof(dir) - 1) return;
    for (int i = 0; i < dirlen; i++) dir[i] = cur[i];
    dir[dirlen] = '\0';

    sys_dirent_t entries[256];
    int total = gather_bmps(dir, entries, 256);
    if (total <= 0) return;

    const char *filename = last_sep_ptr + 1;
    int idx = -1;
    for (int i = 0; i < total; i++) {
        if (strcasecmp(entries[i].name, filename) == 0) { idx = i; break; }
    }
    
    if (idx == -1) {
        idx = (delta > 0) ? -1 : total;
    }
    
    if (delta > 0) {
        idx = (idx + 1) % total;
    } else {
        idx = (idx - 1 + total) % total;
    }

    char newpath[256];
    int n = snprintf(newpath, sizeof(newpath), "%s/%s", dir, entries[idx].name);
    if (n > 0 && n < (int)sizeof(newpath)) {
        /* Always update the main form to keep the path in sync */
        if (form) {
            ctrl_set_image(form, ID_PICTURE, newpath);
        }
        
        /* If fullscreen overlay is active, also update and draw it */
        if (fullscreen && fs_form) {
            ctrl_set_image(fs_form, ID_PICTURE, newpath);
            sys_win_draw(fs_form);
            sys_mouse_invalidate();
        } else {
            /* Windowed mode: draw the main form */
            if (form) {
                sys_win_draw(form);
            }
            sys_mouse_invalidate();
        }
    }
}

__attribute__((section(".entry"), used))
void _start(void) {
    void *form = sys_win_create_form("Image Viewer", 78, 16, 483, 427);
    if (!form) {
        sys_exit();
        return;
    }

    for (int i = 0; i < (int)(sizeof(Form1_controls) / sizeof(Form1_controls[0])); i++) {
        sys_win_add_control(form, &Form1_controls[i]);
    }

    sys_win_set_icon(form, "C:/ICONS/VIEWER.ICO");
    sys_win_draw(form);
    sys_win_force_full_redraw();

    int running = 1;
    int fullscreen = 0;
    void *fs_form = NULL;

    while (running) {
        int ev = sys_win_pump_events(form);

        if (fs_form) {
            int ev_fs = sys_win_pump_events(fs_form);
            if (ev_fs == -3) {
                const char *cur = fs_form ? ctrl_get_text(fs_form, ID_PICTURE) : ctrl_get_text(form, ID_PICTURE);
                toggle_fullscreen(form, &fullscreen, cur, &fs_form);
            }
        }
        if (ev == -3) {
            running = 0;
            continue;
        }
        if (ev == -1 || ev == -2) {
            sys_win_draw(form);
            sys_win_force_full_redraw();
        }

        if (ev > 0) {
            if (ev == ID_PICTURE) {
                /* Open small dialog to enter path */
                void *dlg = sys_win_create_form("Open", 193, 199, 255, 82);
                if (!dlg) continue;
                for (int i = 0; i < (int)(sizeof(Form2_controls) / sizeof(Form2_controls[0])); i++) {
                    sys_win_add_control(dlg, &Form2_controls[i]);
                }

                /* Prefill textbox with current image path */
                const char *cur = ctrl_get_text(form, ID_PICTURE);
                if (cur && cur[0]) {
                    ctrl_set_text(dlg, ID_PATH_TEXT, cur);
                }
                sys_win_draw(dlg);
                sys_win_redraw_all();
                sys_mouse_invalidate();

                int dlg_running = 1;
                while (dlg_running) {
                    int ev2 = sys_win_pump_events(dlg);
                    if (ev2 == -3) {
                        sys_win_destroy_form(dlg);
                        dlg_running = 0;
                        break;
                    }
                    if (ev2 == -1 || ev2 == -2) {
                        sys_win_draw(dlg);
                        sys_win_redraw_all();
                    }
                    if (ev2 > 0) {
                        if (ev2 == ID_OK) {
                            const char *newpath = ctrl_get_text(dlg, ID_PATH_TEXT);
                            if (newpath && newpath[0]) {
                                ctrl_set_image(form, ID_PICTURE, newpath);
                                sys_win_draw(form);
                            }
                            sys_win_destroy_form(dlg);
                            dlg_running = 0;
                            break;
                        } else if (ev2 == ID_CANCEL) {
                            sys_win_destroy_form(dlg);
                            dlg_running = 0;
                            break;
                        }
                    }
                    sys_yield();
                }
            } else if (ev == ID_NEXT || ev == ID_PREV) {
                navigate_dir(form, (ev == ID_NEXT) ? 1 : -1, fullscreen, fs_form);
            } else if (ev == ID_FULLSCREEN) {
                const char *cur = form ? ctrl_get_text(form, ID_PICTURE) : (fs_form ? ctrl_get_text(fs_form, ID_PICTURE) : "");
                toggle_fullscreen(form, &fullscreen, cur, &fs_form);
            }
        }

    /* Arrow-key navigation and fullscreen control: left = previous, right = next, Enter toggles, Esc exits */
        {
            int k = 0;
            /* Only read/consume keys when our window is focused (nonblocking) */
            if (fullscreen && fs_form) {
                if (sys_win_is_focused(fs_form)) {
                    k = sys_get_key_nonblock();
                }
            } else {
                if (sys_win_is_focused(form)) {
                    k = sys_get_key_nonblock();
                }
            }

            if (k == KEY_LEFT) {
                navigate_dir(form, -1, fullscreen, fs_form);
            } else if (k == KEY_RIGHT) {
                navigate_dir(form, 1, fullscreen, fs_form);
            } else if (k == '\n' || k == '\r') {
                const char *cur = form ? ctrl_get_text(form, ID_PICTURE) : (fs_form ? ctrl_get_text(fs_form, ID_PICTURE) : "");
                toggle_fullscreen(form, &fullscreen, cur, &fs_form);
            } else if (k == KEY_ESC) {
                if (fullscreen) {
                    const char *cur = form ? ctrl_get_text(form, ID_PICTURE) : (fs_form ? ctrl_get_text(fs_form, ID_PICTURE) : "");
                    toggle_fullscreen(form, &fullscreen, cur, &fs_form);
                }
            }
        }
        sys_yield();
    }

    if (fs_form) {
        sys_win_destroy_form(fs_form);
        fs_form = NULL;
    }

    if (form) {
        sys_win_destroy_form(form);
        form = NULL;
    }

    sys_exit();
}
