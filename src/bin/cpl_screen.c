#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdlib.h"
#include "../lib/ini.h"
#include "../lib/stdio.h"

#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"
#define MAX_BMP_FILES 32

extern void desktop_apply_settings(uint8_t color, const char *wallpaper, uint8_t wallpaper_mode);

/* Control IDs */
#define CTRL_PIC_MONITOR     1
#define CTRL_FRAME_WALLPAPER 2
#define CTRL_BTN_CANCEL      4
#define CTRL_BTN_OK          5
#define CTRL_BTN_APPLY       6
#define CTRL_PIC_PREVIEW     7
#define CTRL_LBL_FOLDER      8
#define CTRL_TXT_FOLDER      9
#define CTRL_BTN_FOLDER_OK   10
#define CTRL_DROP_IMAGES     11
#define CTRL_LBL_CHOOSE      12
#define CTRL_RADIO_STRETCH   13
#define CTRL_RADIO_CENTER    14
#define CTRL_FRAME_COLOR     15
#define CTRL_DROP_COLOR      16

#define WIN_WIDTH  246
#define WIN_HEIGHT 360
#define WIN_X      198
#define WIN_Y      54

static const char *color_options =
    "Black|Dark Blue|Dark Green|Cyan|Brown|Dark Purple|Olive|Grey|"
    "Dark Grey|Blue|Green|Light Cyan|Red|Peach|Yellow|White";

typedef struct {
    void *form;
    uint8_t desktop_color;
    char wallpaper[128];
    char folder[128];
    char bmp_files[MAX_BMP_FILES][64];
    int bmp_count;
    int wallpaper_mode; /* 0=center, 1=stretch */
    /* Originals for cancel */
    uint8_t orig_desktop_color;
    char orig_wallpaper[128];
    int orig_wallpaper_mode;
} cpl_screen_state_t;

static int cpl_screen_init(prog_instance_t *inst);
static int cpl_screen_event(prog_instance_t *inst, int win_idx, int event);
static void cpl_screen_cleanup(prog_instance_t *inst);

const progmod_t cpl_screen_module = {
    .name = "Screen",
    .icon_path = "C:/ICONS/SCREEN.ICO",
    .init = cpl_screen_init,
    .update = 0,
    .handle_event = cpl_screen_event,
    .cleanup = cpl_screen_cleanup,
    .flags = PROG_FLAG_SINGLETON
};

static void load_settings(cpl_screen_state_t *state) {
    sys_theme_t *theme = sys_win_get_theme();
    state->desktop_color = theme->desktop_color;
    state->wallpaper[0] = '\0';
    strcpy(state->folder, "C:/IMAGES");
    state->wallpaper_mode = 0;

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) goto store_orig;

    char buffer[1024];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);

    if (bytes <= 0) goto store_orig;
    buffer[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buffer);

    state->desktop_color = (uint8_t)ini_get_color(&ini, "DESKTOP", "COLOR", state->desktop_color);

    const char *val = ini_get(&ini, "DESKTOP", "WALLPAPER");
    if (val && val[0]) {
        /* store wallpaper path */
        strncpy(state->wallpaper, val, sizeof(state->wallpaper) - 1);
        state->wallpaper[sizeof(state->wallpaper) - 1] = '\0';

        /* If the saved wallpaper contains a path, use its directory as the
         * initial folder so the dropdown will include the active wallpaper. */
        const char *last_slash = NULL;
        for (const char *p = state->wallpaper; *p; p++) {
            if (*p == '/' || *p == '\\') last_slash = p;
        }
        if (last_slash) {
            size_t dirlen = (size_t)(last_slash - state->wallpaper);
            if (dirlen >= sizeof(state->folder)) dirlen = sizeof(state->folder) - 1;
            memcpy(state->folder, state->wallpaper, dirlen);
            state->folder[dirlen] = '\0';
        }
    }

    val = ini_get(&ini, "DESKTOP", "MODE");
    if (val) {
        int mode = atoi(val);
        state->wallpaper_mode = (mode == 1) ? 1 : 0;
    }

store_orig:
    state->orig_desktop_color = state->desktop_color;
    strncpy(state->orig_wallpaper, state->wallpaper, sizeof(state->orig_wallpaper));
    state->orig_wallpaper_mode = state->wallpaper_mode;
}

static void save_settings(cpl_screen_state_t *state) {
    char read_buf[2048];
    char tmp[4096];
    char desktop_text[512];

    /* Read existing INI to preserve other sections */
    int fd = sys_open(SETTINGS_PATH, "r");
    int bytes = 0;
    if (fd >= 0) {
        bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1);
        sys_close(fd);
    }
    if (bytes > 0) read_buf[bytes] = '\0';
    else read_buf[0] = '\0';

    /* Build DESKTOP section text (CRLF terminated) */
    snprintf(desktop_text, sizeof(desktop_text),
        "[DESKTOP]\r\n"
        "COLOR=%d\r\n"
        "WALLPAPER=%s\r\n"
        "MODE=%d\r\n",
        state->desktop_color,
        state->wallpaper,
        state->wallpaper_mode
    );

    /* Replace or insert only the DESKTOP section; leave THEME and others untouched */
    if (ini_replace_section(read_buf, "DESKTOP", desktop_text, tmp, sizeof(tmp)) < 0) return;

    /* Write final content */
    fd = sys_open(SETTINGS_PATH, "w");
    if (fd >= 0) {
        sys_write_file(fd, tmp, strlen(tmp));
        sys_close(fd);
    }
}

static int scan_bmp_files(cpl_screen_state_t *state) {
    sys_dirent_t entries[64];
    int n = sys_readdir(state->folder, entries, 64);
    if (n <= 0) return 0;

    state->bmp_count = 0;
    for (int i = 0; i < n && state->bmp_count < MAX_BMP_FILES; i++) {
        if (!entries[i].is_directory) {
            const char *name = entries[i].name;
            int len = strlen(name);
            if (len > 4 && 
                (name[len-4] == '.' || name[len-4] == '.') &&
                ((name[len-3] == 'b' || name[len-3] == 'B') &&
                 (name[len-2] == 'm' || name[len-2] == 'M') &&
                 (name[len-1] == 'p' || name[len-1] == 'P'))) {
                strncpy(state->bmp_files[state->bmp_count], name, 63);
                state->bmp_files[state->bmp_count][63] = '\0';
                state->bmp_count++;
            }
        }
    }
    return state->bmp_count;
}

static void update_dropdown_list(cpl_screen_state_t *state) {
    gui_control_t *drop = sys_win_get_control(state->form, CTRL_DROP_IMAGES);
    if (!drop) return;

    int pos = 0;
    int selected_idx = 0;
    
    /* Add "(none)" as first option */
    strcpy(drop->text, "(none)");
    pos = 6;
    
    /* Extract just the filename from the full wallpaper path */
    const char *current_filename = 0;
    if (state->wallpaper[0]) {
        const char *last_slash = state->wallpaper;
        for (const char *p = state->wallpaper; *p; p++) {
            if (*p == '/' || *p == '\\') last_slash = p + 1;
        }
        current_filename = last_slash;
    } else {
        /* No wallpaper means "(none)" is selected */
        selected_idx = 0;
    }
    
    for (int i = 0; i < state->bmp_count && i < MAX_BMP_FILES; i++) {
        if (pos < 255) {
            drop->text[pos++] = '|';
        }
        int len = strlen(state->bmp_files[i]);
        if (pos + len < 255) {
            strcpy(&drop->text[pos], state->bmp_files[i]);
            pos += len;
        }
        
        /* Check if this file matches the current wallpaper */
        if (current_filename && strcmp(state->bmp_files[i], current_filename) == 0) {
            selected_idx = i + 1; /* +1 because (none) is at index 0 */
        }
    }
    drop->text[pos] = '\0';
    drop->item_count = state->bmp_count + 1; /* +1 for (none) */
    drop->cursor_pos = selected_idx;
}

static int cpl_screen_init(prog_instance_t *inst) {
    cpl_screen_state_t *state = sys_malloc(sizeof(cpl_screen_state_t));
    if (!state) return -1;

    inst->user_data = state;
    load_settings(state);
    scan_bmp_files(state);

    state->form = sys_win_create_form("Screen", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/SCREEN.ICO");

    static gui_control_t controls[] = {
        { .type = CTRL_PICTUREBOX, .x = 42,  .y = 12,  .w = 162, .h = 144, .fg = 5,  .bg = -1, .id = CTRL_PIC_MONITOR, .text = "C:/OSLET/MONITOR.BMP" },
        { .type = CTRL_PICTUREBOX, .x = 57,  .y = 26,  .w = 133, .h = 100, .fg = 0,  .bg = 0,  .id = CTRL_PIC_PREVIEW,  .text = "" },
        { .type = CTRL_FRAME,      .x = 6,   .y = 166, .w = 234, .h = 92,  .fg = 0,  .bg = 7,  .id = CTRL_FRAME_WALLPAPER, .text = "Wallpaper" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 187,                     .fg = 0,  .bg = -1, .id = CTRL_LBL_FOLDER,      .text = "Folder:" },
        { .type = CTRL_TEXTBOX,    .x = 58,  .y = 186, .w = 138, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_TXT_FOLDER,     .max_length = 127, .text = "" },
        { .type = CTRL_BUTTON,     .x = 198, .y = 186, .w = 35,  .h = 18,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_FOLDER_OK, .text = "OK" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 209,                     .fg = 0,  .bg = -1, .id = CTRL_LBL_CHOOSE,     .text = "Choose an image:" },
        { .type = CTRL_DROPDOWN,   .x = 13,  .y = 229, .w = 120, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_IMAGES,    .cursor_pos = 0, .item_count = 0, .text = "" },
        { .type = CTRL_RADIOBUTTON,.x = 161, .y = 216, .w = 12,  .h = 12,  .fg = 0,  .bg = 7,  .id = CTRL_RADIO_STRETCH,  .checked = 0, .text = "Stretch" },
        { .type = CTRL_RADIOBUTTON,.x = 161, .y = 234, .w = 12,  .h = 12,  .fg = 0,  .bg = 7,  .id = CTRL_RADIO_CENTER,   .checked = 0, .text = "Center" },
        { .type = CTRL_FRAME,      .x = 6,   .y = 258, .w = 234, .h = 48,  .fg = 0,  .bg = 7,  .id = CTRL_FRAME_COLOR,     .text = "Colour" },
        { .type = CTRL_DROPDOWN,   .x = 13,  .y = 278, .w = 120, .h = 18,  .fg = 0,  .bg = 15, .id = CTRL_DROP_COLOR,     .cursor_pos = 0, .item_count = 16, .text = "" },
        { .type = CTRL_BUTTON,     .x = 36,  .y = 312, .w = 55,  .h = 22,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_APPLY,      .text = "Apply" },
        { .type = CTRL_BUTTON,     .x = 96,  .y = 312, .w = 55,  .h = 22,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_OK,         .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 156, .y = 312, .w = 55,  .h = 22,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_CANCEL,     .text = "Cancel" },
    };

    for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++) {
        sys_win_add_control(state->form, &controls[i]);
    }

    /* dynamic initialization that depends on runtime state */
    gui_control_t *pp = sys_win_get_control(state->form, CTRL_PIC_PREVIEW);
    if (pp) pp->bg = state->desktop_color;

    /* prefill folder textbox and preview image */
    ctrl_set_text(state->form, CTRL_TXT_FOLDER, state->folder);
    if (state->wallpaper[0]) ctrl_set_image(state->form, CTRL_PIC_PREVIEW, state->wallpaper);

    /* populate colour dropdown and set selection */
    ctrl_set_text(state->form, CTRL_DROP_COLOR, color_options);
    gui_control_t *dc = sys_win_get_control(state->form, CTRL_DROP_COLOR);
    if (dc) { dc->cursor_pos = state->desktop_color; dc->item_count = 16; }

    /* radio buttons reflect the current wallpaper mode */
    ctrl_set_checked(state->form, CTRL_RADIO_STRETCH, (state->wallpaper_mode == 1));
    ctrl_set_checked(state->form, CTRL_RADIO_CENTER,  (state->wallpaper_mode == 0));

    /* picturebox image-mode: stretch vs center */
    sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, state->wallpaper_mode);

    update_dropdown_list(state);
    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static void apply_settings(cpl_screen_state_t *state) {
    /* Read values from controls */
    gui_control_t *drop_color = sys_win_get_control(state->form, CTRL_DROP_COLOR);
    if (drop_color) {
        state->desktop_color = (uint8_t)drop_color->cursor_pos;
    }

    gui_control_t *radio_stretch = sys_win_get_control(state->form, CTRL_RADIO_STRETCH);
    if (radio_stretch && radio_stretch->checked) {
        state->wallpaper_mode = 1;
    } else {
        state->wallpaper_mode = 0;
    }

    /* Get selected image from dropdown */
    gui_control_t *drop_images = sys_win_get_control(state->form, CTRL_DROP_IMAGES);
    if (drop_images && drop_images->item_count > 0) {
        if (drop_images->cursor_pos == 0) {
            /* "(none)" selected */
            state->wallpaper[0] = '\0';
        } else if (drop_images->cursor_pos - 1 < state->bmp_count) {
            /* File selected (subtract 1 for (none) offset) */
            snprintf(state->wallpaper, sizeof(state->wallpaper), "%s/%s", 
                     state->folder, state->bmp_files[drop_images->cursor_pos - 1]);
        }
    }

    /* Update preview background color */
    gui_control_t *pic_preview = sys_win_get_control(state->form, CTRL_PIC_PREVIEW);
    if (pic_preview) {
        pic_preview->bg = state->desktop_color;
    }

    /* Update preview control image-mode (center vs stretch) */
    sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, state->wallpaper_mode);

    save_settings(state);
    desktop_apply_settings(state->desktop_color, state->wallpaper, state->wallpaper_mode);
}

static int cpl_screen_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;
    cpl_screen_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    if (event == -1 || event == -2) {
        /* Check if dropdown selection changed and update preview accordingly */
        gui_control_t *drop = sys_win_get_control(state->form, CTRL_DROP_IMAGES);
        gui_control_t *dropcol = sys_win_get_control(state->form, CTRL_DROP_COLOR);
        int changed = 0;

        if (drop) {
            char new_wallpaper[128];
            if (drop->cursor_pos == 0) {
                new_wallpaper[0] = '\0';
            } else if (drop->cursor_pos - 1 < state->bmp_count) {
                snprintf(new_wallpaper, sizeof(new_wallpaper), "%s/%s", 
                         state->folder, state->bmp_files[drop->cursor_pos - 1]);
            } else {
                new_wallpaper[0] = '\0';
            }
            
            /* Only update if wallpaper changed */
            if (strcmp(state->wallpaper, new_wallpaper) != 0) {
                strcpy(state->wallpaper, new_wallpaper);
                ctrl_set_image(state->form, CTRL_PIC_PREVIEW, state->wallpaper);
                changed = 1;
            }
        }

        /* Check color dropdown and update preview only if it actually changed */
        if (dropcol) {
            uint8_t new_color = (uint8_t)dropcol->cursor_pos;
            if (state->desktop_color != new_color) {
                state->desktop_color = new_color;
                gui_control_t *pic_preview = sys_win_get_control(state->form, CTRL_PIC_PREVIEW);
                if (pic_preview) pic_preview->bg = new_color;
                changed = 1;
            }
        }

        /* Read current radio button state for live preview */
        gui_control_t *radio_stretch = sys_win_get_control(state->form, CTRL_RADIO_STRETCH);
        int mode = (radio_stretch && radio_stretch->checked) ? 1 : 0;
        sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, mode);

        return changed ? PROG_EVENT_REDRAW : PROG_EVENT_NONE;
    }

    /* Folder OK button */
    if (event == CTRL_BTN_FOLDER_OK) {
        const char *folder = ctrl_get_text(state->form, CTRL_TXT_FOLDER);
        if (folder) {
            strncpy(state->folder, folder, sizeof(state->folder) - 1);
            state->folder[sizeof(state->folder) - 1] = '\0';
            scan_bmp_files(state);
            update_dropdown_list(state);
            sys_win_draw(state->form);
        }
        return PROG_EVENT_HANDLED;
    }

    /* Dropdown selection changed */
    if (event == CTRL_DROP_IMAGES) {
        /* Handled earlier */
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Radio button toggled */
    if (event == CTRL_RADIO_STRETCH) {
        ctrl_set_checked(state->form, CTRL_RADIO_CENTER, 0);
        ctrl_set_checked(state->form, CTRL_RADIO_STRETCH, 1);
        /* Preview should show stretched image */
        sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, 1);
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    if (event == CTRL_RADIO_CENTER) {
        ctrl_set_checked(state->form, CTRL_RADIO_STRETCH, 0);
        ctrl_set_checked(state->form, CTRL_RADIO_CENTER, 1);
        /* Preview should show centered (preserve aspect) image */
        sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, 0);
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Color dropdown changed */
    if (event == CTRL_DROP_COLOR) {
        /* Handled earlier */
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Apply button */
    if (event == CTRL_BTN_APPLY) {
        apply_settings(state);
        state->orig_desktop_color = state->desktop_color;
        strncpy(state->orig_wallpaper, state->wallpaper, sizeof(state->orig_wallpaper));
        state->orig_wallpaper_mode = state->wallpaper_mode;
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* OK button */
    if (event == CTRL_BTN_OK) {
        apply_settings(state);
        return PROG_EVENT_CLOSE;
    }

    /* Cancel button */
    if (event == CTRL_BTN_CANCEL) {
        state->desktop_color = state->orig_desktop_color;
        strncpy(state->wallpaper, state->orig_wallpaper, sizeof(state->wallpaper));
        state->wallpaper_mode = state->orig_wallpaper_mode;
        desktop_apply_settings(state->desktop_color, state->wallpaper, state->wallpaper_mode);
        return PROG_EVENT_CLOSE;
    }

    return PROG_EVENT_NONE;
}

static void cpl_screen_cleanup(prog_instance_t *inst) {
    cpl_screen_state_t *state = inst->user_data;
    if (state) {
        if (state->form) {
            prog_unregister_window(inst, state->form);
            sys_win_destroy_form(state->form);
        }
        sys_free(state);
        inst->user_data = 0;
    }
}
