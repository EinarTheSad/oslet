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
    state->wallpaper_mode = 0; /* center by default */

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
        strncpy(state->wallpaper, val, sizeof(state->wallpaper) - 1);
        state->wallpaper[sizeof(state->wallpaper) - 1] = '\0';
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
    char buffer[512];
    int len = snprintf(buffer, sizeof(buffer),
        "[DESKTOP]\r\n"
        "COLOR=%d\r\n"
        "WALLPAPER=%s\r\n"
        "MODE=%d\r\n",
        state->desktop_color,
        state->wallpaper,
        state->wallpaper_mode
    );

    int fd = sys_open(SETTINGS_PATH, "w");
    if (fd >= 0) {
        sys_write_file(fd, buffer, len);
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

static void update_preview(cpl_screen_state_t *state) {
    if (state->wallpaper[0]) {
        ctrl_set_image(state->form, CTRL_PIC_PREVIEW, state->wallpaper);
    } else {
        ctrl_set_image(state->form, CTRL_PIC_PREVIEW, "");
    }
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

    /* Monitor picture */
    gui_control_t pic_monitor = {0};
    pic_monitor.type = CTRL_PICTUREBOX;
    pic_monitor.x = 42;
    pic_monitor.y = 12;
    pic_monitor.w = 162;
    pic_monitor.h = 144;
    pic_monitor.fg = 5;
    pic_monitor.bg = -1;
    pic_monitor.id = CTRL_PIC_MONITOR;
    strcpy(pic_monitor.text, "C:/OSLET/MONITOR.BMP");
    sys_win_add_control(state->form, &pic_monitor);

    /* Preview picture (inside monitor) */
    gui_control_t pic_preview = {0};
    pic_preview.type = CTRL_PICTUREBOX;
    pic_preview.x = 57;
    pic_preview.y = 26;
    pic_preview.w = 133;
    pic_preview.h = 100;
    pic_preview.fg = 0;
    pic_preview.bg = state->desktop_color;
    pic_preview.id = CTRL_PIC_PREVIEW;
    pic_preview.text[0] = '\0';
    sys_win_add_control(state->form, &pic_preview);

    /* PROP_ENABLED works as a switch between stretched and non-stretched in PictureBoxes */
    sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, state->wallpaper_mode);

    /* Wallpaper frame */
    gui_control_t frame_wp = {0};
    frame_wp.type = CTRL_FRAME;
    frame_wp.x = 6;
    frame_wp.y = 168;
    frame_wp.w = 234;
    frame_wp.h = 90;
    frame_wp.fg = 0;
    frame_wp.bg = 7;
    frame_wp.id = CTRL_FRAME_WALLPAPER;
    strcpy(frame_wp.text, "Wallpaper");
    sys_win_add_control(state->form, &frame_wp);

    /* Folder label */
    gui_control_t lbl_folder = {0};
    lbl_folder.type = CTRL_LABEL;
    lbl_folder.x = 12;
    lbl_folder.y = 186;
    lbl_folder.w = 0;
    lbl_folder.h = 0;
    lbl_folder.fg = 0;
    lbl_folder.bg = 15;
    lbl_folder.id = CTRL_LBL_FOLDER;
    strcpy(lbl_folder.text, "Folder:");
    sys_win_add_control(state->form, &lbl_folder);

    /* Folder textbox */
    gui_control_t txt_folder = {0};
    txt_folder.type = CTRL_TEXTBOX;
    txt_folder.x = 66;
    txt_folder.y = 186;
    txt_folder.w = 130;
    txt_folder.h = 20;
    txt_folder.fg = 0;
    txt_folder.bg = 15;
    txt_folder.id = CTRL_TXT_FOLDER;
    txt_folder.max_length = 127;
    strncpy(txt_folder.text, state->folder, sizeof(txt_folder.text) - 1);
    sys_win_add_control(state->form, &txt_folder);

    /* Folder OK button */
    gui_control_t btn_folder_ok = {0};
    btn_folder_ok.type = CTRL_BUTTON;
    btn_folder_ok.x = 198;
    btn_folder_ok.y = 186;
    btn_folder_ok.w = 35;
    btn_folder_ok.h = 20;
    btn_folder_ok.fg = 0;
    btn_folder_ok.bg = 7;
    btn_folder_ok.id = CTRL_BTN_FOLDER_OK;
    strcpy(btn_folder_ok.text, "OK");
    sys_win_add_control(state->form, &btn_folder_ok);

    /* Choose label */
    gui_control_t lbl_choose = {0};
    lbl_choose.type = CTRL_LABEL;
    lbl_choose.x = 12;
    lbl_choose.y = 210;
    lbl_choose.w = 0;
    lbl_choose.h = 0;
    lbl_choose.fg = 0;
    lbl_choose.bg = 15;
    lbl_choose.id = CTRL_LBL_CHOOSE;
    strcpy(lbl_choose.text, "Choose an image:");
    sys_win_add_control(state->form, &lbl_choose);

    /* Images dropdown */
    gui_control_t drop_images = {0};
    drop_images.type = CTRL_DROPDOWN;
    drop_images.x = 12;
    drop_images.y = 228;
    drop_images.w = 120;
    drop_images.h = 20;
    drop_images.fg = 0;
    drop_images.bg = 15;
    drop_images.id = CTRL_DROP_IMAGES;
    drop_images.cursor_pos = 0;
    drop_images.item_count = 0;
    sys_win_add_control(state->form, &drop_images);
    update_dropdown_list(state);

    /* Radio buttons */
    gui_control_t radio_stretch = {0};
    radio_stretch.type = CTRL_RADIOBUTTON;
    radio_stretch.x = 162;
    radio_stretch.y = 216;
    radio_stretch.w = 12;
    radio_stretch.h = 12;
    radio_stretch.fg = 0;
    radio_stretch.bg = 7;
    radio_stretch.id = CTRL_RADIO_STRETCH;
    radio_stretch.checked = (state->wallpaper_mode == 1) ? 1 : 0;
    strcpy(radio_stretch.text, "Stretch");
    sys_win_add_control(state->form, &radio_stretch);

    gui_control_t radio_center = {0};
    radio_center.type = CTRL_RADIOBUTTON;
    radio_center.x = 162;
    radio_center.y = 234;
    radio_center.w = 12;
    radio_center.h = 12;
    radio_center.fg = 0;
    radio_center.bg = 7;
    radio_center.id = CTRL_RADIO_CENTER;
    radio_center.checked = (state->wallpaper_mode == 0) ? 1 : 0;
    strcpy(radio_center.text, "Center");
    sys_win_add_control(state->form, &radio_center);

    /* Color frame */
    gui_control_t frame_color = {0};
    frame_color.type = CTRL_FRAME;
    frame_color.x = 6;
    frame_color.y = 258;
    frame_color.w = 234;
    frame_color.h = 48;
    frame_color.fg = 0;
    frame_color.bg = 7;
    frame_color.id = CTRL_FRAME_COLOR;
    strcpy(frame_color.text, "Colour");
    sys_win_add_control(state->form, &frame_color);

    /* Color dropdown */
    gui_control_t drop_color = {0};
    drop_color.type = CTRL_DROPDOWN;
    drop_color.x = 12;
    drop_color.y = 276;
    drop_color.w = 120;
    drop_color.h = 20;
    drop_color.fg = 0;
    drop_color.bg = 15;
    drop_color.id = CTRL_DROP_COLOR;
    drop_color.cursor_pos = state->desktop_color;
    drop_color.item_count = 16;
    strncpy(drop_color.text, color_options, sizeof(drop_color.text) - 1);
    sys_win_add_control(state->form, &drop_color);

    /* Buttons */
    gui_control_t btn_apply = {0};
    btn_apply.type = CTRL_BUTTON;
    btn_apply.x = 36;
    btn_apply.y = 312;
    btn_apply.w = 55;
    btn_apply.h = 22;
    btn_apply.fg = 0;
    btn_apply.bg = 7;
    btn_apply.id = CTRL_BTN_APPLY;
    strcpy(btn_apply.text, "Apply");
    sys_win_add_control(state->form, &btn_apply);

    gui_control_t btn_ok = {0};
    btn_ok.type = CTRL_BUTTON;
    btn_ok.x = 96;
    btn_ok.y = 312;
    btn_ok.w = 55;
    btn_ok.h = 22;
    btn_ok.fg = 0;
    btn_ok.bg = 7;
    btn_ok.id = CTRL_BTN_OK;
    strcpy(btn_ok.text, "OK");
    sys_win_add_control(state->form, &btn_ok);

    gui_control_t btn_cancel = {0};
    btn_cancel.type = CTRL_BUTTON;
    btn_cancel.x = 156;
    btn_cancel.y = 312;
    btn_cancel.w = 55;
    btn_cancel.h = 22;
    btn_cancel.fg = 0;
    btn_cancel.bg = 7;
    btn_cancel.id = CTRL_BTN_CANCEL;
    strcpy(btn_cancel.text, "Cancel");
    sys_win_add_control(state->form, &btn_cancel);

    update_preview(state);
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
