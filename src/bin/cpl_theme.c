#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdlib.h"
#include "../lib/ini.h"
#include "../lib/stdio.h"

#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"

/* From desktop.c */
extern void desktop_apply_settings(uint8_t color, const char *wallpaper);

/* Control IDs */
#define CTRL_FRAME_THEME     1
#define CTRL_FRAME_DESKTOP   2

#define CTRL_LBL_WINBG       10
#define CTRL_LBL_TITLEBAR    11
#define CTRL_LBL_TITLEBTN    12
#define CTRL_LBL_TASKBAR     13
#define CTRL_LBL_DESK_COLOR  14
#define CTRL_LBL_WALLPAPER   15

#define CTRL_DROP_WINBG      20
#define CTRL_DROP_TITLEBAR   21
#define CTRL_DROP_TITLEBTN   22
#define CTRL_DROP_TASKBAR    23
#define CTRL_DROP_DESKTOP    24

#define CTRL_TXT_WALLPAPER   30

#define CTRL_BTN_APPLY   50
#define CTRL_BTN_OK      51
#define CTRL_BTN_CANCEL  52

#define WIN_WIDTH  280
#define WIN_HEIGHT 245
#define WIN_X      180
#define WIN_Y      120

/* Color names for dropdown */
static const char *color_options =
    "Black|Dark Blue|Dark Green|Cyan|Brown|Dark Purple|Olive|Grey|"
    "Dark Grey|Blue|Green|Light Cyan|Red|Peach|Yellow|White";

typedef struct {
    void *form;
    uint8_t theme_winbg;
    uint8_t theme_titlebar;
    uint8_t theme_titlebtn;
    uint8_t theme_taskbar;
    uint8_t desktop_color;
    char wallpaper[128];
    /* Originals for cancel */
    uint8_t orig_theme_winbg;
    uint8_t orig_theme_titlebar;
    uint8_t orig_theme_titlebtn;
    uint8_t orig_theme_taskbar;
    uint8_t orig_desktop_color;
    char orig_wallpaper[128];
} cpl_theme_state_t;

static int cpl_theme_init(prog_instance_t *inst);
static int cpl_theme_event(prog_instance_t *inst, int win_idx, int event);
static void cpl_theme_cleanup(prog_instance_t *inst);

const progmod_t cpl_theme_module = {
    .name = "Theme",
    .icon_path = "C:/ICONS/THEME.ICO",
    .init = cpl_theme_init,
    .update = 0,
    .handle_event = cpl_theme_event,
    .cleanup = cpl_theme_cleanup,
    .flags = PROG_FLAG_SINGLETON
};

static void load_settings(cpl_theme_state_t *state) {
    sys_theme_t *theme = sys_win_get_theme();
    state->theme_winbg = theme->bg_color;
    state->theme_titlebar = theme->titlebar_color;
    state->theme_titlebtn = theme->button_color;
    state->theme_taskbar = theme->taskbar_color;
    state->desktop_color = theme->desktop_color;
    state->wallpaper[0] = '\0';

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) goto store_orig;

    char buffer[1024];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);

    if (bytes <= 0) goto store_orig;
    buffer[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buffer);

    const char *val;

    val = ini_get(&ini, "DESKTOP", "COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) state->desktop_color = (uint8_t)c;
    }

    val = ini_get(&ini, "DESKTOP", "WALLPAPER");
    if (val && val[0]) {
        strncpy(state->wallpaper, val, sizeof(state->wallpaper) - 1);
        state->wallpaper[sizeof(state->wallpaper) - 1] = '\0';
    }

    val = ini_get(&ini, "THEME", "BG_COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) state->theme_winbg = (uint8_t)c;
    }

    val = ini_get(&ini, "THEME", "TITLEBAR_COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) state->theme_titlebar = (uint8_t)c;
    }

    val = ini_get(&ini, "THEME", "BUTTON_COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) state->theme_titlebtn = (uint8_t)c;
    }

    val = ini_get(&ini, "THEME", "TASKBAR_COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) state->theme_taskbar = (uint8_t)c;
    }

    val = ini_get(&ini, "DESKTOP", "COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) state->desktop_color = (uint8_t)c;
    }

store_orig:
    state->orig_theme_winbg = state->theme_winbg;
    state->orig_theme_titlebar = state->theme_titlebar;
    state->orig_theme_titlebtn = state->theme_titlebtn;
    state->orig_theme_taskbar = state->theme_taskbar;
    state->orig_desktop_color = state->desktop_color;
    strncpy(state->orig_wallpaper, state->wallpaper, sizeof(state->orig_wallpaper));
}

static void save_settings(cpl_theme_state_t *state) {
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer),
        "[DESKTOP]\r\n"
        "COLOR=%d\r\n"
        "WALLPAPER=%s\r\n"
        "\r\n"
        "[THEME]\r\n"
        "BG_COLOR=%d\r\n"
        "TITLEBAR_COLOR=%d\r\n"
        "TITLEBAR_INACTIVE=8\r\n"
        "FRAME_DARK=8\r\n"
        "FRAME_LIGHT=7\r\n"
        "TEXT_COLOR=0\r\n"
        "BUTTON_COLOR=%d\r\n"
        "TASKBAR_COLOR=%d\r\n",
        state->desktop_color,
        state->wallpaper,
        state->theme_winbg,
        state->theme_titlebar,
        state->theme_titlebtn,
        state->theme_taskbar
    );

    int fd = sys_open(SETTINGS_PATH, "w");
    if (fd >= 0) {
        sys_write_file(fd, buffer, len);
        sys_close(fd);
    }
}

static void apply_theme(cpl_theme_state_t *state) {
    sys_theme_t *theme = sys_win_get_theme();
    theme->bg_color = state->theme_winbg;
    theme->titlebar_color = state->theme_titlebar;
    theme->button_color = state->theme_titlebtn;
    theme->taskbar_color = state->theme_taskbar;
}

/* Helper to add a label + dropdown pair */
static void add_dropdown_row(void *form, int y, const char *label, int lbl_id, int drop_id, uint8_t selected) {
    gui_control_t lbl = {0};
    lbl.type = CTRL_LABEL;
    lbl.x = 12;
    lbl.y = y;
    lbl.w = 80;
    lbl.h = 16;
    lbl.fg = 0;
    lbl.bg = 15;
    lbl.id = lbl_id;
    strncpy(lbl.text, label, sizeof(lbl.text) - 1);
    sys_win_add_control(form, &lbl);

    gui_control_t drop = {0};
    drop.type = CTRL_DROPDOWN;
    drop.x = 95;
    drop.y = y - 2;
    drop.w = 165;
    drop.h = 18;
    drop.fg = 0;
    drop.bg = 15;
    drop.id = drop_id;
    drop.cursor_pos = selected;  /* cursor_pos = selected_index */
    drop.item_count = 16;
    strncpy(drop.text, color_options, sizeof(drop.text) - 1);
    sys_win_add_control(form, &drop);
}

static int cpl_theme_init(prog_instance_t *inst) {
    cpl_theme_state_t *state = sys_malloc(sizeof(cpl_theme_state_t));
    if (!state) return -1;

    inst->user_data = state;
    load_settings(state);

    state->form = sys_win_create_form("Theme", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/THEME.ICO");

    /* Window Theme Frame */
    gui_control_t frame_theme = {0};
    frame_theme.type = CTRL_FRAME;
    frame_theme.x = 6;
    frame_theme.y = 5;
    frame_theme.w = 264;
    frame_theme.h = 105;
    frame_theme.fg = 0;
    frame_theme.bg = 15;
    frame_theme.id = CTRL_FRAME_THEME;
    strcpy(frame_theme.text, "Window Theme");
    sys_win_add_control(state->form, &frame_theme);

    /* Theme dropdowns */
    add_dropdown_row(state->form, 25, "Window BG:", CTRL_LBL_WINBG, CTRL_DROP_WINBG, state->theme_winbg);
    add_dropdown_row(state->form, 47, "Title Bar:", CTRL_LBL_TITLEBAR, CTRL_DROP_TITLEBAR, state->theme_titlebar);
    add_dropdown_row(state->form, 69, "Title Btn:", CTRL_LBL_TITLEBTN, CTRL_DROP_TITLEBTN, state->theme_titlebtn);
    add_dropdown_row(state->form, 91, "Taskbar:", CTRL_LBL_TASKBAR, CTRL_DROP_TASKBAR, state->theme_taskbar);

    /* Desktop Frame */
    gui_control_t frame_desk = {0};
    frame_desk.type = CTRL_FRAME;
    frame_desk.x = 6;
    frame_desk.y = 115;
    frame_desk.w = 264;
    frame_desk.h = 75;
    frame_desk.fg = 0;
    frame_desk.bg = 15;
    frame_desk.id = CTRL_FRAME_DESKTOP;
    strcpy(frame_desk.text, "Desktop");
    sys_win_add_control(state->form, &frame_desk);

    /* Desktop color dropdown */
    add_dropdown_row(state->form, 135, "Color:", CTRL_LBL_DESK_COLOR, CTRL_DROP_DESKTOP, state->desktop_color);

    /* Wallpaper label and textbox */
    gui_control_t lbl_wp = {0};
    lbl_wp.type = CTRL_LABEL;
    lbl_wp.x = 12;
    lbl_wp.y = 162;
    lbl_wp.w = 60;
    lbl_wp.h = 14;
    lbl_wp.fg = 0;
    lbl_wp.bg = 15;
    lbl_wp.id = CTRL_LBL_WALLPAPER;
    strcpy(lbl_wp.text, "Wallpaper:");
    sys_win_add_control(state->form, &lbl_wp);

    gui_control_t txt_wp = {0};
    txt_wp.type = CTRL_TEXTBOX;
    txt_wp.x = 75;
    txt_wp.y = 159;
    txt_wp.w = 185;
    txt_wp.h = 18;
    txt_wp.fg = 0;
    txt_wp.bg = 15;
    txt_wp.id = CTRL_TXT_WALLPAPER;
    txt_wp.max_length = 127;
    strncpy(txt_wp.text, state->wallpaper, sizeof(txt_wp.text) - 1);
    sys_win_add_control(state->form, &txt_wp);

    /* Buttons */
    gui_control_t btn_apply = {0};
    btn_apply.type = CTRL_BUTTON;
    btn_apply.x = 30;
    btn_apply.y = 195;
    btn_apply.w = 65;
    btn_apply.h = 22;
    btn_apply.fg = 0;
    btn_apply.bg = 7;
    btn_apply.id = CTRL_BTN_APPLY;
    strcpy(btn_apply.text, "Apply");
    sys_win_add_control(state->form, &btn_apply);

    gui_control_t btn_ok = {0};
    btn_ok.type = CTRL_BUTTON;
    btn_ok.x = 105;
    btn_ok.y = 195;
    btn_ok.w = 65;
    btn_ok.h = 22;
    btn_ok.fg = 0;
    btn_ok.bg = 7;
    btn_ok.id = CTRL_BTN_OK;
    strcpy(btn_ok.text, "OK");
    sys_win_add_control(state->form, &btn_ok);

    gui_control_t btn_cancel = {0};
    btn_cancel.type = CTRL_BUTTON;
    btn_cancel.x = 180;
    btn_cancel.y = 195;
    btn_cancel.w = 65;
    btn_cancel.h = 22;
    btn_cancel.fg = 0;
    btn_cancel.bg = 7;
    btn_cancel.id = CTRL_BTN_CANCEL;
    strcpy(btn_cancel.text, "Cancel");
    sys_win_add_control(state->form, &btn_cancel);

    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

/* Get selected value from dropdown */
static uint8_t get_dropdown_value(void *form, int ctrl_id) {
    gui_control_t *ctrl = sys_win_get_control(form, ctrl_id);
    if (ctrl) {
        return (uint8_t)ctrl->cursor_pos;
    }
    return 0;
}

static int cpl_theme_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;
    cpl_theme_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    if (event == -1 || event == -2)
        return PROG_EVENT_REDRAW;

    /* Dropdown selection changed - just redraw (values read on apply) */
    if (event == CTRL_DROP_WINBG || event == CTRL_DROP_TITLEBAR ||
        event == CTRL_DROP_TITLEBTN || event == CTRL_DROP_TASKBAR ||
        event == CTRL_DROP_DESKTOP) {
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Apply button */
    if (event == CTRL_BTN_APPLY) {
        /* Read values from dropdowns */
        state->theme_winbg = get_dropdown_value(state->form, CTRL_DROP_WINBG);
        state->theme_titlebar = get_dropdown_value(state->form, CTRL_DROP_TITLEBAR);
        state->theme_titlebtn = get_dropdown_value(state->form, CTRL_DROP_TITLEBTN);
        state->theme_taskbar = get_dropdown_value(state->form, CTRL_DROP_TASKBAR);
        state->desktop_color = get_dropdown_value(state->form, CTRL_DROP_DESKTOP);

        const char *wp = ctrl_get_text(state->form, CTRL_TXT_WALLPAPER);
        if (wp) {
            strncpy(state->wallpaper, wp, sizeof(state->wallpaper) - 1);
            state->wallpaper[sizeof(state->wallpaper) - 1] = '\0';
        }

        apply_theme(state);
        save_settings(state);
        desktop_apply_settings(state->desktop_color, state->wallpaper);

        state->orig_theme_winbg = state->theme_winbg;
        state->orig_theme_titlebar = state->theme_titlebar;
        state->orig_theme_titlebtn = state->theme_titlebtn;
        state->orig_theme_taskbar = state->theme_taskbar;
        state->orig_desktop_color = state->desktop_color;
        strncpy(state->orig_wallpaper, state->wallpaper, sizeof(state->orig_wallpaper));

        return PROG_EVENT_HANDLED;
    }

    /* OK button */
    if (event == CTRL_BTN_OK) {
        /* Read values from dropdowns */
        state->theme_winbg = get_dropdown_value(state->form, CTRL_DROP_WINBG);
        state->theme_titlebar = get_dropdown_value(state->form, CTRL_DROP_TITLEBAR);
        state->theme_titlebtn = get_dropdown_value(state->form, CTRL_DROP_TITLEBTN);
        state->theme_taskbar = get_dropdown_value(state->form, CTRL_DROP_TASKBAR);
        state->desktop_color = get_dropdown_value(state->form, CTRL_DROP_DESKTOP);

        const char *wp = ctrl_get_text(state->form, CTRL_TXT_WALLPAPER);
        if (wp) {
            strncpy(state->wallpaper, wp, sizeof(state->wallpaper) - 1);
            state->wallpaper[sizeof(state->wallpaper) - 1] = '\0';
        }

        apply_theme(state);
        save_settings(state);
        desktop_apply_settings(state->desktop_color, state->wallpaper);
        return PROG_EVENT_CLOSE;
    }

    /* Cancel button */
    if (event == CTRL_BTN_CANCEL) {
        state->theme_winbg = state->orig_theme_winbg;
        state->theme_titlebar = state->orig_theme_titlebar;
        state->theme_titlebtn = state->orig_theme_titlebtn;
        state->theme_taskbar = state->orig_theme_taskbar;
        apply_theme(state);
        return PROG_EVENT_CLOSE;
    }

    return PROG_EVENT_NONE;
}

static void cpl_theme_cleanup(prog_instance_t *inst) {
    cpl_theme_state_t *state = inst->user_data;
    if (state) {
        if (state->form) {
            prog_unregister_window(inst, state->form);
            sys_win_destroy_form(state->form);
        }
        sys_free(state);
        inst->user_data = 0;
    }
}
