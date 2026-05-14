#include "../../syscall.h"
#include "../../lib/string.h"
#include "../../lib/pathdlg.h"

#define ID_TEXT         1

#define MENU_FILE_NEW      101
#define MENU_FILE_OPEN    102
#define MENU_FILE_SAVE    103
#define MENU_FILE_SAVE_AS 104
#define MENU_FILE_EXIT    100
#define MENU_EDIT_SELECT_ALL 204
#define MENU_HELP_ABOUT 500

#define MAX_FILE_CONTENT 8192

#define NOTEPAD_X      100
#define NOTEPAD_Y       50
#define NOTEPAD_W      360
#define NOTEPAD_H      255
#define TEXTBOX_MARGIN   5
#define TEXTBOX_BOTTOM  52

typedef struct {
    void *form;
    char current_file_path[256];
} notepad_state_t;

static gui_control_t Form1_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 5, .y = 5, .w = 10, .h = 10, .fg = 0, .bg = -1, .text = "", .id = ID_TEXT, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0, .textbox = { .is_multiline = 1 } },
};

static inline int is_valid_text_char(uint8_t c) {
    return (c >= 0x20 || c == '\t' || c == '\n' || c == '\r') && c < 0x80;
}

static void set_current_file_path(notepad_state_t *state, const char *path) {
    if (!state) return;

    if (!path) {
        state->current_file_path[0] = '\0';
        return;
    }
    strncpy(state->current_file_path, path, sizeof(state->current_file_path) - 1);
    state->current_file_path[sizeof(state->current_file_path) - 1] = '\0';
}

static int load_text_file(void *form, int textbox_id, const char *path) {
    int fd = sys_open(path, "r");
    if (fd < 0) return -1;

    char buffer[256];
    char content[MAX_FILE_CONTENT + 1];
    int content_len = 0;
    int bytes_read;

    while ((bytes_read = sys_read(fd, buffer, sizeof(buffer))) > 0) {
        for (int i = 0; i < bytes_read; i++) {
            uint8_t c = (uint8_t)buffer[i];
            if (!is_valid_text_char(c)) {
                sys_close(fd);
                return -1;
            }
            if (content_len < MAX_FILE_CONTENT) {
                content[content_len++] = c;
            }
        }
    }

    sys_close(fd);
    content[content_len] = '\0';
    ctrl_set_text(form, textbox_id, content);
    return 0;
}

static int save_text_to_file(const char *path, const char *text) {
    int fd = sys_open(path, "w");
    if (fd < 0) return -1;

    int len = 0;
    if (text) {
        while (text[len]) len++;
    }

    if (len > 0) {
        if (sys_write_file(fd, text, len) != len) {
            sys_close(fd);
            return -1;
        }
    }

    sys_close(fd);
    return 0;
}

static void perform_select_all(void *form) {
    gui_control_t *ctrl = sys_win_get_control(form, ID_TEXT);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return;

    char *text = ctrl->textbox.multiline_text;
    if (!text) text = ctrl->text;
    if (!text) return;

    int len = 0;
    while (text[len]) len++;
    if (len == 0) return;

    ctrl->textbox.sel_start = 0;
    ctrl->textbox.sel_end = len;
    ctrl->textbox.cursor_pos = len;
    sys_win_draw(form);
}

static void update_layout(void *form) {
    gui_form_t *f = form;
    int win_w = f->win.w;
    int win_h = f->win.h;

    int txt_w = win_w - (TEXTBOX_MARGIN * 2);
    int txt_h = win_h - TEXTBOX_BOTTOM;
    if (txt_w < 32) txt_w = 32;
    if (txt_h < 16) txt_h = 16;

    sys_ctrl_set_prop(form, ID_TEXT, PROP_W, txt_w);
    sys_ctrl_set_prop(form, ID_TEXT, PROP_H, txt_h);
}

static int prompt_for_path(notepad_state_t *state, const char *title, char *out_path, int out_len) {
    const char *initial_path = "";
    if (state && state->current_file_path[0]) {
        initial_path = state->current_file_path;
    }

    return gui_show_path_dialog(title, initial_path, out_path, out_len);
}

static int notepad_new(notepad_state_t *state) {
    ctrl_set_text(state->form, ID_TEXT, "");

    gui_control_t *ctrl = sys_win_get_control(state->form, ID_TEXT);
    if (ctrl && ctrl->type == CTRL_TEXTBOX) {
        ctrl->textbox.cursor_pos = 0;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
    }

    set_current_file_path(state, NULL);
    sys_win_draw(state->form);
    return 0;
}

static int notepad_open(notepad_state_t *state) {
    char path[sizeof(state->current_file_path)] = "";

    if (!prompt_for_path(state, "Open", path, sizeof(path))) return 0;

    if (load_text_file(state->form, ID_TEXT, path) == 0) {
        set_current_file_path(state, path);
        sys_win_draw(state->form);
    } else {
        sys_win_msgbox("Cannot read file", "OK", "Error");
    }

    return 0;
}

static int notepad_save_to_path(notepad_state_t *state, const char *path, const char *title) {
    if (save_text_to_file(path, ctrl_get_text(state->form, ID_TEXT)) == 0) {
        set_current_file_path(state, path);
        sys_win_msgbox("File saved.", "OK", title);
        return 0;
    }

    sys_win_msgbox("Failed to save file.", "OK", title);
    return 0;
}

static int notepad_save(notepad_state_t *state, int save_as) {
    char path[sizeof(state->current_file_path)] = "";
    const char *title = save_as ? "Save As" : "Save";

    if (!save_as && state->current_file_path[0]) {
        return notepad_save_to_path(state, state->current_file_path, title);
    }

    if (!prompt_for_path(state, "Save As", path, sizeof(path))) return 0;
    return notepad_save_to_path(state, path, "Save As");
}

static int handle_events(void *form, int event, void *userdata) {
    notepad_state_t *state = userdata;
    (void)form;

    if (event > 0) {
        switch (event) {
            case MENU_FILE_OPEN:
                return notepad_open(state);
            case MENU_FILE_NEW:
                return notepad_new(state);
            case MENU_FILE_SAVE:
                return notepad_save(state, 0);
            case MENU_FILE_SAVE_AS:
                return notepad_save(state, 1);
            case MENU_FILE_EXIT:
                return 1;
            case MENU_EDIT_SELECT_ALL:
                perform_select_all(state->form);
                return 0;
            case MENU_HELP_ABOUT:
                sys_win_msgbox("osLET Notepad 0.2, EinarTheSad 2026", "OK", "About");
                return 0;
        }
    }

    if (event == -4) {
        update_layout(state->form);
        sys_win_draw(state->form);
        sys_win_force_full_redraw();
    }

    return 0;
}

static void setup_menus(void *form) {
    sys_win_menubar_enable(form);

    int menu_file = sys_win_menubar_add_menu(form, "File");
    int menu_edit = sys_win_menubar_add_menu(form, "Edit");
    int menu_help = sys_win_menubar_add_menu(form, "Help");

    sys_win_menubar_add_item(form, menu_file, "New", MENU_FILE_NEW);
    sys_win_menubar_add_item(form, menu_file, "Open", MENU_FILE_OPEN);
    sys_win_menubar_add_item(form, menu_file, "Save", MENU_FILE_SAVE);
    sys_win_menubar_add_item(form, menu_file, "Save As", MENU_FILE_SAVE_AS);
    sys_win_menubar_add_item(form, menu_file, "Exit", MENU_FILE_EXIT);

    sys_win_menubar_add_item(form, menu_edit, "Select all", MENU_EDIT_SELECT_ALL);

    sys_win_menubar_add_item(form, menu_help, "About", MENU_HELP_ABOUT);
}

__attribute__((section(".entry"), used))
void _start(void) {
    notepad_state_t state;
    state.form = sys_win_create_form("Notepad", NOTEPAD_X, NOTEPAD_Y, NOTEPAD_W, NOTEPAD_H);
    state.current_file_path[0] = '\0';

    if (!state.form) {
        sys_exit();
        return;
    }

    for (int i = 0; i < (int)(sizeof(Form1_controls) / sizeof(Form1_controls[0])); i++) {
        sys_win_add_control(state.form, &Form1_controls[i]);
    }

    sys_win_set_icon(state.form, "C:/ICONS/TEXT.ICO");

    setup_menus(state.form);

    update_layout(state.form);
    sys_win_draw(state.form);
    sys_win_force_full_redraw();

    /* Load text from launch arguments if provided */
    char args[256];
    if (sys_getargs(args, sizeof(args)) && args[0]) {
        if (load_text_file(state.form, ID_TEXT, args) == 0) {
            set_current_file_path(&state, args);
            sys_win_draw(state.form);
        }
    }

    sys_win_run_event_loop(state.form, handle_events, &state);
}
