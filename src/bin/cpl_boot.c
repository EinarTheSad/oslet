#include "progmod.h"
#include "progman.h"
#include "../syscall.h"
#include "../lib/string.h"
#include "../lib/stdlib.h"
#include "../lib/ini.h"
#include "../lib/stdio.h"

#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"

/* Control IDs */
#define CTRL_FRAME_SHELL     1
#define CTRL_LBL_OTHER       2
#define CTRL_TXT_OTHER       3
#define CTRL_FRAME_BOOTSCR   5
#define CTRL_RADIO_BOOTSCR_EN  6
#define CTRL_RADIO_BOOTSCR_DIS 7
#define CTRL_RADIO_SHELL_GUI   8
#define CTRL_RADIO_SHELL_TEXT  9
#define CTRL_BTN_OK         10
#define CTRL_BTN_CANCEL     11

#define WIN_WIDTH  187
#define WIN_HEIGHT 219
#define WIN_X      128
#define WIN_Y      104

typedef struct {
    void *form;
    int shell_type;      /* 0=graphical, 1=text, 2=other */
    char shell_path[256];
    int boot_screen;     /* 0=disabled, 1=enabled */
    /* Originals for cancel */
    int orig_shell_type;
    char orig_shell_path[256];
    int orig_boot_screen;
} cpl_boot_state_t;

static int cpl_boot_init(prog_instance_t *inst);
static int cpl_boot_event(prog_instance_t *inst, int win_idx, int event);
static void cpl_boot_cleanup(prog_instance_t *inst);

const progmod_t cpl_boot_module = {
    .name = "Boot",
    .icon_path = "C:/ICONS/SHUTDOWN.ICO",
    .init = cpl_boot_init,
    .update = 0,
    .handle_event = cpl_boot_event,
    .cleanup = cpl_boot_cleanup,
    .flags = PROG_FLAG_SINGLETON
};

static void load_settings(cpl_boot_state_t *state) {
    /* Defaults */
    state->shell_type = 0;  /* graphical */
    state->shell_path[0] = '\0';
    state->boot_screen = 1;  /* enabled */

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) goto store_orig;

    char buffer[2048];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);

    if (bytes <= 0) goto store_orig;
    buffer[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buffer);

    const char *val = ini_get(&ini, "BOOT", "SHELL");
    if (val) {
        if (strcmp(val, "C:/SHELL.ELF") == 0) {
            state->shell_type = 1;
        } else if (strcmp(val, "C:/DESKTOP.ELF") == 0) {
            state->shell_type = 0;
        } else {
            state->shell_type = 2;
            strncpy(state->shell_path, val, sizeof(state->shell_path) - 1);
            state->shell_path[sizeof(state->shell_path) - 1] = '\0';
        }
    }

    val = ini_get(&ini, "BOOT", "BOOTSCR");
    if (val) {
        int bs = atoi(val);
        state->boot_screen = (bs == 1) ? 1 : 0;
    }

store_orig:
    state->orig_shell_type = state->shell_type;
    strncpy(state->orig_shell_path, state->shell_path, sizeof(state->orig_shell_path));
    state->orig_boot_screen = state->boot_screen;
}

static void save_settings(cpl_boot_state_t *state) {
    char read_buf[2048];
    char tmp1[4096];
    char boot_text[512];

    /* Read existing INI to preserve other sections */
    int fd = sys_open(SETTINGS_PATH, "r");
    int bytes = 0;
    if (fd >= 0) {
        bytes = sys_read(fd, read_buf, sizeof(read_buf) - 1);
        sys_close(fd);
    }
    if (bytes > 0) read_buf[bytes] = '\0';
    else read_buf[0] = '\0';

    /* Build boot section text */
    const char *shell_val;
    if (state->shell_type == 0) {
        shell_val = "C:/DESKTOP.ELF";
    } else if (state->shell_type == 1) {
        shell_val = "C:/SHELL.ELF";
    } else {
        shell_val = state->shell_path;
    }

    snprintf(boot_text, sizeof(boot_text),
        "[BOOT]\r\n"
        "SHELL=%s\r\n"
        "BOOTSCR=%d\r\n",
        shell_val,
        state->boot_screen
    );

    /* Replace or insert only the BOOT section */
    if (ini_replace_section(read_buf, "BOOT", boot_text, tmp1, sizeof(tmp1)) < 0) return;

    fd = sys_open(SETTINGS_PATH, "w");
    if (fd >= 0) {
        sys_write_file(fd, tmp1, strlen(tmp1));
        sys_close(fd);
    }
}

static int cpl_boot_init(prog_instance_t *inst) {
    cpl_boot_state_t *state = sys_malloc(sizeof(cpl_boot_state_t));
    if (!state) return -1;

    inst->user_data = state;
    load_settings(state);

    state->form = sys_win_create_form("Boot Options", WIN_X, WIN_Y, WIN_WIDTH, WIN_HEIGHT);
    if (!state->form) {
        sys_free(state);
        return -1;
    }
    sys_win_set_icon(state->form, "C:/ICONS/SHUTDOWN.ICO");

    static gui_control_t controls[] = {
        { .type = CTRL_FRAME,      .x = 8,   .y = 5,   .w = 171, .h = 98, .fg = 0,  .bg = -1, .id = CTRL_FRAME_SHELL,      .text = "Shell" },
        { .type = CTRL_RADIOBUTTON,.x = 16,  .y = 29,  .w = 12,  .h = 12,  .fg = 0,  .bg = -1, .id = CTRL_RADIO_SHELL_GUI,  .text = "Graphical", .group_id = 1 },
        { .type = CTRL_RADIOBUTTON,.x = 95,  .y = 29,  .w = 12,  .h = 12,  .fg = 0,  .bg = -1, .id = CTRL_RADIO_SHELL_TEXT, .text = "Textmode", .group_id = 1 },
        { .type = CTRL_LABEL,      .x = 15,  .y = 50,                      .fg = 0,  .bg = -1, .id = CTRL_LBL_OTHER,        .text = "Other (e.g. C:/SHELL.ELF):" },
        { .type = CTRL_TEXTBOX,    .x = 17,  .y = 70,  .w = 153, .h = 20,  .fg = 0,  .bg = 7,  .id = CTRL_TXT_OTHER,        .text = "", .max_length = 255 },

        { .type = CTRL_FRAME,      .x = 8,   .y = 108, .w = 171, .h = 47,  .fg = 0,  .bg = -1, .id = CTRL_FRAME_BOOTSCR,    .text = "Boot screen" },
        { .type = CTRL_RADIOBUTTON,.x = 16,  .y = 130, .w = 12,  .h = 12,  .fg = 0,  .bg = -1, .id = CTRL_RADIO_BOOTSCR_EN, .text = "Enabled", .group_id = 2 },
        { .type = CTRL_RADIOBUTTON,.x = 106, .y = 130, .w = 12,  .h = 12,  .fg = 0,  .bg = -1, .id = CTRL_RADIO_BOOTSCR_DIS,.text = "Disabled", .group_id = 2 },

        { .type = CTRL_BUTTON,     .x = 26,  .y = 165, .w = 65,  .h = 23,  .fg = 0,  .bg = -1, .id = CTRL_BTN_OK,           .text = "OK" },
        { .type = CTRL_BUTTON,     .x = 98,  .y = 165, .w = 65,  .h = 23,  .fg = 0,  .bg = -1, .id = CTRL_BTN_CANCEL,       .text = "Cancel" },
    };

    for (int i = 0; i < (int)(sizeof(controls) / sizeof(controls[0])); i++) {
        sys_win_add_control(state->form, &controls[i]);
    }

    /* Set radio button states based on loaded settings */
    gui_control_t *g;
    
    /* Shell type radio buttons */
    g = sys_win_get_control(state->form, CTRL_RADIO_SHELL_GUI);
    if (g) g->checked = (state->shell_type == 0) ? 1 : 0;
    
    g = sys_win_get_control(state->form, CTRL_RADIO_SHELL_TEXT);
    if (g) g->checked = (state->shell_type == 1) ? 1 : 0;

    /* Load custom shell path into textbox */
    ctrl_set_text(state->form, CTRL_TXT_OTHER, state->shell_path);

    /* Boot screen radio buttons */
    g = sys_win_get_control(state->form, CTRL_RADIO_BOOTSCR_EN);
    if (g) g->checked = (state->boot_screen == 1) ? 1 : 0;
    
    g = sys_win_get_control(state->form, CTRL_RADIO_BOOTSCR_DIS);
    if (g) g->checked = (state->boot_screen == 0) ? 1 : 0;

    sys_win_draw(state->form);
    prog_register_window(inst, state->form);
    return 0;
}

static void read_current_values(cpl_boot_state_t *state) {
    gui_control_t *g;

    /* Determine shell type from radio buttons */
    g = sys_win_get_control(state->form, CTRL_RADIO_SHELL_GUI);
    if (g && g->checked) {
        state->shell_type = 0;
    } else {
        g = sys_win_get_control(state->form, CTRL_RADIO_SHELL_TEXT);
        if (g && g->checked) {
            state->shell_type = 1;
        } else {
            /* Neither checked - assume "other" */
            state->shell_type = 2;
        }
    }

    /* Read custom shell path from textbox */
    g = sys_win_get_control(state->form, CTRL_TXT_OTHER);
    if (g) {
        strncpy(state->shell_path, g->text, sizeof(state->shell_path) - 1);
        state->shell_path[sizeof(state->shell_path) - 1] = '\0';
    }

    /* Determine boot screen from radio buttons */
    g = sys_win_get_control(state->form, CTRL_RADIO_BOOTSCR_EN);
    if (g && g->checked) {
        state->boot_screen = 1;
    } else {
        state->boot_screen = 0;
    }
}

static int cpl_boot_event(prog_instance_t *inst, int win_idx, int event) {
    (void)win_idx;
    cpl_boot_state_t *state = inst->user_data;
    if (!state) return PROG_EVENT_NONE;

    if (event == -1 || event == -2)
        return PROG_EVENT_REDRAW;

    /* Radio button or textbox changed - just redraw */
    if (event == CTRL_RADIO_SHELL_GUI || event == CTRL_RADIO_SHELL_TEXT ||
        event == CTRL_RADIO_BOOTSCR_EN || event == CTRL_RADIO_BOOTSCR_DIS ||
        event == CTRL_TXT_OTHER) {
        sys_win_draw(state->form);
        return PROG_EVENT_HANDLED;
    }

    /* OK button */
    if (event == CTRL_BTN_OK) {
        read_current_values(state);
        save_settings(state);

        /* Update originals so cancel doesn't revert */
        state->orig_shell_type = state->shell_type;
        strncpy(state->orig_shell_path, state->shell_path, sizeof(state->orig_shell_path));
        state->orig_boot_screen = state->boot_screen;

        return PROG_EVENT_CLOSE;
    }

    /* Cancel button */
    if (event == CTRL_BTN_CANCEL) {
        /* Restore original values (no need to save) */
        state->shell_type = state->orig_shell_type;
        strncpy(state->shell_path, state->orig_shell_path, sizeof(state->shell_path));
        state->boot_screen = state->orig_boot_screen;
        return PROG_EVENT_CLOSE;
    }

    return PROG_EVENT_NONE;
}

static void cpl_boot_cleanup(prog_instance_t *inst) {
    cpl_boot_state_t *state = inst->user_data;
    if (state) {
        if (state->form) {
            prog_unregister_window(inst, state->form);
            sys_win_destroy_form(state->form);
        }
        sys_free(state);
        inst->user_data = 0;
    }
}
