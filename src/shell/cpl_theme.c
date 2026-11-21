#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdlib.h"
#include "../lib/ini.h"
#include "../lib/stdio.h"

#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"

/* Control IDs */
#define CTRL_FRAME_THEME     1
#define CTRL_FRAME_DESKTOP   2

#define CTRL_LBL_WINBG       10
#define CTRL_LBL_TITLEBAR    11
#define CTRL_LBL_TITLEBTN    12
#define CTRL_LBL_TASKBAR     13
#define CTRL_LBL_DESK_COLOR  14

#define CTRL_LBL_STARTBTN    15
#define CTRL_LBL_ICON_TEXT   16
#define CTRL_LBL_TEXT        17
#define CTRL_LBL_CONTROL     18
#define CTRL_LBL_FRAME_DARK 30
#define CTRL_LBL_FRAME_LIGHT 31
#define CTRL_LBL_FRAME_SHINE 32
#define CTRL_LBL_HIGHLIGHT   33
#define CTRL_LBL_HOVER       34

#define CTRL_DROP_WINBG      20
#define CTRL_DROP_TITLEBAR   21
#define CTRL_DROP_TITLEBTN   22
#define CTRL_DROP_TASKBAR    23
#define CTRL_DROP_STARTBTN   24
#define CTRL_DROP_ICON_TEXT  25
#define CTRL_DROP_TEXT       26
#define CTRL_DROP_CONTROL    27
#define CTRL_DROP_FRAME_DARK 35
#define CTRL_DROP_FRAME_LIGHT 36
#define CTRL_DROP_FRAME_SHINE 37
#define CTRL_DROP_HIGHLIGHT  38
#define CTRL_DROP_HOVER      39

#define CTRL_BTN_APPLY   50
#define CTRL_BTN_OK      51
#define CTRL_BTN_CANCEL  52

#define WIN_WIDTH  274
#define WIN_HEIGHT 401
#define WIN_X      183
#define WIN_Y      35

/* Color names for dropdown */
static const char *color_options =
    "Black|Dark Blue|Dark Green|Cyan|Brown|Dark Purple|Olive|Grey|"
    "Dark Grey|Blue|Green|Light Cyan|Red|Peach|Yellow|White";

typedef struct {
    void *form;
    uint8_t theme_winbg;
    uint8_t theme_titlebar;
    uint8_t theme_titlebtn;
    uint8_t theme_desktop_text_color;
    uint8_t theme_taskbar;
    uint8_t theme_startbtn;
    uint8_t theme_text;
    uint8_t theme_control;
    uint8_t theme_frame_dark;
    uint8_t theme_frame_light;
    uint8_t theme_frame_shine;
    uint8_t theme_highlight;
    uint8_t theme_hover;
    /* Originals for cancel */
    uint8_t orig_theme_winbg;
    uint8_t orig_theme_titlebar;
    uint8_t orig_theme_titlebtn;
    uint8_t orig_theme_desktop_text_color;
    uint8_t orig_theme_taskbar;
    uint8_t orig_theme_startbtn;
    uint8_t orig_theme_text;
    uint8_t orig_theme_control;
    uint8_t orig_theme_frame_dark;
    uint8_t orig_theme_frame_light;
    uint8_t orig_theme_frame_shine;
    uint8_t orig_theme_highlight;
    uint8_t orig_theme_hover;
} cpl_theme_state_t;

static int cpl_theme_init(prog_instance_t *inst);
static int cpl_theme_event(prog_instance_t *inst, int win_idx,
                           const sys_win_event_t *event);
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

static int read_settings(cpl_theme_state_t *state) {
    char *buffer = ini_read_file(SETTINGS_PATH);
    if (!buffer) return -1;

    ini_parser_t ini;
    ini_init(&ini, buffer);

    state->theme_winbg = (uint8_t)ini_get_color(&ini, "THEME", "BG_COLOR", state->theme_winbg);
    state->theme_titlebar = (uint8_t)ini_get_color(&ini, "THEME", "TITLEBAR_COLOR", state->theme_titlebar);
    state->theme_titlebtn = (uint8_t)ini_get_color(&ini, "THEME", "BUTTON_COLOR", state->theme_titlebtn);
    state->theme_taskbar = (uint8_t)ini_get_color(&ini, "THEME", "TASKBAR_COLOR", state->theme_taskbar);
    state->theme_startbtn = (uint8_t)ini_get_color(&ini, "THEME", "START_BUTTON_COLOR", state->theme_startbtn);
    state->theme_text = (uint8_t)ini_get_color(&ini, "THEME", "TEXT_COLOR", state->theme_text);
    state->theme_control = (uint8_t)ini_get_color(&ini, "THEME", "CONTROL_COLOR", state->theme_control);
    state->theme_desktop_text_color = (uint8_t)ini_get_color(&ini, "THEME", "DESKTOP_ICON_TEXT_COLOR", state->theme_desktop_text_color);
    state->theme_frame_dark = (uint8_t)ini_get_color(&ini, "THEME", "FRAME_DARK", state->theme_frame_dark);
    state->theme_frame_light = (uint8_t)ini_get_color(&ini, "THEME", "FRAME_LIGHT", state->theme_frame_light);
    state->theme_frame_shine = (uint8_t)ini_get_color(&ini, "THEME", "FRAME_SHINE", state->theme_frame_shine);
    state->theme_highlight = (uint8_t)ini_get_color(&ini, "THEME", "HIGHLIGHT_COLOR", state->theme_highlight);
    state->theme_hover = (uint8_t)ini_get_color(&ini, "THEME", "HOVER_COLOR", state->theme_hover);
    free(buffer);
    return 0;
}

static int load_settings(cpl_theme_state_t *state) {
    sys_theme_t *theme = sys_win_get_theme();
    state->theme_winbg = theme->bg_color;
    state->theme_titlebar = theme->titlebar_color;
    state->theme_titlebtn = theme->button_color;
    state->theme_taskbar = theme->taskbar_color;
    state->theme_startbtn = theme->start_button_color;
    state->theme_desktop_text_color = theme->desktop_icon_text_color;
    state->theme_text = theme->text_color;
    state->theme_control = theme->control_color;
    state->theme_frame_dark = theme->frame_dark;
    state->theme_frame_light = theme->frame_light;
    state->theme_frame_shine = theme->frame_shine;
    state->theme_highlight = theme->highlight_color;
    state->theme_hover = theme->hover_color;

    if (read_settings(state) != 0) return -1;

    state->orig_theme_winbg = state->theme_winbg;
    state->orig_theme_titlebar = state->theme_titlebar;
    state->orig_theme_titlebtn = state->theme_titlebtn;
    state->orig_theme_desktop_text_color = state->theme_desktop_text_color;
    state->orig_theme_taskbar = state->theme_taskbar;
    state->orig_theme_startbtn = state->theme_startbtn;
    state->orig_theme_text = state->theme_text;
    state->orig_theme_control = state->theme_control;
    state->orig_theme_frame_dark = state->theme_frame_dark;
    state->orig_theme_frame_light = state->theme_frame_light;
    state->orig_theme_frame_shine = state->theme_frame_shine;
    state->orig_theme_highlight = state->theme_highlight;
    state->orig_theme_hover = state->theme_hover;
    return 0;
}

static int save_settings(cpl_theme_state_t *state) {
    char theme_text[512];

    snprintf(theme_text, sizeof(theme_text),
        "[THEME]\r\n"
        "BG_COLOR=%d\r\n"
        "TITLEBAR_COLOR=%d\r\n"
        "TEXT_COLOR=%d\r\n"
        "CONTROL_COLOR=%d\r\n"
        "FRAME_DARK=%d\r\n"
        "FRAME_LIGHT=%d\r\n"
        "FRAME_SHINE=%d\r\n"
        "HIGHLIGHT_COLOR=%d\r\n"
        "HOVER_COLOR=%d\r\n"
        "DESKTOP_ICON_TEXT_COLOR=%d\r\n"
        "BUTTON_COLOR=%d\r\n"
        "TASKBAR_COLOR=%d\r\n"
        "START_BUTTON_COLOR=%d\r\n",
        state->theme_winbg,
        state->theme_titlebar,
        state->theme_text,
        state->theme_control,
        state->theme_frame_dark,
        state->theme_frame_light,
        state->theme_frame_shine,
        state->theme_highlight,
        state->theme_hover,
        state->theme_desktop_text_color,
        state->theme_titlebtn,
        state->theme_taskbar,
        state->theme_startbtn
    );

    return ini_save_section(SETTINGS_PATH, "THEME", theme_text);
}

static void apply_theme(cpl_theme_state_t *state) {
    sys_theme_t *theme = sys_win_get_theme();
    theme->bg_color = state->theme_winbg;
    theme->titlebar_color = state->theme_titlebar;
    theme->desktop_icon_text_color = state->theme_desktop_text_color;
    theme->button_color = state->theme_titlebtn;
    theme->taskbar_color = state->theme_taskbar;
    theme->start_button_color = state->theme_startbtn;
    theme->text_color = state->theme_text;
    theme->control_color = state->theme_control;
    theme->frame_dark = state->theme_frame_dark;
    theme->frame_light = state->theme_frame_light;
    theme->frame_shine = state->theme_frame_shine;
    theme->highlight_color = state->theme_highlight;
    theme->hover_color = state->theme_hover;
}

static int cpl_theme_init(prog_instance_t *inst) {
    cpl_theme_state_t *state = sys_malloc(sizeof(cpl_theme_state_t));
    if (!state) return -1;

    inst->user_data = state;
    if (load_settings(state) != 0) {
        sys_win_msgbox("Could not load theme settings.", "OK", "Theme");
        sys_free(state);
        inst->user_data = 0;
        return -1;
    }

    state->form = sys_win_create_form("Theme", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/THEME.ICO");
    sys_win_set_resizable(state->form, 0);

    static gui_control_t controls[] = {
        { .type = CTRL_FRAME,      .x = 6,   .y = 5,   .w = 262, .h = 291, .fg = -1,  .bg = -1, .id = CTRL_FRAME_THEME,    .text = "System theme" },

        /* Windows */
        { .type = CTRL_LABEL,      .x = 12,  .y = 25,                     .fg = -1,  .bg = -1, .id = CTRL_LBL_WINBG,     .text = "Windows:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 25,  .w = 165, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_WINBG,    .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        /* Title bar */
        { .type = CTRL_LABEL,      .x = 12,  .y = 47,                     .fg = -1,  .bg = -1, .id = CTRL_LBL_TITLEBAR,  .text = "Title bar:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 47,  .w = 165, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_TITLEBAR, .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        /* Buttons */
        { .type = CTRL_LABEL,      .x = 12,  .y = 69,                     .fg = -1,  .bg = -1, .id = CTRL_LBL_TITLEBTN,  .text = "Buttons:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 69,  .w = 165, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_TITLEBTN, .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        /* Taskbar */
        { .type = CTRL_LABEL,      .x = 12,  .y = 91,                     .fg = -1,  .bg = -1, .id = CTRL_LBL_TASKBAR,   .text = "Taskbar:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 91,  .w = 165, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_TASKBAR,  .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        /* Start button */
        { .type = CTRL_LABEL,      .x = 12,  .y = 113,                    .fg = -1,  .bg = -1, .id = CTRL_LBL_STARTBTN,  .text = "Start button:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 113, .w = 165, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_STARTBTN, .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        { .type = CTRL_LABEL,      .x = 12,  .y = 135,                    .fg = -1,  .bg = -1, .id = CTRL_LBL_TEXT, .text = "Text:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 135, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_TEXT, .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 157,                    .fg = -1,  .bg = -1, .id = CTRL_LBL_CONTROL, .text = "Controls:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 157, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_CONTROL, .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        { .type = CTRL_LABEL,      .x = 12,  .y = 179,                    .fg = -1, .bg = -1, .id = CTRL_LBL_FRAME_DARK, .text = "Frame dark:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 179, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_FRAME_DARK, .dropdown = { .cursor_pos = 0, .item_count = 16 } },
        { .type = CTRL_LABEL,      .x = 12,  .y = 201,                    .fg = -1, .bg = -1, .id = CTRL_LBL_FRAME_LIGHT, .text = "Frame light:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 201, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_FRAME_LIGHT, .dropdown = { .cursor_pos = 0, .item_count = 16 } },
        { .type = CTRL_LABEL,      .x = 12,  .y = 223,                    .fg = -1, .bg = -1, .id = CTRL_LBL_FRAME_SHINE, .text = "Frame shine:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 223, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_FRAME_SHINE, .dropdown = { .cursor_pos = 0, .item_count = 16 } },
        { .type = CTRL_LABEL,      .x = 12,  .y = 245,                    .fg = -1, .bg = -1, .id = CTRL_LBL_HIGHLIGHT, .text = "Selection:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 245, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_HIGHLIGHT, .dropdown = { .cursor_pos = 0, .item_count = 16 } },
        { .type = CTRL_LABEL,      .x = 12,  .y = 267,                    .fg = -1, .bg = -1, .id = CTRL_LBL_HOVER, .text = "Hover:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 267, .w = 165, .h = 18, .fg = -1, .bg = -1, .id = CTRL_DROP_HOVER, .dropdown = { .cursor_pos = 0, .item_count = 16 } },

        { .type = CTRL_FRAME,      .x = 6,   .y = 301, .w = 262, .h = 45,  .fg = -1,  .bg = -1, .id = CTRL_FRAME_DESKTOP,  .text = "Desktop icons" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 321,                    .fg = -1,  .bg = -1, .id = CTRL_LBL_ICON_TEXT, .text = "Captions:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 321, .w = 165, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_ICON_TEXT, .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },

        /* Buttons */
        { .type = CTRL_BUTTON,     .x = 30,  .y = 352, .w = 65,  .h = 22,  .fg = 0,  .bg = -1, .id = CTRL_BTN_APPLY,      .text = "Apply" },
        { .type = CTRL_BUTTON,     .x = 105, .y = 352, .w = 65,  .h = 22,  .fg = 0,  .bg = -1, .id = CTRL_BTN_OK,         .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 180, .y = 352, .w = 65,  .h = 22,  .fg = 0,  .bg = -1, .id = CTRL_BTN_CANCEL,     .text = "Cancel" },
    };

    for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++) {
        sys_win_add_control(state->form, &controls[i]);
    }

    /* runtime / state-dependent initialization */
    /* populate colour dropdowns and set selections */
    ctrl_set_text(state->form, CTRL_DROP_WINBG, color_options);
    ctrl_set_text(state->form, CTRL_DROP_TITLEBAR, color_options);
    ctrl_set_text(state->form, CTRL_DROP_TITLEBTN, color_options);
    ctrl_set_text(state->form, CTRL_DROP_TASKBAR, color_options);
    ctrl_set_text(state->form, CTRL_DROP_STARTBTN, color_options);
    ctrl_set_text(state->form, CTRL_DROP_TEXT, color_options);
    ctrl_set_text(state->form, CTRL_DROP_CONTROL, color_options);
    ctrl_set_text(state->form, CTRL_DROP_FRAME_DARK, color_options);
    ctrl_set_text(state->form, CTRL_DROP_FRAME_LIGHT, color_options);
    ctrl_set_text(state->form, CTRL_DROP_FRAME_SHINE, color_options);
    ctrl_set_text(state->form, CTRL_DROP_HIGHLIGHT, color_options);
    ctrl_set_text(state->form, CTRL_DROP_HOVER, color_options);

    gui_control_t *g = sys_win_get_control(state->form, CTRL_DROP_WINBG); if (g) { g->dropdown.cursor_pos = state->theme_winbg; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TITLEBAR); if (g) { g->dropdown.cursor_pos = state->theme_titlebar; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TITLEBTN); if (g) { g->dropdown.cursor_pos = state->theme_titlebtn; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TASKBAR); if (g) { g->dropdown.cursor_pos = state->theme_taskbar; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_STARTBTN); if (g) { g->dropdown.cursor_pos = state->theme_startbtn; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TEXT); if (g) { g->dropdown.cursor_pos = state->theme_text; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_CONTROL); if (g) { g->dropdown.cursor_pos = state->theme_control; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_FRAME_DARK); if (g) { g->dropdown.cursor_pos = state->theme_frame_dark; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_FRAME_LIGHT); if (g) { g->dropdown.cursor_pos = state->theme_frame_light; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_FRAME_SHINE); if (g) { g->dropdown.cursor_pos = state->theme_frame_shine; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_HIGHLIGHT); if (g) { g->dropdown.cursor_pos = state->theme_highlight; g->dropdown.item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_HOVER); if (g) { g->dropdown.cursor_pos = state->theme_hover; g->dropdown.item_count = 16; }

    /* icon text dropdown */
    ctrl_set_text(state->form, CTRL_DROP_ICON_TEXT, color_options);
    g = sys_win_get_control(state->form, CTRL_DROP_ICON_TEXT); if (g) { g->dropdown.cursor_pos = state->theme_desktop_text_color; g->dropdown.item_count = 16; }

    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

/* Get selected value from dropdown */
static uint8_t get_dropdown_value(void *form, int ctrl_id) {
    gui_control_t *ctrl = sys_win_get_control(form, ctrl_id);
    if (ctrl) {
        return (uint8_t)ctrl->dropdown.cursor_pos;
    }
    return 0;
}

static int cpl_theme_event(prog_instance_t *inst, int win_idx,
                           const sys_win_event_t *event) {
    (void)win_idx;
    cpl_theme_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    if (event->type == SYS_WIN_EVENT_REDRAW ||
        event->type == SYS_WIN_EVENT_WINDOW_CHANGED)
        return PROG_EVENT_REDRAW;
    if (event->type != SYS_WIN_EVENT_CONTROL) return PROG_EVENT_NONE;
    int id = event->id;

    /* Dropdown selection changed - just redraw (values read on apply) */
    if (id == CTRL_DROP_WINBG || id == CTRL_DROP_TITLEBAR ||
        id == CTRL_DROP_TITLEBTN || id == CTRL_DROP_TASKBAR ||
        id == CTRL_DROP_STARTBTN || id == CTRL_DROP_ICON_TEXT ||
        id == CTRL_DROP_TEXT || id == CTRL_DROP_CONTROL ||
        id == CTRL_DROP_FRAME_DARK || id == CTRL_DROP_FRAME_LIGHT ||
        id == CTRL_DROP_FRAME_SHINE || id == CTRL_DROP_HIGHLIGHT || id == CTRL_DROP_HOVER) {
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Apply button */
    if (id == CTRL_BTN_APPLY) {
        state->theme_winbg = get_dropdown_value(state->form, CTRL_DROP_WINBG);
        state->theme_titlebar = get_dropdown_value(state->form, CTRL_DROP_TITLEBAR);
        state->theme_titlebtn = get_dropdown_value(state->form, CTRL_DROP_TITLEBTN);
        state->theme_taskbar = get_dropdown_value(state->form, CTRL_DROP_TASKBAR);
        state->theme_startbtn = get_dropdown_value(state->form, CTRL_DROP_STARTBTN);
        state->theme_text = get_dropdown_value(state->form, CTRL_DROP_TEXT);
        state->theme_control = get_dropdown_value(state->form, CTRL_DROP_CONTROL);
        state->theme_frame_dark = get_dropdown_value(state->form, CTRL_DROP_FRAME_DARK);
        state->theme_frame_light = get_dropdown_value(state->form, CTRL_DROP_FRAME_LIGHT);
        state->theme_frame_shine = get_dropdown_value(state->form, CTRL_DROP_FRAME_SHINE);
        state->theme_highlight = get_dropdown_value(state->form, CTRL_DROP_HIGHLIGHT);
        state->theme_hover = get_dropdown_value(state->form, CTRL_DROP_HOVER);
        state->theme_desktop_text_color = get_dropdown_value(state->form, CTRL_DROP_ICON_TEXT);

        apply_theme(state);

        sys_win_draw(state->form);
        sys_win_invalidate_icons();
        sys_win_force_full_redraw();

        return PROG_EVENT_HANDLED;
    }

    if (id == CTRL_BTN_OK) {
        state->theme_winbg = get_dropdown_value(state->form, CTRL_DROP_WINBG);
        state->theme_titlebar = get_dropdown_value(state->form, CTRL_DROP_TITLEBAR);
        state->theme_titlebtn = get_dropdown_value(state->form, CTRL_DROP_TITLEBTN);
        state->theme_taskbar = get_dropdown_value(state->form, CTRL_DROP_TASKBAR);
        state->theme_startbtn = get_dropdown_value(state->form, CTRL_DROP_STARTBTN);
        state->theme_text = get_dropdown_value(state->form, CTRL_DROP_TEXT);
        state->theme_control = get_dropdown_value(state->form, CTRL_DROP_CONTROL);
        state->theme_frame_dark = get_dropdown_value(state->form, CTRL_DROP_FRAME_DARK);
        state->theme_frame_light = get_dropdown_value(state->form, CTRL_DROP_FRAME_LIGHT);
        state->theme_frame_shine = get_dropdown_value(state->form, CTRL_DROP_FRAME_SHINE);
        state->theme_highlight = get_dropdown_value(state->form, CTRL_DROP_HIGHLIGHT);
        state->theme_hover = get_dropdown_value(state->form, CTRL_DROP_HOVER);
        state->theme_desktop_text_color = get_dropdown_value(state->form, CTRL_DROP_ICON_TEXT);

        if (save_settings(state) != 0) {
            sys_win_msgbox("Could not save theme settings.", "OK", "Theme");
            return PROG_EVENT_HANDLED;
        }
        apply_theme(state);

        sys_win_draw(state->form);
        sys_win_invalidate_icons();
        sys_win_force_full_redraw();

        return PROG_EVENT_CLOSE;
    }

    if (id == CTRL_BTN_CANCEL) {
        state->theme_winbg = state->orig_theme_winbg;
        state->theme_titlebar = state->orig_theme_titlebar;
        state->theme_titlebtn = state->orig_theme_titlebtn;
        state->theme_taskbar = state->orig_theme_taskbar;
        state->theme_startbtn = state->orig_theme_startbtn;
        state->theme_text = state->orig_theme_text;
        state->theme_control = state->orig_theme_control;
        state->theme_frame_dark = state->orig_theme_frame_dark;
        state->theme_frame_light = state->orig_theme_frame_light;
        state->theme_frame_shine = state->orig_theme_frame_shine;
        state->theme_highlight = state->orig_theme_highlight;
        state->theme_hover = state->orig_theme_hover;
        state->theme_desktop_text_color = state->orig_theme_desktop_text_color;
        if (read_settings(state) != 0) {
            sys_win_msgbox("Could not load theme settings.", "OK", "Theme");
            return PROG_EVENT_HANDLED;
        }
        apply_theme(state);
        /* Revert visible UI immediately: draw the form and request desktop redraw */
        sys_win_draw(state->form);
        sys_win_invalidate_icons();
        sys_win_force_full_redraw();
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
