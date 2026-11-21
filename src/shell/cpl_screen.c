#include "progmod.h"
#include "progman.h"
#include "cpl_screen.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdlib.h"
#include "../lib/ini.h"
#include "../lib/stdio.h"
#include "../lib/display.h"
#include "../win/wm_config.h"

#ifndef SETTINGS_PATH
#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"
#endif
#ifndef DISPLAY_TRIAL
#define DISPLAY_TRIAL "C:/OSLET/VMODE.TRY"
#endif
#define MAX_BMP_FILES 32

extern void desktop_apply_settings(uint8_t color, const char *wallpaper, uint8_t wallpaper_mode);
extern int desktop_refresh_display(void);
extern void desktop_present_display(void);

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
#define CTRL_RADIO_TILE      15
#define CTRL_FRAME_COLOR     16
#define CTRL_DROP_COLOR      17
#define CTRL_DROP_MODE       18
#define CTRL_FRAME_RESOLUTION 19
#define CTRL_DROP_RESOLUTION 22
#define CTRL_DROP_DEPTH      24
#define MAX_VBE_MODES        64
#define MAX_RESOLUTIONS      8

#define WIN_WIDTH  246
#define WIN_HEIGHT 403
#define WIN_X      ((WM_SCREEN_WIDTH - WIN_WIDTH) / 2)
#define WIN_Y      ((WM_SCREEN_HEIGHT - WIN_HEIGHT) / 2)

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
    int wallpaper_mode; /* 0=center, 1=stretch, 2=tile */
    sys_vbe_state_t display;
    sys_vbe_mode_t modes[MAX_VBE_MODES];
    int mode_count;
    struct { uint32_t width, height; } resolutions[MAX_RESOLUTIONS];
    int resolution_count;
    uint32_t depths[4];
    int depth_count;
    /* Originals for cancel */
    uint8_t orig_desktop_color;
    char orig_wallpaper[128];
    int orig_wallpaper_mode;
} cpl_screen_state_t;

static int cpl_screen_init(prog_instance_t *inst);
static int cpl_screen_event(prog_instance_t *inst, int win_idx,
                            const sys_win_event_t *event);
static void cpl_screen_cleanup(prog_instance_t *inst);
static int file_exists(const char *path);

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
    state->wallpaper_mode = 0; /* centre default */

    char *buffer = ini_read_file(SETTINGS_PATH);
    if (!buffer) goto store_orig;

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
        if (mode == 1) {
            state->wallpaper_mode = 1;
        } else if (mode == 2) {
            state->wallpaper_mode = 2;
        } else {
            state->wallpaper_mode = 0;
        }
    }
    free(buffer);

store_orig:
    state->orig_desktop_color = state->desktop_color;
    strncpy(state->orig_wallpaper, state->wallpaper, sizeof(state->orig_wallpaper));
    state->orig_wallpaper_mode = state->wallpaper_mode;
}

static int save_settings(cpl_screen_state_t *state) {
    char desktop_text[512];

    snprintf(desktop_text, sizeof(desktop_text),
        "[DESKTOP]\r\n"
        "COLOR=%d\r\n"
        "WALLPAPER=%s\r\n"
        "MODE=%d\r\n",
        state->desktop_color,
        state->wallpaper,
        state->wallpaper_mode
    );

    return ini_save_section(SETTINGS_PATH, "DESKTOP", desktop_text);
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

static const uint32_t depths[] = {4, 8, 24};
static const char *depth_names[] = {
    "16 colours", "256 colours", "True Colour (24 bits)"
};
static const struct { uint32_t width, height; } sizes[] = {
    {640, 480}, {800, 600}, {1024, 768}, {1280, 960},
    {1280, 720}, {1366, 768}, {1920, 1080}
};

static int has_mode(cpl_screen_state_t *state, uint32_t width, uint32_t height,
                    uint32_t depth) {
    for (int i = 0; i < state->mode_count; ++i) {
        sys_vbe_mode_t *mode = &state->modes[i];
        if (mode->width == width && mode->height == height && mode->depth == depth)
            return 1;
    }
    return 0;
}

static int set_depths(cpl_screen_state_t *state, uint32_t selected) {
    gui_control_t *size = sys_win_get_control(state->form, CTRL_DROP_RESOLUTION);
    gui_control_t *depth = sys_win_get_control(state->form, CTRL_DROP_DEPTH);
    if (!size || !depth || size->dropdown.cursor_pos >= state->resolution_count)
        return -1;
    int index = size->dropdown.cursor_pos;
    uint32_t width = state->resolutions[index].width;
    uint32_t height = state->resolutions[index].height;
    int current = width == state->display.width && height == state->display.height;
    char text[160] = "";
    size_t used = 0;
    int active = -1;
    state->depth_count = 0;
    for (unsigned i = 0; i < sizeof(depths) / sizeof(depths[0]); ++i) {
        if (!has_mode(state, width, height, depths[i]) &&
            !(current && depths[i] == state->display.depth)) continue;
        int n = snprintf(text + used, sizeof(text) - used, "%s%s",
                         state->depth_count ? "|" : "", depth_names[i]);
        if (n < 0 || (size_t)n >= sizeof(text) - used) return -1;
        used += (size_t)n;
        state->depths[state->depth_count] = depths[i];
        if (depths[i] == selected) active = state->depth_count;
        ++state->depth_count;
    }
    if (current && (state->display.depth == 16 || state->display.depth == 32)) {
        int n = snprintf(text + used, sizeof(text) - used, "%sCurrent (%u bits)",
                         state->depth_count ? "|" : "", state->display.depth);
        if (n < 0 || (size_t)n >= sizeof(text) - used) return -1;
        state->depths[state->depth_count] = state->display.depth;
        if (selected == state->display.depth) active = state->depth_count;
        ++state->depth_count;
    }
    if (!state->depth_count) return -1;
    ctrl_set_text(state->form, CTRL_DROP_DEPTH, text);
    depth->dropdown.item_count = state->depth_count;
    depth->dropdown.cursor_pos = active >= 0 ? active : 0;
    return 0;
}

static int refresh_display_options(cpl_screen_state_t *state, int discover) {
    int discovery_failed = 0;
    if (sys_gfx_vbe_state(&state->display) != 0) return -1;
    if (discover) {
        int count = sys_gfx_vbe_discover();
        state->mode_count = 0;
        if (count < 0 || count > MAX_VBE_MODES) {
            discovery_failed = 1;
        } else {
            for (int i = 0; i < count; ++i) {
                if (sys_gfx_vbe_get_mode((unsigned)i, &state->modes[i]) != 0) {
                    state->mode_count = 0;
                    discovery_failed = 1;
                    break;
                }
                state->mode_count++;
            }
        }
    }
    state->resolution_count = 0;
    int active = -1;
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
        int current = sizes[i].width == state->display.width &&
                      sizes[i].height == state->display.height;
        int supported = current;
        for (unsigned j = 0; j < sizeof(depths) / sizeof(depths[0]); ++j)
            if (has_mode(state, sizes[i].width, sizes[i].height, depths[j]))
                supported = 1;
        if (!supported) continue;
        int n = state->resolution_count++;
        state->resolutions[n].width = sizes[i].width;
        state->resolutions[n].height = sizes[i].height;
        if (current) active = n;
    }
    if (active < 0) {
        if (state->resolution_count == MAX_RESOLUTIONS) return -1;
        active = state->resolution_count++;
        state->resolutions[active].width = state->display.width;
        state->resolutions[active].height = state->display.height;
    }
    char options[128] = "";
    size_t used = 0;
    for (int i = 0; i < state->resolution_count; ++i) {
        int n = snprintf(options + used, sizeof(options) - used, "%s%ux%u", i ? "|" : "",
                         state->resolutions[i].width, state->resolutions[i].height);
        if (n < 0 || (size_t)n >= sizeof(options) - used) return -1;
        used += (size_t)n;
    }
    ctrl_set_text(state->form, CTRL_DROP_RESOLUTION, options);
    gui_control_t *resolution = sys_win_get_control(state->form, CTRL_DROP_RESOLUTION);
    if (!resolution) return -1;
    resolution->dropdown.item_count = state->resolution_count;
    resolution->dropdown.cursor_pos = active;
    if (set_depths(state, state->display.depth) != 0) return -1;
    return discovery_failed ? -1 : 0;
}

static void update_dropdown_list(cpl_screen_state_t *state) {
    gui_control_t *drop = sys_win_get_control(state->form, CTRL_DROP_IMAGES);
    if (!drop) return;

    int pos = 6;
    int selected_idx = 0;
    strcpy(drop->text, "(none)");

    const char *current_filename = 0;
    if (state->wallpaper[0]) {
        const char *last_slash = state->wallpaper;
        for (const char *p = state->wallpaper; *p; p++) {
            if (*p == '/' || *p == '\\') last_slash = p + 1;
        }
        current_filename = last_slash;
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
        
        if (current_filename && strcmp(state->bmp_files[i], current_filename) == 0) {
            selected_idx = i + 1;
        }
    }
    drop->text[pos] = '\0';
    drop->dropdown.item_count = state->bmp_count + 1;
    drop->dropdown.cursor_pos = selected_idx;
}

static int cpl_screen_init(prog_instance_t *inst) {
    cpl_screen_state_t *state = sys_malloc(sizeof(cpl_screen_state_t));
    if (!state) return -1;

    memset(state, 0, sizeof(*state));
    inst->user_data = state;
    load_settings(state);
    scan_bmp_files(state);

    state->form = sys_win_create_form("Screen", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/SCREEN.ICO");
    sys_win_set_resizable(state->form, 0);

    static gui_control_t controls[] = {
        { .type = CTRL_PICTUREBOX, .x = 42,  .y = 12,  .w = 162, .h = 144, .fg = 5,  .bg = -1, .id = CTRL_PIC_MONITOR, .text = "C:/OSLET/MONITOR.BMP" },
        { .type = CTRL_PICTUREBOX, .x = 57,  .y = 26,  .w = 133, .h = 100, .fg = 0,  .bg = 0,  .id = CTRL_PIC_PREVIEW,  .text = "" },
        { .type = CTRL_FRAME,      .x = 6,   .y = 160, .w = 234, .h = 92, .fg = -1,  .bg = 7,  .id = CTRL_FRAME_WALLPAPER, .text = "Wallpaper" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 181,                     .fg = -1,  .bg = -1, .id = CTRL_LBL_FOLDER,      .text = "Folder:" },
        { .type = CTRL_TEXTBOX,    .x = 58,  .y = 180, .w = 138, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_TXT_FOLDER,     .textbox = { .max_length = 127 }, .text = "" },
        { .type = CTRL_BUTTON,     .x = 198, .y = 180, .w = 35,  .h = 18,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_FOLDER_OK, .text = "OK" },
        { .type = CTRL_LABEL,      .x = 12,  .y = 203,                     .fg = -1,  .bg = -1, .id = CTRL_LBL_CHOOSE,     .text = "Choose an image:" },
        { .type = CTRL_DROPDOWN,   .x = 13,  .y = 223, .w = 128, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_IMAGES,    .dropdown = { .cursor_pos = 0, .item_count = 0 }, .text = "" },
        { .type = CTRL_LABEL,      .x = 160, .y = 203,                     .fg = -1,  .bg = -1, .id = 0,                   .text = "Mode:" },
        { .type = CTRL_DROPDOWN,   .x = 161, .y = 223, .w = 72,  .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_MODE,      .dropdown = { .cursor_pos = 0, .item_count = 3 }, .text = "Center|Stretch|Tile" },
        { .type = CTRL_FRAME,      .x = 6,   .y = 252, .w = 234, .h = 48,  .fg = -1,  .bg = 7,  .id = CTRL_FRAME_COLOR,     .text = "Desktop colour" },
        { .type = CTRL_DROPDOWN,   .x = 13,  .y = 272, .w = 128, .h = 18,  .fg = -1,  .bg = -1, .id = CTRL_DROP_COLOR,     .dropdown = { .cursor_pos = 0, .item_count = 16 }, .text = "" },
        { .type = CTRL_FRAME,      .x = 6,   .y = 300, .w = 234, .h = 48,  .fg = -1,  .bg = 7,  .id = CTRL_FRAME_RESOLUTION, .text = "Resolution" },
        { .type = CTRL_DROPDOWN,   .x = 13,  .y = 320, .w = 82,  .h = 18, .fg = -1,  .bg = -1, .id = CTRL_DROP_RESOLUTION, .dropdown = { .cursor_pos = 0, .item_count = 0 }, .text = "" },
        { .type = CTRL_DROPDOWN,   .x = 101, .y = 320, .w = 132, .h = 18, .fg = -1,  .bg = -1, .id = CTRL_DROP_DEPTH,      .dropdown = { .cursor_pos = 0, .item_count = 0 }, .text = "" },
        { .type = CTRL_BUTTON,     .x = 36,  .y = 354, .w = 55,  .h = 22,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_APPLY,      .text = "Apply" },
        { .type = CTRL_BUTTON,     .x = 96,  .y = 354, .w = 55,  .h = 22,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_OK,         .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 156, .y = 354, .w = 55,  .h = 22,  .fg = 0,  .bg = -1,  .id = CTRL_BTN_CANCEL,     .text = "Cancel" },
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
    if (dc) { dc->dropdown.cursor_pos = state->desktop_color; dc->dropdown.item_count = 16; }

    /* dropdown reflects the current wallpaper mode */
    gui_control_t *drop_mode = sys_win_get_control(state->form, CTRL_DROP_MODE);
    if (drop_mode) { drop_mode->dropdown.cursor_pos = state->wallpaper_mode; }

    if (refresh_display_options(state, 1) != 0)
        sys_win_msgbox("Could not discover display modes.", "OK", "Screen");

    /* picturebox preview always uses centered mode */
    sys_ctrl_set_prop(state->form, CTRL_PIC_PREVIEW, PROP_ENABLED, 0);

    update_dropdown_list(state);
    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static int apply_display_mode(cpl_screen_state_t *state, int resolution, int depth_index) {
    uint32_t width = state->resolutions[resolution].width;
    uint32_t height = state->resolutions[resolution].height;
    uint32_t depth = state->depths[depth_index];
    if (width != state->display.width || height != state->display.height ||
        depth != state->display.depth) {
        int mode = -1;
        for (int i = 0; i < state->mode_count; ++i) {
            if (state->modes[i].width == width && state->modes[i].height == height &&
                state->modes[i].depth == depth) {
                mode = i;
                break;
            }
        }
        if (mode < 0) {
            sys_win_msgbox("That resolution and colour depth are not available.",
                           "OK", "Screen");
            return -1;
        }
        int try_result = sys_gfx_vbe_try((unsigned)mode);
        if (try_result != 0) {
            if (desktop_refresh_display() >= 0) desktop_present_display();
            refresh_display_options(state, 0);
            char message[96];
            snprintf(message, sizeof(message),
                     "Could not set the selected display mode (error %d).", try_result);
            sys_win_msgbox(message, "OK", "Screen");
            return -1;
        }
        if (desktop_refresh_display() < 0) {
            int revert_result = sys_gfx_vbe_revert();
            sys_win_msgbox(revert_result == 0 ?
                           "Could not refresh the display. The mode was reverted." :
                           "Could not refresh the display or revert the mode.",
                           "OK", "Screen");
            return -1;
        }
        desktop_present_display();
        int answer = sys_win_display_confirm("Keep this display mode? It will revert after 15 seconds.",
                                            "Keep|Revert", "Screen");
        int result = answer == 1 ? sys_gfx_vbe_confirm() : sys_gfx_vbe_revert();
        if (desktop_refresh_display() < 0) {
            sys_win_msgbox("Could not refresh the display.", "OK", "Screen");
            return -1;
        }
        desktop_present_display();
        if (refresh_display_options(state, 0) != 0) {
            sys_win_msgbox("Could not query the display mode.", "OK", "Screen");
            return -1;
        }
        if (answer != 1 || result != 0) {
            if (answer == 1 || result != 0)
                sys_win_msgbox("The display mode was reverted.", "OK", "Screen");
            return -1;
        }
        if (screen_save_display(width, height, depth) != 0) {
            sys_win_msgbox("The display mode was kept, but could not be saved.",
                           "OK", "Screen");
            return -1;
        }
    } else {
        int trial = file_exists(DISPLAY_TRIAL);
        if (trial < 0 || (trial && screen_save_display(width, height, depth) != 0)) {
            sys_win_msgbox("Could not save display recovery.", "OK", "Screen");
            return -1;
        }
    }
    return 0;
}

static int apply_settings(cpl_screen_state_t *state) {
    /* Read values from controls */
    gui_control_t *drop_color = sys_win_get_control(state->form, CTRL_DROP_COLOR);
    if (drop_color) {
        state->desktop_color = (uint8_t)drop_color->dropdown.cursor_pos;
    }

    gui_control_t *drop_mode = sys_win_get_control(state->form, CTRL_DROP_MODE);
    if (drop_mode) {
        state->wallpaper_mode = drop_mode->dropdown.cursor_pos;
    }

    gui_control_t *drop_resolution = sys_win_get_control(state->form, CTRL_DROP_RESOLUTION);
    gui_control_t *drop_depth = sys_win_get_control(state->form, CTRL_DROP_DEPTH);
    if (!drop_resolution || !drop_depth ||
        drop_resolution->dropdown.cursor_pos >= state->resolution_count ||
        drop_depth->dropdown.cursor_pos >= state->depth_count) {
        sys_win_msgbox("No display mode is selected.", "OK", "Screen");
        return -1;
    }
    int resolution = drop_resolution->dropdown.cursor_pos;
    if (apply_display_mode(state, resolution, drop_depth->dropdown.cursor_pos) != 0) {
        return -1;
    }

    /* Get selected image from dropdown */
    gui_control_t *drop_images = sys_win_get_control(state->form, CTRL_DROP_IMAGES);
    if (drop_images && drop_images->dropdown.item_count > 0) {
        if (drop_images->dropdown.cursor_pos == 0) {
            /* "(none)" selected */
            state->wallpaper[0] = '\0';
        } else if (drop_images->dropdown.cursor_pos - 1 < state->bmp_count) {
            /* File selected (subtract 1 for (none) offset) */
            snprintf(state->wallpaper, sizeof(state->wallpaper), "%s/%s", 
                     state->folder, state->bmp_files[drop_images->dropdown.cursor_pos - 1]);
        }
    }

    /* Update preview background color */
    gui_control_t *pic_preview = sys_win_get_control(state->form, CTRL_PIC_PREVIEW);
    if (pic_preview) {
        pic_preview->bg = state->desktop_color;
    }

    int settings_result = save_settings(state);
    desktop_apply_settings(state->desktop_color, state->wallpaper, state->wallpaper_mode);

    if (settings_result == 0) {
        state->orig_desktop_color = state->desktop_color;
        strncpy(state->orig_wallpaper, state->wallpaper,
                sizeof(state->orig_wallpaper) - 1);
        state->orig_wallpaper[sizeof(state->orig_wallpaper) - 1] = '\0';
        state->orig_wallpaper_mode = state->wallpaper_mode;
    } else {
        sys_win_msgbox("Could not save wallpaper and colour settings.",
                       "OK", "Screen");
    }

    return settings_result == 0 ? 0 : -1;
}

static int cpl_screen_event(prog_instance_t *inst, int win_idx,
                            const sys_win_event_t *event) {
    (void)win_idx;
    cpl_screen_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    if (event->type == SYS_WIN_EVENT_REDRAW ||
        event->type == SYS_WIN_EVENT_WINDOW_CHANGED) {
        return PROG_EVENT_NONE;
    }
    if (event->type != SYS_WIN_EVENT_CONTROL) return PROG_EVENT_NONE;
    int id = event->id;

    /* Folder OK button */
    if (id == CTRL_BTN_FOLDER_OK) {
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

    if (id == CTRL_DROP_IMAGES) {
        gui_control_t *drop = sys_win_get_control(state->form, CTRL_DROP_IMAGES);
        if (drop) {
            char new_wallpaper[128];
            if (drop->dropdown.cursor_pos == 0) {
                new_wallpaper[0] = '\0';
            } else if (drop->dropdown.cursor_pos - 1 < state->bmp_count) {
                snprintf(new_wallpaper, sizeof(new_wallpaper), "%s/%s", 
                         state->folder, state->bmp_files[drop->dropdown.cursor_pos - 1]);
            } else {
                new_wallpaper[0] = '\0';
            }
            
            if (strcmp(state->wallpaper, new_wallpaper) != 0) {
                strcpy(state->wallpaper, new_wallpaper);
                ctrl_set_image(state->form, CTRL_PIC_PREVIEW, state->wallpaper);
            }
        }
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Wallpaper mode dropdown changed */
    if (id == CTRL_DROP_MODE) {
        gui_control_t *drop_mode = sys_win_get_control(state->form, CTRL_DROP_MODE);
        if (drop_mode) {
            state->wallpaper_mode = drop_mode->dropdown.cursor_pos;
        }
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    if (id == CTRL_DROP_RESOLUTION) {
        gui_control_t *depth = sys_win_get_control(state->form, CTRL_DROP_DEPTH);
        if (!depth || depth->dropdown.cursor_pos >= state->depth_count ||
            set_depths(state, state->depths[depth->dropdown.cursor_pos]) != 0)
            sys_win_msgbox("Could not update colour depths.", "OK", "Screen");
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Color dropdown changed */
    if (id == CTRL_DROP_COLOR) {
        gui_control_t *dropcol = sys_win_get_control(state->form, CTRL_DROP_COLOR);
        if (dropcol) {
            uint8_t new_color = (uint8_t)dropcol->dropdown.cursor_pos;
            if (state->desktop_color != new_color) {
                state->desktop_color = new_color;
                gui_control_t *pic_preview = sys_win_get_control(state->form, CTRL_PIC_PREVIEW);
                if (pic_preview) pic_preview->bg = new_color;
            }
        }
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* Apply button */
    if (id == CTRL_BTN_APPLY) {
        apply_settings(state);
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* OK button */
    if (id == CTRL_BTN_OK) {
        return apply_settings(state) == 0 ? PROG_EVENT_CLOSE : PROG_EVENT_HANDLED;
    }

    /* Cancel button */
    if (id == CTRL_BTN_CANCEL) {
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


typedef struct {
    uint32_t width, height, depth;
} saved_display_t;

static int valid_display(const saved_display_t *mode) {
    int size = (mode->width == 640 && mode->height == 480) ||
               (mode->width == 800 && mode->height == 600) ||
               (mode->width == 1024 && mode->height == 768) ||
               (mode->width == 1280 && mode->height == 960) ||
               (mode->width == 1280 && mode->height == 720) ||
               (mode->width == 1366 && mode->height == 768) ||
               (mode->width == 1920 && mode->height == 1080);
    int depth = mode->depth == 4 || mode->depth == 8 || mode->depth == 16 ||
                mode->depth == 24 || mode->depth == 32;
    return size && depth;
}

static int file_exists(const char *path) {
    int fd = sys_open(path, "r");
    if (fd >= 0) return sys_close(fd) == 0 ? 1 : -1;
    const int limit = 64;
    sys_dirent_t *entries = sys_malloc(sizeof(*entries) * limit);
    if (!entries) return -1;
    int count = sys_readdir("C:/OSLET", entries, limit);
    const char *name = path;
    for (const char *p = path; *p; ++p)
        if (*p == '/') name = p + 1;
    int result = count >= 0 && count < limit ? 0 : -1;
    for (int i = 0; i < count && i < limit; ++i) {
        if (strcasecmp(entries[i].name, name) == 0) {
            result = 1;
            break;
        }
    }
    sys_free(entries);
    return result;
}

static int remove_if_present(const char *path) {
    int exists = file_exists(path);
    if (exists < 0) return -1;
    return exists ? sys_unlink(path) : 0;
}

static int write_display_trial(void) {
    const uint8_t marker = 1;
    uint8_t saved;
    int fd = sys_open(DISPLAY_TRIAL, "w");
    if (fd < 0) return -1;
    int written = sys_write_file(fd, &marker, sizeof(marker));
    int closed = sys_close(fd);
    if (written != (int)sizeof(marker) || closed != 0) return -1;
    fd = sys_open(DISPLAY_TRIAL, "r");
    if (fd < 0) return -1;
    int length = sys_read(fd, &saved, sizeof(saved));
    closed = sys_close(fd);
    return length == (int)sizeof(saved) && closed == 0 && saved == marker ? 0 : -1;
}

int screen_save_display(uint32_t width, uint32_t height, uint32_t depth) {
    saved_display_t mode = { width, height, depth };
    if (depth == 32 || !valid_display(&mode)) return -1;
    char text[128];
    snprintf(text, sizeof(text), "[DESKTOP]\nWIDTH=%u\nHEIGHT=%u\nDEPTH=%u\n",
             width, height, depth);
    if (ini_save_section(SETTINGS_PATH, "DESKTOP", text) != 0) return -1;
    return remove_if_present(DISPLAY_TRIAL);
}

static int parse_display_value(const char *text, uint32_t *value) {
    uint32_t n = 0;
    if (!*text) return -1;
    while (*text) {
        if (*text < '0' || *text > '9' || n > 10000) return -1;
        n = n * 10 + (uint32_t)(*text++ - '0');
    }
    *value = n;
    return 0;
}

static int read_saved_display(saved_display_t *mode) {
    char *buffer = ini_read_file(SETTINGS_PATH);
    if (!buffer) return -1;
    ini_parser_t ini;
    ini_init(&ini, buffer);
    char width[32], height[32], depth[32];
    int has_width = ini_get_into(&ini, "DESKTOP", "WIDTH", width, sizeof(width)) == 0;
    int has_height = ini_get_into(&ini, "DESKTOP", "HEIGHT", height, sizeof(height)) == 0;
    int has_depth = ini_get_into(&ini, "DESKTOP", "DEPTH", depth, sizeof(depth)) == 0;
    free(buffer);
    if (!has_width && !has_height && !has_depth) return 0;
    if (!has_width || !has_height || !has_depth) return -1;
    if (parse_display_value(width, &mode->width) != 0 ||
        parse_display_value(height, &mode->height) != 0 ||
        parse_display_value(depth, &mode->depth) != 0) return -1;
    return valid_display(mode) ? 1 : -1;
}

void screen_restore_display(void) {
    saved_display_t mode;
    int trial = file_exists(DISPLAY_TRIAL);
    if (trial != 0) {
        if (trial < 0)
            sys_win_msgbox("Could not check the display recovery marker.", "OK", "Display");
        else
            sys_win_msgbox("Saved display mode disabled\\nafter an interrupted trial.",
                           "OK", "Display");
        return;
    }
    int saved = read_saved_display(&mode);
    if (saved < 0) {
        sys_win_msgbox("The saved display mode is invalid.", "OK", "Display");
        return;
    }
    if (saved && mode.depth == 32) {
        sys_win_msgbox("32-bit mode is unavailable.\\nChoose a mode in Screen.",
                       "OK", "Display");
        return;
    }
    if (!saved) return;
    sys_vbe_state_t current;
    if (sys_gfx_vbe_state(&current) != 0) {
        sys_win_msgbox("Could not query the display mode.", "OK", "Display");
        return;
    }
    if (current.width == mode.width && current.height == mode.height &&
        current.depth == mode.depth) return;

    if (write_display_trial() != 0) {
        sys_win_msgbox("Display recovery failed.\\nSaved mode not applied.",
                       "OK", "Display");
        return;
    }

    int count = sys_gfx_vbe_discover();
    int index = -1;
    for (int i = 0; i < count; ++i) {
        sys_vbe_mode_t candidate;
        if (sys_gfx_vbe_get_mode((unsigned)i, &candidate) != 0) break;
        if (candidate.width == mode.width && candidate.height == mode.height &&
            candidate.depth == mode.depth) {
            index = i;
            break;
        }
    }
    int kept = 0;
    int transition_error = 0;
    if (index >= 0 && sys_gfx_vbe_try((unsigned)index) == 0) {
        if (desktop_refresh_display() >= 0) {
            kept = sys_gfx_vbe_confirm() == 0;
            if (!kept) {
                sys_vbe_state_t state;
                if (sys_gfx_vbe_state(&state) != 0 ||
                    (state.pending && sys_gfx_vbe_revert() != 0))
                    transition_error = 1;
            }
        } else {
            if (sys_gfx_vbe_revert() != 0) transition_error = 1;
        }
    }
    if (desktop_refresh_display() < 0) {
        sys_win_msgbox("Could not refresh the display.", "OK", "Display");
    } else {
        desktop_present_display();
    }
    if (kept) {
        if (remove_if_present(DISPLAY_TRIAL) != 0)
            sys_win_msgbox("Could not clear display recovery.", "OK", "Display");
    } else {
        sys_win_msgbox(transition_error ?
                       "The saved display mode could not be reverted." :
                       "The saved display mode could not be applied.",
                       "OK", "Display");
    }
}
