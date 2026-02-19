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

#define CTRL_DROP_WINBG      20
#define CTRL_DROP_TITLEBAR   21
#define CTRL_DROP_TITLEBTN   22
#define CTRL_DROP_TASKBAR    23
#define CTRL_DROP_STARTBTN   24
#define CTRL_DROP_ICON_TEXT  25

#define CTRL_BTN_APPLY   50
#define CTRL_BTN_OK      51
#define CTRL_BTN_CANCEL  52

#define WIN_WIDTH  274
#define WIN_HEIGHT 247
#define WIN_X      183
#define WIN_Y      92

/* Color names for dropdown */
static const char *color_options =
    "Black|Dark Blue|Dark Green|Cyan|Brown|Dark Purple|Olive|Grey|"
    "Dark Grey|Blue|Green|Light Cyan|Red|Peach|Yellow|White";

/* Simple two-option for icon text color */
static const char *icon_text_options = "Black|White"; 

typedef struct {
    void *form;
    uint8_t theme_winbg;
    uint8_t theme_titlebar;
    uint8_t theme_titlebtn;
    uint8_t theme_text_color; /* 0 or 15 */
    uint8_t theme_taskbar;
    uint8_t theme_startbtn;
    /* Originals for cancel */
    uint8_t orig_theme_winbg;
    uint8_t orig_theme_titlebar;
    uint8_t orig_theme_titlebtn;
    uint8_t orig_theme_text_color;
    uint8_t orig_theme_taskbar;
    uint8_t orig_theme_startbtn;
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
    state->theme_startbtn = theme->start_button_color;
    state->theme_text_color = theme->icon_text_color;

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) goto store_orig;

    char buffer[1024];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);

    if (bytes <= 0) goto store_orig;
    buffer[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buffer);

    state->theme_winbg = (uint8_t)ini_get_color(&ini, "THEME", "BG_COLOR", state->theme_winbg);
    state->theme_titlebar = (uint8_t)ini_get_color(&ini, "THEME", "TITLEBAR_COLOR", state->theme_titlebar);
    state->theme_titlebtn = (uint8_t)ini_get_color(&ini, "THEME", "BUTTON_COLOR", state->theme_titlebtn);
    state->theme_taskbar = (uint8_t)ini_get_color(&ini, "THEME", "TASKBAR_COLOR", state->theme_taskbar);
    state->theme_startbtn = (uint8_t)ini_get_color(&ini, "THEME", "START_BUTTON_COLOR", state->theme_startbtn);
    
    const char *val = ini_get(&ini, "THEME", "ICON_TEXT_COLOR");
    if (val) {
        int c = atoi(val);
        if (c == 0 || c == 15) state->theme_text_color = (uint8_t)c;
    }

store_orig:
    state->orig_theme_winbg = state->theme_winbg;
    state->orig_theme_titlebar = state->theme_titlebar;
    state->orig_theme_titlebtn = state->theme_titlebtn;
    state->orig_theme_text_color = state->theme_text_color;
    state->orig_theme_taskbar = state->theme_taskbar;
    state->orig_theme_startbtn = state->theme_startbtn;
}

static void save_settings(cpl_theme_state_t *state) {
    char read_buf[2048];
    char tmp1[4096];
    char theme_text[512];

    /* Read existing INI to preserve other sections */
    int fd = sys_open(SETTINGS_PATH, "r");
    int bytes = 0;
    if (fd >= 0) {
        bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1);
        sys_close(fd);
    }
    if (bytes > 0) read_buf[bytes] = '\0';
    else read_buf[0] = '\0';

    /* Build theme section text */
    snprintf(theme_text, sizeof(theme_text),
        "[THEME]\r\n"
        "BG_COLOR=%d\r\n"
        "TITLEBAR_COLOR=%d\r\n"
        "TITLEBAR_INACTIVE=8\r\n"
        "FRAME_DARK=8\r\n"
        "FRAME_LIGHT=7\r\n"
        "ICON_TEXT_COLOR=%d\r\n"
        "BUTTON_COLOR=%d\r\n"
        "TASKBAR_COLOR=%d\r\n"
        "START_BUTTON_COLOR=%d\r\n",
        state->theme_winbg,
        state->theme_titlebar,
        state->theme_text_color,
        state->theme_titlebtn,
        state->theme_taskbar,
        state->theme_startbtn
    );

    /* Replace or insert only the THEME section */
    if (ini_replace_section(read_buf, "THEME", theme_text, tmp1, sizeof(tmp1)) < 0) return;

    fd = sys_open(SETTINGS_PATH, "w");
    if (fd >= 0) {
        sys_write_file(fd, tmp1, strlen(tmp1));
        sys_close(fd);
    }
}

static void apply_theme(cpl_theme_state_t *state) {
    sys_theme_t *theme = sys_win_get_theme();
    theme->bg_color = state->theme_winbg;
    theme->titlebar_color = state->theme_titlebar;
    theme->icon_text_color = state->theme_text_color;
    theme->button_color = state->theme_titlebtn;
    theme->taskbar_color = state->theme_taskbar;
    theme->start_button_color = state->theme_startbtn;
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

    static gui_control_t controls[] = {
        { .type = CTRL_FRAME,      .x = 6,   .y = 5,   .w = 262, .h = 137, .fg = 0,  .bg = -1, .id = CTRL_FRAME_THEME,    .text = "System theme" },

        /* Windows */
        { .type = CTRL_LABEL,      .x = 12,  .y = 25,                     .fg = 0,  .bg = -1, .id = CTRL_LBL_WINBG,     .text = "Windows:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 25,  .w = 165, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_WINBG,    .cursor_pos = 0, .item_count = 16, .text = "" },

        /* Title bar */
        { .type = CTRL_LABEL,      .x = 12,  .y = 47,                     .fg = 0,  .bg = -1, .id = CTRL_LBL_TITLEBAR,  .text = "Title bar:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 47,  .w = 165, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_TITLEBAR, .cursor_pos = 0, .item_count = 16, .text = "" },

        /* Buttons */
        { .type = CTRL_LABEL,      .x = 12,  .y = 69,                     .fg = 0,  .bg = -1, .id = CTRL_LBL_TITLEBTN,  .text = "Buttons:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 69,  .w = 165, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_TITLEBTN, .cursor_pos = 0, .item_count = 16, .text = "" },

        /* Taskbar */
        { .type = CTRL_LABEL,      .x = 12,  .y = 91,                     .fg = 0,  .bg = -1, .id = CTRL_LBL_TASKBAR,   .text = "Taskbar:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 91,  .w = 165, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_TASKBAR,  .cursor_pos = 0, .item_count = 16, .text = "" },

        /* Start button */
        { .type = CTRL_LABEL,      .x = 12,  .y = 113,                    .fg = 0,  .bg = -1, .id = CTRL_LBL_STARTBTN,  .text = "Start button:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 113, .w = 165, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_STARTBTN, .cursor_pos = 0, .item_count = 16, .text = "" },

        /* Desktop icons frame + icon-text dropdown */
        { .type = CTRL_FRAME,      .x = 6,   .y = 147, .w = 262, .h = 45,  .fg = 0,  .bg = -1, .id = CTRL_FRAME_DESKTOP,  .text = "Desktop icons" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 167,                    .fg = 0,  .bg = -1, .id = CTRL_LBL_ICON_TEXT, .text = "Icon text:" },
        { .type = CTRL_DROPDOWN,   .x = 95,  .y = 167, .w = 165, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_ICON_TEXT, .cursor_pos = 0, .item_count = 2,  .text = "" },

        /* Buttons */
        { .type = CTRL_BUTTON,     .x = 30,  .y = 198, .w = 65,  .h = 22,  .fg = 0,  .bg = -1, .id = CTRL_BTN_APPLY,      .text = "Apply" },
        { .type = CTRL_BUTTON,     .x = 105, .y = 198, .w = 65,  .h = 22,  .fg = 0,  .bg = -1, .id = CTRL_BTN_OK,         .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 180, .y = 198, .w = 65,  .h = 22,  .fg = 0,  .bg = -1, .id = CTRL_BTN_CANCEL,     .text = "Cancel" },
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

    gui_control_t *g = sys_win_get_control(state->form, CTRL_DROP_WINBG); if (g) { g->cursor_pos = state->theme_winbg; g->item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TITLEBAR); if (g) { g->cursor_pos = state->theme_titlebar; g->item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TITLEBTN); if (g) { g->cursor_pos = state->theme_titlebtn; g->item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_TASKBAR); if (g) { g->cursor_pos = state->theme_taskbar; g->item_count = 16; }
    g = sys_win_get_control(state->form, CTRL_DROP_STARTBTN); if (g) { g->cursor_pos = state->theme_startbtn; g->item_count = 16; }

    /* icon text dropdown */
    ctrl_set_text(state->form, CTRL_DROP_ICON_TEXT, icon_text_options);
    g = sys_win_get_control(state->form, CTRL_DROP_ICON_TEXT); if (g) { g->cursor_pos = (state->theme_text_color == 15) ? 1 : 0; g->item_count = 2; }

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
        event == CTRL_DROP_STARTBTN || event == CTRL_DROP_ICON_TEXT) {
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
        state->theme_startbtn = get_dropdown_value(state->form, CTRL_DROP_STARTBTN);
        state->theme_text_color = get_dropdown_value(state->form, CTRL_DROP_ICON_TEXT) ? 15 : 0;

          apply_theme(state);
          save_settings(state);

          state->orig_theme_winbg = state->theme_winbg;
          state->orig_theme_titlebar = state->theme_titlebar;
          state->orig_theme_titlebtn = state->theme_titlebtn;
          state->orig_theme_text_color = state->theme_text_color;
          state->orig_theme_taskbar = state->theme_taskbar;
          state->orig_theme_startbtn = state->theme_startbtn;

          /* Draw this form immediately, then request a full desktop redraw.
              Avoid calling sys_win_redraw_all here because it would consume the
              full-redraw flag before the desktop loop can perform its own
              desktop/taskbar redraw. The desktop main loop will notice the
              full redraw request and repaint wallpaper/taskbar/icons. */
          sys_win_draw(state->form);
          sys_win_invalidate_icons();
          sys_win_force_full_redraw();

          return PROG_EVENT_HANDLED;
    }

    /* OK button */
    if (event == CTRL_BTN_OK) {
        /* Read values from dropdowns */
        state->theme_winbg = get_dropdown_value(state->form, CTRL_DROP_WINBG);
        state->theme_titlebar = get_dropdown_value(state->form, CTRL_DROP_TITLEBAR);
        state->theme_titlebtn = get_dropdown_value(state->form, CTRL_DROP_TITLEBTN);
        state->theme_taskbar = get_dropdown_value(state->form, CTRL_DROP_TASKBAR);
        state->theme_startbtn = get_dropdown_value(state->form, CTRL_DROP_STARTBTN);
        state->theme_text_color = get_dropdown_value(state->form, CTRL_DROP_ICON_TEXT) ? 15 : 0;

          apply_theme(state);
          save_settings(state);

          /* Draw this form and request desktop full redraw; let the desktop
              main loop perform wallpaper/taskbar redrawing before final
              window compositing. */
          sys_win_draw(state->form);
          sys_win_invalidate_icons();
          sys_win_force_full_redraw();

          return PROG_EVENT_CLOSE;
    }

    /* Cancel button */
    if (event == CTRL_BTN_CANCEL) {
        state->theme_winbg = state->orig_theme_winbg;
        state->theme_titlebar = state->orig_theme_titlebar;
        state->theme_titlebtn = state->orig_theme_titlebtn;
        state->theme_taskbar = state->orig_theme_taskbar;
        state->theme_startbtn = state->orig_theme_startbtn;
        state->theme_text_color = state->orig_theme_text_color;
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
