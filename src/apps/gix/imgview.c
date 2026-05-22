#include "../../syscall.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../lib/pathdlg.h"
#include "../../lib/app.h"
#include "../../lib/gix_app.h"
#include "../../drivers/keyboard.h"
#include "../../win/wm_config.h"

OSLET_APP("Image Viewer", OSLET_KIND_GIX, "C:/ICONS/VIEWER.ICO", OSLET_APP_FLAG_NONE);

/* Control IDs */
#define ID_PICTURE    1
#define ID_PREV       6
#define ID_NEXT       7
#define ID_FULLSCREEN 8

/* Controls for Form1 */
static gui_control_t Form1_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 8, .y = 5, .w = 467, .h = 350, .fg = 0, .bg = 7, .text = "", .id = ID_PICTURE, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 174, .y = 370, .w = 22, .h = 22, .fg = 0, .bg = -1, .text = "", .id = ID_PREV, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 244, .y = 370, .w = 22, .h = 22, .fg = 0, .bg = -1, .text = "", .id = ID_NEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 224, .y = 360, .w = 40, .h = 40, .fg = 0, .bg = -1, .text = "", .id = ID_FULLSCREEN, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

static gui_control_t fullscreen_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 0, .y = 0, .w = WM_SCREEN_WIDTH, .h = WM_SCREEN_HEIGHT, .fg = 0, .bg = 7, .text = "", .id = ID_PICTURE, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

typedef struct {
    void *form;
    int fullscreen;
    void *fs_form;
} imgview_state_t;

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

static void toggle_fullscreen(imgview_state_t *state, const char *cur_path) {
    if (!cur_path || !cur_path[0]) return;

    if (!state->fullscreen) {
        state->fullscreen = 1;
        sys_mouse_invalidate();

        gix_app_window_desc_t fs_desc = {
            .title = "Image Viewer (full screen)",
            .icon_path = "C:/ICONS/VIEWER.ICO",
            .x = 0,
            .y = -WM_TITLEBAR_HEIGHT - 2,
            .w = WM_SCREEN_WIDTH + 2,
            .h = WM_SCREEN_HEIGHT + WM_TITLEBAR_HEIGHT + 4,
            .resizable = 0,
            .controls = fullscreen_controls,
            .control_count = sizeof(fullscreen_controls) / sizeof(fullscreen_controls[0])
        };

        void *fs = gix_app_create_window(&fs_desc);
        if (fs) {
            ctrl_set_image(fs, ID_PICTURE, cur_path);
            sys_win_draw(fs);
            sys_win_force_full_redraw();
            state->fs_form = fs;
        } else {
            /* fallback to direct draw */
            sys_gfx_fillrect(0, 0, WM_SCREEN_WIDTH, WM_SCREEN_HEIGHT, COLOR_DARK_GRAY);
            sys_gfx_load_bmp_scaled(cur_path, 0, 0, WM_SCREEN_WIDTH, WM_SCREEN_HEIGHT);
            sys_mouse_invalidate();
            sys_gfx_swap();
            state->fs_form = NULL;
        }

    } else {
        state->fullscreen = 0;
        if (state->fs_form) {
            gix_app_destroy_window(state->fs_form);
            state->fs_form = NULL;
        }

        sys_win_restore_form(state->form);
        sys_win_draw(state->form);
        sys_win_force_full_redraw();
        sys_mouse_invalidate();
        sys_gfx_swap();
    }
}

static void update_layout(void *form) {
    gui_form_t *f = form;
    int win_w = f->win.w;
    int win_h = f->win.h;
    
    /* Picturebox fills most of the window */
    int pic_w = win_w - 16;
    int pic_h = win_h - 77;
    if (pic_w < 100) pic_w = 100;
    if (pic_h < 100) pic_h = 100;
    
    sys_ctrl_set_prop(form, ID_PICTURE, PROP_W, pic_w);
    sys_ctrl_set_prop(form, ID_PICTURE, PROP_H, pic_h);
    
    /* Position buttons at bottom, centered */
    int btn_y = win_h - 58;
    int center_x = win_w / 2;
    
    sys_ctrl_set_prop(form, ID_PREV, PROP_X, center_x - 47);
    sys_ctrl_set_prop(form, ID_PREV, PROP_Y, btn_y);
    
    sys_ctrl_set_prop(form, ID_NEXT, PROP_X, center_x + 25);
    sys_ctrl_set_prop(form, ID_NEXT, PROP_Y, btn_y);
    
    sys_ctrl_set_prop(form, ID_FULLSCREEN, PROP_X, center_x - 20);
    sys_ctrl_set_prop(form, ID_FULLSCREEN, PROP_Y, btn_y - 9);
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

static void imgview_init(void *form, void *userdata) {
    imgview_state_t *state = userdata;
    state->form = form;
    state->fullscreen = 0;
    state->fs_form = NULL;
    
    ctrl_set_image(form, ID_FULLSCREEN, "C:/ICONS/VIEWER.ICO");
    ctrl_set_image(form, ID_PREV, "C:/ICONS/ARL.ICO");
    ctrl_set_image(form, ID_NEXT, "C:/ICONS/ARR.ICO");

    update_layout(form);

    /* Load image from launch arguments if provided */
    char args[256];
    if (sys_getargs(args, sizeof(args)) && args[0]) {
        ctrl_set_image(form, ID_PICTURE, args);
    }
}

static void imgview_resize(void *form, void *userdata) {
    (void)userdata;
    update_layout(form);
}

static int imgview_event(void *form, int ev, void *userdata) {
    imgview_state_t *state = userdata;

    if (ev == -3 && form == state->fs_form) {
        const char *cur = ctrl_get_text(state->fs_form, ID_PICTURE);
        toggle_fullscreen(state, cur);
        return 0;
    }

    if (form != state->form || ev <= 0) return 0;

    if (ev == ID_PICTURE) {
        char newpath[256];
        const char *cur = ctrl_get_text(state->form, ID_PICTURE);
        if (gui_show_path_dialog("Open", cur, newpath, sizeof(newpath))) {
            ctrl_set_image(state->form, ID_PICTURE, newpath);
            sys_win_draw(state->form);
        }
    } else if (ev == ID_NEXT || ev == ID_PREV) {
        navigate_dir(state->form, (ev == ID_NEXT) ? 1 : -1, state->fullscreen, state->fs_form);
    } else if (ev == ID_FULLSCREEN) {
        const char *cur = ctrl_get_text(state->form, ID_PICTURE);
        toggle_fullscreen(state, cur);
    }

    return 0;
}

static void imgview_tick(void *form, void *userdata) {
    imgview_state_t *state = userdata;
    int k = 0;
    (void)form;

    if (state->fullscreen && state->fs_form) {
        if (sys_win_is_focused(state->fs_form)) {
            k = sys_get_key_nonblock();
        }
    } else if (state->form && sys_win_is_focused(state->form)) {
        k = sys_get_key_nonblock();
    }

    if (k == KEY_LEFT) {
        navigate_dir(state->form, -1, state->fullscreen, state->fs_form);
    } else if (k == KEY_RIGHT) {
        navigate_dir(state->form, 1, state->fullscreen, state->fs_form);
    } else if (k == '\n' || k == '\r') {
        const char *cur = state->form ? ctrl_get_text(state->form, ID_PICTURE) :
                          (state->fs_form ? ctrl_get_text(state->fs_form, ID_PICTURE) : "");
        toggle_fullscreen(state, cur);
    } else if (k == KEY_ESC) {
        if (state->fullscreen) {
            const char *cur = state->form ? ctrl_get_text(state->form, ID_PICTURE) :
                              (state->fs_form ? ctrl_get_text(state->fs_form, ID_PICTURE) : "");
            toggle_fullscreen(state, cur);
        }
    }
}

static void imgview_cleanup(void *form, void *userdata) {
    imgview_state_t *state = userdata;
    (void)form;

    if (state->fs_form) {
        gix_app_destroy_window(state->fs_form);
        state->fs_form = NULL;
    }
}

__attribute__((section(".entry"), used))
void _start(void) {
    static imgview_state_t state;
    static gix_app_desc_t app = {
        .title = "Image Viewer",
        .icon_path = "C:/ICONS/VIEWER.ICO",
        .x = 240,
        .y = 87,
        .w = 400,
        .h = 366,
        .resizable = 1,
        .controls = Form1_controls,
        .control_count = sizeof(Form1_controls) / sizeof(Form1_controls[0]),
        .on_init = imgview_init,
        .on_resize = imgview_resize,
        .on_event = imgview_event,
        .on_tick = imgview_tick,
        .on_cleanup = imgview_cleanup,
        .userdata = &state
    };

    gix_app_run(&app);
}
