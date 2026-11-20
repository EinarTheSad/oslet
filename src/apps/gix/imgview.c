#include "../../syscall.h"
#include "../../lib/stdio.h"
#include "../../lib/string.h"
#include "../../lib/pathdlg.h"
#include "../../lib/app.h"
#include "../../lib/gix_app.h"
#include "../../lib/display.h"
#include "../../drivers/keyboard.h"
#include "../../win/wm_config.h"

OSLET_APP("Image Viewer", OSLET_KIND_GIX, "C:/ICONS/VIEWER.ICO", OSLET_APP_FLAG_NONE);

/* Control IDs */
#define ID_PICTURE    1
#define ID_PREV       6
#define ID_NEXT       7
#define ID_FULLSCREEN 8
#define IMGVIEW_FILE_FILTER "*.bmp;*.ico"
#define IMGVIEW_MAX_ENTRIES 256
#define IMGVIEW_PATH_MAX    256

/* Controls for Form1 */
static gui_control_t Form1_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 8, .y = 5, .w = 467, .h = 350, .fg = 0, .bg = 7, .text = "", .id = ID_PICTURE, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 174, .y = 370, .w = 22, .h = 22, .fg = 0, .bg = -1, .text = "", .id = ID_PREV, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 244, .y = 370, .w = 22, .h = 22, .fg = 0, .bg = -1, .text = "", .id = ID_NEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 224, .y = 360, .w = 40, .h = 40, .fg = 0, .bg = -1, .text = "", .id = ID_FULLSCREEN, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

static gui_control_t fullscreen_controls[] = {
    { .type = CTRL_PICTUREBOX, .x = 0, .y = 0, .w = 640, .h = 480, .fg = 0, .bg = 7, .text = "", .id = ID_PICTURE, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

typedef struct {
    void *form;
    int fullscreen;
    void *fs_form;
    char image_path[IMGVIEW_PATH_MAX];
    sys_dirent_t entries[IMGVIEW_MAX_ENTRIES];
} imgview_state_t;

static int gather_images(imgview_state_t *state, const char *dir) {
    int count = sys_readdir(dir, state->entries, IMGVIEW_MAX_ENTRIES);
    int out = 0;

    if (count <= 0) return 0;
    if (count > IMGVIEW_MAX_ENTRIES) count = IMGVIEW_MAX_ENTRIES;

    for (int i = 0; i < count; i++) {
        sys_dirent_t *entry = &state->entries[i];
        if (!entry->is_directory &&
            (str_ends_with_icase(entry->name, ".bmp") ||
             str_ends_with_icase(entry->name, ".ico"))) {
            if (out != i) state->entries[out] = *entry;
            out++;
        }
    }

    return out;
}

static void draw_current_image(imgview_state_t *state) {
    void *form = state->fullscreen ? state->fs_form : state->form;

    if (!form || !state->image_path[0]) return;

    ctrl_set_image(form, ID_PICTURE, state->image_path);
    sys_win_draw(form);
    sys_mouse_invalidate();
}

static void set_current_image(imgview_state_t *state, const char *path) {
    if (!path || !path[0] || strlen(path) >= sizeof(state->image_path)) return;

    strcpy(state->image_path, path);
    draw_current_image(state);
}

static void enter_fullscreen(imgview_state_t *state) {
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

    if (!state->image_path[0]) return;

    state->fs_form = gix_app_create_window(&fs_desc);
    if (!state->fs_form) return;

    state->fullscreen = 1;
    draw_current_image(state);
    sys_win_force_full_redraw();
}

static void exit_fullscreen(imgview_state_t *state) {
    void *fs_form = state->fs_form;

    state->fullscreen = 0;
    state->fs_form = NULL;
    if (fs_form) gix_app_destroy_window(fs_form);

    sys_win_restore_form(state->form);
    draw_current_image(state);
    sys_win_force_full_redraw();
    sys_gfx_swap();
}

static void toggle_fullscreen(imgview_state_t *state) {
    if (state->fullscreen) {
        exit_fullscreen(state);
    } else {
        enter_fullscreen(state);
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

static void navigate_dir(imgview_state_t *state, int delta) {
    const char *slash;
    char dir[IMGVIEW_PATH_MAX];
    char new_path[IMGVIEW_PATH_MAX];
    int dir_len;
    int total;
    int idx = -1;

    slash = strrchr(state->image_path, '/');
    if (!slash || slash == state->image_path) return;

    dir_len = slash - state->image_path;
    if (dir_len <= 0 || dir_len >= (int)sizeof(dir)) return;
    for (int i = 0; i < dir_len; i++) dir[i] = state->image_path[i];
    dir[dir_len] = '\0';

    total = gather_images(state, dir);
    if (total == 0) return;

    for (int i = 0; i < total; i++) {
        if (strcasecmp(state->entries[i].name, slash + 1) == 0) {
            idx = i;
            break;
        }
    }

    if (idx == -1) {
        idx = delta > 0 ? 0 : total - 1;
    } else {
        idx = delta > 0 ? (idx + 1) % total : (idx - 1 + total) % total;
    }

    int length = snprintf(new_path, sizeof(new_path), "%s/%s",
                          dir, state->entries[idx].name);
    if (length > 0 && length < (int)sizeof(new_path))
        set_current_image(state, new_path);
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
        set_current_image(state, args);
    }
}

static void imgview_resize(void *form, void *userdata) {
    (void)userdata;
    update_layout(form);
}

static int imgview_event(void *form, const sys_win_event_t *event, void *userdata) {
    imgview_state_t *state = userdata;

    if (event->type == SYS_WIN_EVENT_CLOSE && form == state->fs_form) {
        exit_fullscreen(state);
        return 0;
    }

    if (form != state->form || event->type != SYS_WIN_EVENT_CONTROL) return 0;

    if (event->id == ID_PICTURE) {
        char new_path[IMGVIEW_PATH_MAX];
        char initial[IMGVIEW_PATH_MAX] = "C:/" IMGVIEW_FILE_FILTER;
        if (state->image_path[0]) {
            strncpy(initial, state->image_path, sizeof(initial) - 1);
            initial[sizeof(initial) - 1] = '\0';
            char *slash = strrchr(initial, '/');
            if (slash && slash >= initial + 2) {
                *(slash + 1) = '\0';
                if (strlen(initial) + strlen(IMGVIEW_FILE_FILTER) < sizeof(initial))
                    strcat(initial, IMGVIEW_FILE_FILTER);
            } else {
                strcpy(initial, "C:/" IMGVIEW_FILE_FILTER);
            }
        }
        if (gui_show_path_dialog_filtered("Open", initial, IMGVIEW_FILE_FILTER,
                                          new_path, sizeof(new_path)))
            set_current_image(state, new_path);
    } else if (event->id == ID_NEXT || event->id == ID_PREV) {
        navigate_dir(state, event->id == ID_NEXT ? 1 : -1);
    } else if (event->id == ID_FULLSCREEN) {
        toggle_fullscreen(state);
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
        navigate_dir(state, -1);
    } else if (k == KEY_RIGHT) {
        navigate_dir(state, 1);
    } else if (k == '\n' || k == '\r') {
        toggle_fullscreen(state);
    } else if (k == KEY_ESC) {
        if (state->fullscreen) exit_fullscreen(state);
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

    if (oslet_display_init() != 0) {
        sys_win_msgbox("Unable to query display dimensions.", "OK", "Image Viewer");
        sys_exit();
        return;
    }
    app.x = (WM_SCREEN_WIDTH - app.w) / 2;
    app.y = (WM_SCREEN_HEIGHT - app.h) / 2;
    fullscreen_controls[0].w = WM_SCREEN_WIDTH;
    fullscreen_controls[0].h = WM_SCREEN_HEIGHT;
    gix_app_run(&app);
}
