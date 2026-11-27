#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdio.h"
#include "../lib/stdlib.h"
#include "../lib/ini.h"
#include "../lib/pathdlg.h"
#include "../lib/palette.h"

#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"
#define DEFAULT_PALETTE_PATH "C:/OSLET/oslet.pal"
#define PALETTE_FILE_FILTER "*.PAL"

#define WIN_WIDTH  354
#define WIN_HEIGHT 222

#define MENU_NEW   1
#define MENU_OPEN  2
#define MENU_SAVE  3
#define MENU_RESET 4
#define MENU_EXIT  5
#define MENU_SAVE_AS 6

#define REQUEST_NONE    0
#define REQUEST_NEW     1
#define REQUEST_OPEN    2
#define REQUEST_SAVE    3
#define REQUEST_SAVE_AS 4
#define REQUEST_RESET   5
#define REQUEST_APPLY   6
#define REQUEST_OK      7

#define CTRL_FRAME_PREVIEW 4
#define CTRL_PREVIEW_COLOR 5
#define CTRL_ICON_ONE      6
#define CTRL_ICON_TWO      7
#define CTRL_ICON_THREE    8
#define CTRL_SWATCH_SELECT 9

#define CTRL_LBL_RED       20
#define CTRL_LBL_GREEN     21
#define CTRL_LBL_BLUE      22
#define CTRL_SLIDER_RED    23
#define CTRL_SLIDER_GREEN  24
#define CTRL_SLIDER_BLUE   25
#define CTRL_VAL_RED       26
#define CTRL_VAL_GREEN     27
#define CTRL_VAL_BLUE      28

#define CTRL_BTN_APPLY     90
#define CTRL_BTN_OK        91
#define CTRL_BTN_CANCEL    92
#define CTRL_COLOR_BASE    100

#define SWATCH_X 209
#define SWATCH_Y 27
#define SWATCH_SIZE 14
#define SWATCH_GAP 3
#define SWATCH_COLS 8

typedef struct {
    void *form;
    char palette_path[256];
    char orig_palette_path[256];
    uint8_t palette[16][3];
    uint8_t orig_palette[16][3];
    int selected_color;
    int preview_dirty;
    int modified;
    int last_focus;
    int request;
} cpl_palette_state_t;

static int save_palette(cpl_palette_state_t *state, int save_as);

static void copy_path(char *dest, const char *src) {
    int i = 0;
    while (src && src[i] && i < 255) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = '\0';
}

static void copy_palette(uint8_t dest[16][3], const uint8_t src[16][3]) {
    memcpy(dest, src, 16 * 3);
}

static void palette_error(const char *message) {
    sys_win_msgbox(message, "OK", "Palette");
}

static void update_title(cpl_palette_state_t *state) {
    const char *name = state->palette_path;
    const char *slash = strrchr(name, '/');
    const char *backslash = strrchr(name, '\\');
    if (slash) name = slash + 1;
    if (backslash && backslash + 1 > name) name = backslash + 1;
    if (!name[0]) name = "Untitled";

    char title[64];
    snprintf(title, sizeof(title), "Palette - %s%s", state->modified ? "*" : "", name);
    if (sys_win_set_title(state->form, title) != 0)
        palette_error("Could not update the window title");
}

static void update_swatch_controls(cpl_palette_state_t *state) {
    gui_control_t *select = sys_win_get_control(state->form, CTRL_SWATCH_SELECT);

    for (int i = 0; i < 16; i++) {
        gui_control_t *ctrl = sys_win_get_control(state->form, CTRL_COLOR_BASE + i);
        if (!ctrl) continue;

        int row = i / SWATCH_COLS;
        int col = i % SWATCH_COLS;
        ctrl->x = SWATCH_X + col * (SWATCH_SIZE + SWATCH_GAP);
        ctrl->y = SWATCH_Y + row * (SWATCH_SIZE + SWATCH_GAP);
        ctrl->w = SWATCH_SIZE;
        ctrl->h = SWATCH_SIZE;
        if (select && i == state->selected_color) {
            select->x = ctrl->x - 1;
            select->y = ctrl->y - 1;
            select->w = SWATCH_SIZE + 2;
            select->h = SWATCH_SIZE + 2;
        }
    }
}

static void update_slider_values(cpl_palette_state_t *state) {
    uint8_t *color = state->palette[state->selected_color];
    gui_form_t *form = (gui_form_t*)state->form;
    int focused = form ? form->focused_control_id : -1;
    gui_control_t *ctrl;
    char buf[8];

    ctrl = sys_win_get_control(state->form, CTRL_SLIDER_RED);
    if (ctrl) { ctrl->scrollbar.cursor_pos = color[0]; ctrl->scrollbar.checked = 1; }
    ctrl = sys_win_get_control(state->form, CTRL_SLIDER_GREEN);
    if (ctrl) { ctrl->scrollbar.cursor_pos = color[1]; ctrl->scrollbar.checked = 1; }
    ctrl = sys_win_get_control(state->form, CTRL_SLIDER_BLUE);
    if (ctrl) { ctrl->scrollbar.cursor_pos = color[2]; ctrl->scrollbar.checked = 1; }

    if (focused != CTRL_VAL_RED) {
        snprintf(buf, sizeof(buf), "%d", color[0]);
        ctrl_set_text(state->form, CTRL_VAL_RED, buf);
    }
    if (focused != CTRL_VAL_GREEN) {
        snprintf(buf, sizeof(buf), "%d", color[1]);
        ctrl_set_text(state->form, CTRL_VAL_GREEN, buf);
    }
    if (focused != CTRL_VAL_BLUE) {
        snprintf(buf, sizeof(buf), "%d", color[2]);
        ctrl_set_text(state->form, CTRL_VAL_BLUE, buf);
    }

    ctrl_set_bg(state->form, CTRL_PREVIEW_COLOR, state->selected_color);
    update_swatch_controls(state);
}

static void update_rgb_texts(cpl_palette_state_t *state) {
    static const int ids[3] = { CTRL_VAL_RED, CTRL_VAL_GREEN, CTRL_VAL_BLUE };
    char buf[8];
    for (int i = 0; i < 3; i++) {
        snprintf(buf, sizeof(buf), "%d", state->palette[state->selected_color][i]);
        ctrl_set_text(state->form, ids[i], buf);
    }
}

static void preview_palette(cpl_palette_state_t *state, int dirty) {
    sys_gfx_set_palette_data(state->palette);
    if (dirty) {
        state->preview_dirty = 1;
        if (!state->modified) {
            state->modified = 1;
            update_title(state);
        }
    }
    update_slider_values(state);
    sys_win_draw(state->form);
}

static int load_settings_path(cpl_palette_state_t *state) {
    char *buffer = ini_read_file(SETTINGS_PATH);
    if (!buffer) return -1;
    ini_parser_t ini;
    ini_init(&ini, buffer);
    const char *val = ini_get(&ini, "PALETTE", "FILE");
    if (val && val[0]) copy_path(state->palette_path, val);
    free(buffer);
    return 0;
}

static int save_settings_path(cpl_palette_state_t *state) {
    char section[320];

    snprintf(section, sizeof(section),
        "[PALETTE]\r\n"
        "FILE=%s\r\n",
        state->palette_path);

    return ini_save_section(SETTINGS_PATH, "PALETTE", section);
}

static int apply_palette_setting(cpl_palette_state_t *state) {
    if (!save_palette(state, 0))
        return -1;

    if (save_settings_path(state) != 0) {
        palette_error("Could not save palette setting");
        return -1;
    }
    copy_path(state->orig_palette_path, state->palette_path);
    copy_palette(state->orig_palette, state->palette);
    state->preview_dirty = 0;
    return 0;
}

static void restore_original_palette(cpl_palette_state_t *state) {
    copy_path(state->palette_path, state->orig_palette_path);
    copy_palette(state->palette, state->orig_palette);
    sys_gfx_set_palette_data(state->orig_palette);
    state->preview_dirty = 0;
}

static void read_sliders(cpl_palette_state_t *state) {
    gui_control_t *ctrl;
    uint8_t *color = state->palette[state->selected_color];

    ctrl = sys_win_get_control(state->form, CTRL_SLIDER_RED);
    if (ctrl) { ctrl->scrollbar.checked = 1; color[0] = (uint8_t)ctrl->scrollbar.cursor_pos; }
    ctrl = sys_win_get_control(state->form, CTRL_SLIDER_GREEN);
    if (ctrl) { ctrl->scrollbar.checked = 1; color[1] = (uint8_t)ctrl->scrollbar.cursor_pos; }
    ctrl = sys_win_get_control(state->form, CTRL_SLIDER_BLUE);
    if (ctrl) { ctrl->scrollbar.checked = 1; color[2] = (uint8_t)ctrl->scrollbar.cursor_pos; }
}

static int rgb_value_control(int ctrl_id) {
    return ctrl_id == CTRL_VAL_RED ||
           ctrl_id == CTRL_VAL_GREEN ||
           ctrl_id == CTRL_VAL_BLUE;
}

static int rgb_value_channel(int ctrl_id) {
    if (ctrl_id == CTRL_VAL_RED) return 0;
    if (ctrl_id == CTRL_VAL_GREEN) return 1;
    return 2;
}

static int parse_rgb_text(cpl_palette_state_t *state, int ctrl_id) {
    const char *text = ctrl_get_text(state->form, ctrl_id);
    int channel = rgb_value_channel(ctrl_id);
    int value;

    if (!text || !text[0])
        return state->palette[state->selected_color][channel];

    value = atoi(text);
    if (value < 0) value = 0;
    if (value > 255) value = 255;
    return value;
}

static void apply_rgb_text(cpl_palette_state_t *state, int ctrl_id) {
    int channel;
    int value;
    char buf[8];

    if (!rgb_value_control(ctrl_id)) return;

    channel = rgb_value_channel(ctrl_id);
    value = parse_rgb_text(state, ctrl_id);
    int changed = state->palette[state->selected_color][channel] != value;
    state->palette[state->selected_color][channel] = (uint8_t)value;

    if (ctrl_id == CTRL_VAL_RED) {
        gui_control_t *ctrl = sys_win_get_control(state->form, CTRL_SLIDER_RED);
        if (ctrl) ctrl->scrollbar.cursor_pos = value;
    } else if (ctrl_id == CTRL_VAL_GREEN) {
        gui_control_t *ctrl = sys_win_get_control(state->form, CTRL_SLIDER_GREEN);
        if (ctrl) ctrl->scrollbar.cursor_pos = value;
    } else {
        gui_control_t *ctrl = sys_win_get_control(state->form, CTRL_SLIDER_BLUE);
        if (ctrl) ctrl->scrollbar.cursor_pos = value;
    }

    snprintf(buf, sizeof(buf), "%d", value);
    ctrl_set_text(state->form, ctrl_id, buf);
    if (changed) preview_palette(state, 1);
}

static void sync_rgb_text_focus(cpl_palette_state_t *state) {
    gui_form_t *form = (gui_form_t*)state->form;
    int focused = form ? form->focused_control_id : -1;

    if (rgb_value_control(state->last_focus) && focused != state->last_focus)
        apply_rgb_text(state, state->last_focus);

    state->last_focus = focused;
}

static void init_scrollbars(cpl_palette_state_t *state) {
    int ids[] = { CTRL_SLIDER_RED, CTRL_SLIDER_GREEN, CTRL_SLIDER_BLUE };
    for (int i = 0; i < 3; i++) {
        gui_control_t *ctrl = sys_win_get_control(state->form, ids[i]);
        if (!ctrl) continue;
        ctrl->scrollbar.max_length = 255;
        ctrl->scrollbar.checked = 1;
    }
}

static void ensure_pal_extension(char *path) {
    char *last_slash = strrchr(path, '/');
    char *last_backslash = strrchr(path, '\\');
    char *start = path;

    if (last_slash && last_slash + 1 > start) start = last_slash + 1;
    if (last_backslash && last_backslash + 1 > start) start = last_backslash + 1;

    if (strchr(start, '.'))
        return;

    if (strlen(path) + 4 < 256)
        strcat(path, ".pal");
}

static void open_palette_dialog(cpl_palette_state_t *state) {
    char path[256];
    uint8_t new_palette[16][3];

    if (!gui_show_path_dialog_filtered("Open Palette", state->palette_path,
                                       PALETTE_FILE_FILTER, path, sizeof(path)))
        return;

    if (palette_parse_pal(path, new_palette) != 0) {
        palette_error("That is not a valid JASC palette file");
        sys_gfx_set_palette_data(state->palette);
        update_slider_values(state);
        sys_win_draw(state->form);
        return;
    }

    copy_path(state->palette_path, path);
    copy_palette(state->palette, new_palette);
    update_rgb_texts(state);
    preview_palette(state, 1);
    state->modified = 0;
    update_title(state);
}

static int save_palette(cpl_palette_state_t *state, int save_as) {
    char path[256] = "";
    if (!save_as) copy_path(path, state->palette_path);

    if (!path[0]) {
        const char *initial = state->palette_path[0] ?
                              state->palette_path : "C:/OSLET/untitled.pal";
        if (!gui_show_path_dialog_filtered("Save Palette", initial,
                                           PALETTE_FILE_FILTER, path, sizeof(path)))
            return 0;
        ensure_pal_extension(path);
        int fd = sys_open(path, "r");
        if (fd >= 0) {
            sys_close(fd);
            if (sys_win_msgbox("Replace the existing palette file?",
                               "Yes|No", "Save Palette") != 1)
                return 0;
        }
    }
    if (palette_save_pal(path, state->palette) != 0) {
        palette_error("Could not save the palette file");
        return 0;
    }

    copy_path(state->palette_path, path);
    state->modified = 0;
    if (strcmp(path, state->orig_palette_path) == 0)
        copy_palette(state->orig_palette, state->palette);
    update_title(state);
    sys_win_draw(state->form);
    return 1;
}

static int confirm_discard(cpl_palette_state_t *state) {
    if (!state->modified) return 1;
    int choice = sys_win_msgbox("Save changes to the palette?",
                                "Yes|No|Cancel", "Palette");
    if (choice == 1) return save_palette(state, 0);
    return choice == 2;
}

static int cpl_palette_init(prog_instance_t *inst) {
    cpl_palette_state_t *state = sys_malloc(sizeof(cpl_palette_state_t));
    if (!state) return -1;

    state->form = NULL;
    state->selected_color = 0;
    state->preview_dirty = 0;
    state->modified = 0;
    state->last_focus = -1;
    state->request = REQUEST_NONE;
    copy_path(state->palette_path, DEFAULT_PALETTE_PATH);
    load_settings_path(state);

    if (palette_parse_pal(state->palette_path, state->palette) != 0) {
        if (palette_load_default_file(DEFAULT_PALETTE_PATH, state->palette) != 0) {
            palette_set_default(state->palette);
        }
        copy_path(state->palette_path, DEFAULT_PALETTE_PATH);
    }

    copy_path(state->orig_palette_path, state->palette_path);
    copy_palette(state->orig_palette, state->palette);
    inst->user_data = state;

    sys_display_info_t display;
    if (sys_gfx_get_display_info(&display) != 0) {
        palette_error("Could not query the display size");
        sys_free(state);
        inst->user_data = 0;
        return -1;
    }
    int x = display.width > WIN_WIDTH ? (display.width - WIN_WIDTH) / 2 : 0;
    int y = display.height > WIN_HEIGHT ? (display.height - WIN_HEIGHT) / 2 : 0;
    state->form = sys_win_create_form("Palette", x, y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        inst->user_data = 0;
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/PALETTE.ICO");
    sys_win_set_resizable(state->form, 0);
    sys_win_menubar_enable(state->form);
    int file_menu = sys_win_menubar_add_menu(state->form, "File");
    if (file_menu < 0 ||
        sys_win_menubar_add_item(state->form, file_menu, "New palette\tCtrl+N", MENU_NEW) < 0 ||
        sys_win_menubar_add_item(state->form, file_menu, "Open palette...\tCtrl+O", MENU_OPEN) < 0 ||
        sys_win_menubar_add_item(state->form, file_menu, "Save palette\tCtrl+S", MENU_SAVE) < 0 ||
        sys_win_menubar_add_item(state->form, file_menu, "Save palette as...", MENU_SAVE_AS) < 0 ||
        sys_win_menubar_add_item(state->form, file_menu, "Reset to default", MENU_RESET) < 0 ||
        sys_win_menubar_add_separator(state->form, file_menu) < 0 ||
        sys_win_menubar_add_item(state->form, file_menu, "Exit", MENU_EXIT) < 0) {
        palette_error("Could not create the palette menu");
        sys_win_destroy_form(state->form);
        sys_free(state);
        inst->user_data = 0;
        return -1;
    }

    static gui_control_t controls[] = {
        { .type = CTRL_FRAME,      .x = 202, .y = 5,   .w = 146, .h = 142, .fg = -1, .bg = -1, .id = CTRL_FRAME_PREVIEW, .text = "Colour preview" },
        { .type = CTRL_LABEL,      .x = 208, .y = 26,  .w = 16,  .h = 16, .fg = -1, .bg = -1, .id = CTRL_SWATCH_SELECT, .border = 1, .border_color = 0, .text = "" },
        { .type = CTRL_PICTUREBOX, .x = 214, .y = 68,  .w = 32,  .h = 32, .fg = 0, .bg = -1, .id = CTRL_ICON_ONE,      .text = "C:/ICONS/VIEWER.ICO" },
        { .type = CTRL_PICTUREBOX, .x = 263, .y = 68,  .w = 32,  .h = 32, .fg = 0, .bg = -1, .id = CTRL_ICON_TWO,      .text = "C:/ICONS/OSLET.ICO" },
        { .type = CTRL_PICTUREBOX, .x = 308, .y = 68,  .w = 32,  .h = 32, .fg = 0, .bg = -1, .id = CTRL_ICON_THREE,    .text = "C:/ICONS/CABINET.ICO" },
        { .type = CTRL_LABEL,      .x = 253, .y = 112, .w = 44,  .h = 24, .fg = -1, .bg = 0,  .id = CTRL_PREVIEW_COLOR, .border = 1, .border_color = 0, .text = "" },

        { .type = CTRL_LABEL,      .x = 6,   .y = 28,                    .fg = -1, .bg = -1, .id = CTRL_LBL_RED,   .text = "Red" },
        { .type = CTRL_SCROLLBAR,  .x = 50,  .y = 26,  .w = 108, .h = 18, .fg = 0, .bg = 15, .id = CTRL_SLIDER_RED,   .scrollbar = { .max_length = 255, .checked = 1 } },
        { .type = CTRL_TEXTBOX,    .x = 164, .y = 26,  .w = 30,  .h = 18, .fg = -1, .bg = -1, .id = CTRL_VAL_RED,   .textbox = { .max_length = 4 }, .text = "0" },

        { .type = CTRL_LABEL,      .x = 6,   .y = 68,                    .fg = -1, .bg = -1, .id = CTRL_LBL_GREEN, .text = "Green" },
        { .type = CTRL_SCROLLBAR,  .x = 50,  .y = 66,  .w = 108, .h = 18, .fg = 0, .bg = 15, .id = CTRL_SLIDER_GREEN, .scrollbar = { .max_length = 255, .checked = 1 } },
        { .type = CTRL_TEXTBOX,    .x = 164, .y = 66,  .w = 30,  .h = 18, .fg = -1, .bg = -1, .id = CTRL_VAL_GREEN, .textbox = { .max_length = 4 }, .text = "0" },

        { .type = CTRL_LABEL,      .x = 6,   .y = 108,                   .fg = -1, .bg = -1, .id = CTRL_LBL_BLUE,  .text = "Blue" },
        { .type = CTRL_SCROLLBAR,  .x = 50,  .y = 106, .w = 108, .h = 18, .fg = 0, .bg = 15, .id = CTRL_SLIDER_BLUE,  .scrollbar = { .max_length = 255, .checked = 1 } },
        { .type = CTRL_TEXTBOX,    .x = 164, .y = 106, .w = 30,  .h = 18, .fg = -1, .bg = -1, .id = CTRL_VAL_BLUE,  .textbox = { .max_length = 4 }, .text = "0" },

        { .type = CTRL_BUTTON,     .x = 86,  .y = 155, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_APPLY,  .text = "Apply" },
        { .type = CTRL_BUTTON,     .x = 148, .y = 155, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_OK,     .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 210, .y = 155, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_CANCEL, .text = "Cancel" },
    };

    for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++)
        sys_win_add_control(state->form, &controls[i]);

    for (int i = 0; i < 16; i++) {
        gui_control_t swatch = {0};
        swatch.type = CTRL_PICTUREBOX;
        swatch.fg = 0;
        swatch.bg = i;
        swatch.id = CTRL_COLOR_BASE + i;
        sys_win_add_control(state->form, &swatch);
    }

    update_title(state);
    init_scrollbars(state);
    update_slider_values(state);

    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static int cpl_palette_event(prog_instance_t *inst, int win_idx,
                             const sys_win_event_t *event) {
    (void)win_idx;
    cpl_palette_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    sync_rgb_text_focus(state);

    if (event->type == SYS_WIN_EVENT_CONTROL_KEY) {
        if (rgb_value_control(state->last_focus))
            apply_rgb_text(state, state->last_focus);
        if (event->id == 14) state->request = REQUEST_NEW;
        else if (event->id == 15) state->request = REQUEST_OPEN;
        else if (event->id == 19) state->request = REQUEST_SAVE;
        else return PROG_EVENT_NONE;
        return PROG_EVENT_HANDLED;
    }
    if (event->type == SYS_WIN_EVENT_MENU) {
        if (rgb_value_control(state->last_focus))
            apply_rgb_text(state, state->last_focus);
        if (event->id == MENU_NEW) state->request = REQUEST_NEW;
        else if (event->id == MENU_OPEN) state->request = REQUEST_OPEN;
        else if (event->id == MENU_SAVE) state->request = REQUEST_SAVE;
        else if (event->id == MENU_SAVE_AS) state->request = REQUEST_SAVE_AS;
        else if (event->id == MENU_RESET) state->request = REQUEST_RESET;
        else if (event->id == MENU_EXIT) return PROG_EVENT_CLOSE;
        else return PROG_EVENT_NONE;
        return PROG_EVENT_HANDLED;
    }
    if (event->type == SYS_WIN_EVENT_REDRAW ||
        event->type == SYS_WIN_EVENT_WINDOW_CHANGED) return PROG_EVENT_REDRAW;
    if (event->type != SYS_WIN_EVENT_CONTROL) return PROG_EVENT_NONE;
    int id = event->id;

    if (id >= CTRL_COLOR_BASE && id < CTRL_COLOR_BASE + 16) {
        state->selected_color = id - CTRL_COLOR_BASE;
        update_slider_values(state);
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    if (rgb_value_control(id)) {
        apply_rgb_text(state, id);
        return PROG_EVENT_HANDLED;
    }

    if (id == CTRL_SLIDER_RED ||
        id == CTRL_SLIDER_GREEN ||
        id == CTRL_SLIDER_BLUE) {
        read_sliders(state);
        preview_palette(state, 1);
        return PROG_EVENT_HANDLED;
    }

    if (id == CTRL_BTN_APPLY) {
        state->request = REQUEST_APPLY;
        return PROG_EVENT_HANDLED;
    }

    if (id == CTRL_BTN_OK) {
        state->request = REQUEST_OK;
        return PROG_EVENT_HANDLED;
    }

    if (id == CTRL_BTN_CANCEL) {
        restore_original_palette(state);
        return PROG_EVENT_CLOSE;
    }

    return PROG_EVENT_NONE;
}

static void cpl_palette_update(prog_instance_t *inst) {
    cpl_palette_state_t *state = inst->user_data;
    if (!state) return;

    sync_rgb_text_focus(state);

    gui_form_t *form = (gui_form_t*)state->form;
    gui_control_t *focused = form && form->focused_control_id >= 0 ?
        sys_win_get_control(state->form, form->focused_control_id) : NULL;
    if (state->request == REQUEST_NONE && sys_win_is_focused(state->form) &&
        (!focused || focused->type != CTRL_TEXTBOX)) {
        int key = sys_get_key_nonblock();
        if (key == 14) state->request = REQUEST_NEW;
        else if (key == 15) state->request = REQUEST_OPEN;
        else if (key == 19) state->request = REQUEST_SAVE;
    }

    int request = state->request;
    state->request = REQUEST_NONE;
    if (request == REQUEST_NEW && confirm_discard(state)) {
        state->palette_path[0] = '\0';
        palette_set_default(state->palette);
        update_rgb_texts(state);
        preview_palette(state, 1);
        update_title(state);
    } else if (request == REQUEST_OPEN && confirm_discard(state)) {
        open_palette_dialog(state);
    } else if (request == REQUEST_SAVE || request == REQUEST_SAVE_AS) {
        save_palette(state, request == REQUEST_SAVE_AS);
    } else if (request == REQUEST_RESET) {
        palette_set_default(state->palette);
        update_rgb_texts(state);
        preview_palette(state, 1);
    } else if (request == REQUEST_APPLY || request == REQUEST_OK) {
        if (apply_palette_setting(state) == 0 && request == REQUEST_OK) {
            progman_close(inst->instance_id);
            return;
        }
    }
}

static void cpl_palette_cleanup(prog_instance_t *inst) {
    cpl_palette_state_t *state = inst->user_data;
    if (!state) return;

    if (state->preview_dirty)
        sys_gfx_set_palette_data(state->orig_palette);

    if (state->form) {
        prog_unregister_window(inst, state->form);
        sys_win_destroy_form(state->form);
    }

    sys_free(state);
    inst->user_data = 0;
}

const progmod_t cpl_palette_module = {
    .name = "Palette",
    .icon_path = "C:/ICONS/PALETTE.ICO",
    .init = cpl_palette_init,
    .update = cpl_palette_update,
    .handle_event = cpl_palette_event,
    .cleanup = cpl_palette_cleanup,
    .flags = PROG_FLAG_SINGLETON
};
