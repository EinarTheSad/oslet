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
#define WIN_HEIGHT 213
#define WIN_X      279
#define WIN_Y      232

#define CTRL_FRAME_FILE    1
#define CTRL_TXT_PATH      2
#define CTRL_BTN_OPEN      3
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

#define CTRL_BTN_SAVE      89
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
    int last_focus;
    int open_requested;
    int save_requested;
} cpl_palette_state_t;

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

static void preview_palette(cpl_palette_state_t *state, int dirty) {
    sys_gfx_set_palette_data(state->palette);
    if (dirty) state->preview_dirty = 1;
    update_slider_values(state);
    sys_win_draw(state->form);
}

static int load_settings_path(cpl_palette_state_t *state) {
    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) return -1;

    char buffer[1024];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);
    if (bytes <= 0) return -1;

    buffer[bytes] = '\0';
    ini_parser_t ini;
    ini_init(&ini, buffer);
    const char *val = ini_get(&ini, "PALETTE", "FILE");
    if (val && val[0]) copy_path(state->palette_path, val);
    return 0;
}

static void save_settings_path(cpl_palette_state_t *state) {
    char read_buf[2048];
    char out_buf[4096];
    char section[320];
    int bytes = 0;

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd >= 0) {
        bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1);
        sys_close(fd);
    }
    if (bytes > 0) read_buf[bytes] = '\0';
    else read_buf[0] = '\0';

    snprintf(section, sizeof(section),
        "[PALETTE]\r\n"
        "FILE=%s\r\n",
        state->palette_path);

    if (ini_replace_section(read_buf, "PALETTE", section, out_buf, sizeof(out_buf)) < 0)
        return;

    fd = sys_open(SETTINGS_PATH, "w");
    if (fd >= 0) {
        sys_write_file(fd, out_buf, strlen(out_buf));
        sys_close(fd);
    }
}

static int apply_palette_setting(cpl_palette_state_t *state) {
    const char *typed_path = ctrl_get_text(state->form, CTRL_TXT_PATH);
    if (typed_path && typed_path[0]) copy_path(state->palette_path, typed_path);

    if (!state->palette_path[0]) {
        palette_error("Choose a palette file first");
        return -1;
    }

    save_settings_path(state);
    copy_path(state->orig_palette_path, state->palette_path);
    copy_palette(state->orig_palette, state->palette);
    state->preview_dirty = 0;
    return 0;
}

static void restore_original_palette(cpl_palette_state_t *state) {
    copy_path(state->palette_path, state->orig_palette_path);
    copy_palette(state->palette, state->orig_palette);
    ctrl_set_text(state->form, CTRL_TXT_PATH, state->palette_path);
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
    preview_palette(state, 1);
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
        ctrl_set_text(state->form, CTRL_TXT_PATH, state->palette_path);
        sys_gfx_set_palette_data(state->palette);
        update_slider_values(state);
        sys_win_draw(state->form);
        return;
    }

    copy_path(state->palette_path, path);
    copy_palette(state->palette, new_palette);
    ctrl_set_text(state->form, CTRL_TXT_PATH, state->palette_path);
    preview_palette(state, 1);
}

static void save_palette_dialog(cpl_palette_state_t *state) {
    char path[256];

    if (!gui_show_path_dialog_filtered("Save Palette", state->palette_path,
                                       PALETTE_FILE_FILTER, path, sizeof(path)))
        return;

    ensure_pal_extension(path);
    if (palette_save_pal(path, state->palette) != 0) {
        palette_error("Could not save the palette file");
        return;
    }

    copy_path(state->palette_path, path);
    ctrl_set_text(state->form, CTRL_TXT_PATH, state->palette_path);
    sys_win_draw(state->form);
}

static int cpl_palette_init(prog_instance_t *inst) {
    cpl_palette_state_t *state = sys_malloc(sizeof(cpl_palette_state_t));
    if (!state) return -1;

    state->form = NULL;
    state->selected_color = 0;
    state->preview_dirty = 0;
    state->last_focus = -1;
    state->open_requested = 0;
    state->save_requested = 0;
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

    state->form = sys_win_create_form("Palette", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        inst->user_data = 0;
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/PALETTE.ICO");
    sys_win_set_resizable(state->form, 0);

    static gui_control_t controls[] = {
        { .type = CTRL_FRAME,      .x = 6,   .y = 5,   .w = 190, .h = 50, .fg = 0, .bg = -1, .id = CTRL_FRAME_FILE,    .text = "System palette" },
        { .type = CTRL_TEXTBOX,    .x = 16,  .y = 28,  .w = 140, .h = 18, .fg = 0, .bg = 15, .id = CTRL_TXT_PATH,      .textbox = { .max_length = 255 }, .text = "" },
        { .type = CTRL_BUTTON,     .x = 164, .y = 28,  .w = 22,  .h = 18, .fg = 0, .bg = -1, .id = CTRL_BTN_OPEN,      .text = "..." },

        { .type = CTRL_FRAME,      .x = 202, .y = 5,   .w = 146, .h = 142, .fg = 0, .bg = -1, .id = CTRL_FRAME_PREVIEW, .text = "Colour preview" },
        { .type = CTRL_LABEL,      .x = 208, .y = 26,  .w = 16,  .h = 16, .fg = 0, .bg = -1, .id = CTRL_SWATCH_SELECT, .border = 1, .border_color = 0, .text = "" },
        { .type = CTRL_PICTUREBOX, .x = 214, .y = 68,  .w = 32,  .h = 32, .fg = 0, .bg = -1, .id = CTRL_ICON_ONE,      .text = "C:/ICONS/VIEWER.ICO" },
        { .type = CTRL_PICTUREBOX, .x = 263, .y = 68,  .w = 32,  .h = 32, .fg = 0, .bg = -1, .id = CTRL_ICON_TWO,      .text = "C:/ICONS/OSLET.ICO" },
        { .type = CTRL_PICTUREBOX, .x = 308, .y = 68,  .w = 32,  .h = 32, .fg = 0, .bg = -1, .id = CTRL_ICON_THREE,    .text = "C:/ICONS/CABINET.ICO" },
        { .type = CTRL_LABEL,      .x = 253, .y = 112, .w = 44,  .h = 24, .fg = 0, .bg = 0,  .id = CTRL_PREVIEW_COLOR, .border = 1, .border_color = 0, .text = "" },

        { .type = CTRL_LABEL,      .x = 6,   .y = 72,                    .fg = 0, .bg = -1, .id = CTRL_LBL_RED,   .text = "Red" },
        { .type = CTRL_SCROLLBAR,  .x = 50,  .y = 70,  .w = 108, .h = 18, .fg = 0, .bg = 15, .id = CTRL_SLIDER_RED,   .scrollbar = { .max_length = 255, .checked = 1 } },
        { .type = CTRL_TEXTBOX,    .x = 164, .y = 70,  .w = 30,  .h = 18, .fg = 0, .bg = 15, .id = CTRL_VAL_RED,   .textbox = { .max_length = 4 }, .text = "0" },

        { .type = CTRL_LABEL,      .x = 6,   .y = 100,                   .fg = 0, .bg = -1, .id = CTRL_LBL_GREEN, .text = "Green" },
        { .type = CTRL_SCROLLBAR,  .x = 50,  .y = 98,  .w = 108, .h = 18, .fg = 0, .bg = 15, .id = CTRL_SLIDER_GREEN, .scrollbar = { .max_length = 255, .checked = 1 } },
        { .type = CTRL_TEXTBOX,    .x = 164, .y = 98,  .w = 30,  .h = 18, .fg = 0, .bg = 15, .id = CTRL_VAL_GREEN, .textbox = { .max_length = 4 }, .text = "0" },

        { .type = CTRL_LABEL,      .x = 6,   .y = 128,                   .fg = 0, .bg = -1, .id = CTRL_LBL_BLUE,  .text = "Blue" },
        { .type = CTRL_SCROLLBAR,  .x = 50,  .y = 126, .w = 108, .h = 18, .fg = 0, .bg = 15, .id = CTRL_SLIDER_BLUE,  .scrollbar = { .max_length = 255, .checked = 1 } },
        { .type = CTRL_TEXTBOX,    .x = 164, .y = 126, .w = 30,  .h = 18, .fg = 0, .bg = 15, .id = CTRL_VAL_BLUE,  .textbox = { .max_length = 4 }, .text = "0" },

        { .type = CTRL_BUTTON,     .x = 55,  .y = 164, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_SAVE,   .text = "Save" },
        { .type = CTRL_BUTTON,     .x = 117, .y = 164, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_APPLY,  .text = "Apply" },
        { .type = CTRL_BUTTON,     .x = 179, .y = 164, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_OK,     .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 241, .y = 164, .w = 58,  .h = 22, .fg = 0, .bg = -1, .id = CTRL_BTN_CANCEL, .text = "Cancel" },
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

    ctrl_set_text(state->form, CTRL_TXT_PATH, state->palette_path);
    init_scrollbars(state);
    update_slider_values(state);

    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static int cpl_palette_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;
    cpl_palette_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    sync_rgb_text_focus(state);

    if (event == -1 || event == -2) return PROG_EVENT_REDRAW;

    if (event >= CTRL_COLOR_BASE && event < CTRL_COLOR_BASE + 16) {
        state->selected_color = event - CTRL_COLOR_BASE;
        update_slider_values(state);
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_BTN_OPEN) {
        state->open_requested = 1;
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_BTN_SAVE) {
        state->save_requested = 1;
        return PROG_EVENT_HANDLED;
    }

    if (rgb_value_control(event)) {
        apply_rgb_text(state, event);
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_SLIDER_RED ||
        event == CTRL_SLIDER_GREEN ||
        event == CTRL_SLIDER_BLUE) {
        read_sliders(state);
        preview_palette(state, 1);
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_BTN_APPLY) {
        if (apply_palette_setting(state) == 0)
            sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_BTN_OK) {
        if (apply_palette_setting(state) == 0)
            return PROG_EVENT_CLOSE;
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_BTN_CANCEL) {
        restore_original_palette(state);
        return PROG_EVENT_CLOSE;
    }

    return PROG_EVENT_NONE;
}

static void cpl_palette_update(prog_instance_t *inst) {
    cpl_palette_state_t *state = inst->user_data;
    if (!state) return;

    sync_rgb_text_focus(state);

    if (state->open_requested) {
        state->open_requested = 0;
        open_palette_dialog(state);
    }

    if (state->save_requested) {
        state->save_requested = 0;
        save_palette_dialog(state);
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
