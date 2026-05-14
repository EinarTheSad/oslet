#include "../../syscall.h"
#include "../../lib/string.h"
#include "../../lib/pathdlg.h"

#define ID_TEXT         1

#define MENU_FILE_NEW      101
#define MENU_FILE_OPEN    102
#define MENU_FILE_SAVE    103
#define MENU_FILE_SAVE_AS 104
#define MENU_FILE_EXIT    100
#define MENU_EDIT_CUT        201
#define MENU_EDIT_COPY       202
#define MENU_EDIT_PASTE      203
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
    char clipboard[MAX_FILE_CONTENT + 1];
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

static char *get_textbox_buffer(gui_control_t *ctrl) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return NULL;
    if (ctrl->textbox.is_multiline && ctrl->textbox.multiline_text) {
        return ctrl->textbox.multiline_text;
    }
    return ctrl->text;
}

static int text_len(const char *text) {
    int len = 0;
    if (text) {
        while (text[len]) len++;
    }
    return len;
}

static int get_selection(gui_control_t *ctrl, int len, int *sel_min, int *sel_max) {
    if (!ctrl || ctrl->textbox.sel_start < 0 || ctrl->textbox.sel_start == ctrl->textbox.sel_end) {
        return 0;
    }

    int start = ctrl->textbox.sel_start;
    int end = ctrl->textbox.sel_end;
    if (start > end) {
        int tmp = start;
        start = end;
        end = tmp;
    }

    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > len) start = len;
    if (end > len) end = len;
    if (start == end) return 0;

    *sel_min = start;
    *sel_max = end;
    return 1;
}

static int copy_selection(notepad_state_t *state, gui_control_t *ctrl) {
    char *text = get_textbox_buffer(ctrl);
    int len = text_len(text);
    int sel_min, sel_max;

    if (!text || !get_selection(ctrl, len, &sel_min, &sel_max)) return 0;

    int copy_len = sel_max - sel_min;
    if (copy_len > MAX_FILE_CONTENT) copy_len = MAX_FILE_CONTENT;

    for (int i = 0; i < copy_len; i++) {
        state->clipboard[i] = text[sel_min + i];
    }
    state->clipboard[copy_len] = '\0';
    return copy_len > 0;
}

static int delete_selection(gui_control_t *ctrl) {
    char *text = get_textbox_buffer(ctrl);
    int len = text_len(text);
    int sel_min, sel_max;

    if (!text || !get_selection(ctrl, len, &sel_min, &sel_max)) return 0;

    int del_len = sel_max - sel_min;
    for (int i = sel_min; i <= len - del_len; i++) {
        text[i] = text[i + del_len];
    }

    ctrl->textbox.cursor_pos = sel_min;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

static int paste_clipboard(notepad_state_t *state, gui_control_t *ctrl) {
    char *text = get_textbox_buffer(ctrl);
    if (!text || !state->clipboard[0]) return 0;

    delete_selection(ctrl);

    int len = text_len(text);
    int cursor = ctrl->textbox.cursor_pos;
    int max_len = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;

    if (cursor < 0) cursor = 0;
    if (cursor > len) cursor = len;
    if (max_len < 2 || len >= max_len - 1) return 0;

    int room = (max_len - 1) - len;
    int paste_len = 0;
    while (state->clipboard[paste_len] && paste_len < room) paste_len++;
    if (paste_len <= 0) return 0;

    for (int i = len; i >= cursor; i--) {
        text[i + paste_len] = text[i];
    }
    for (int i = 0; i < paste_len; i++) {
        text[cursor + i] = state->clipboard[i];
    }

    ctrl->textbox.cursor_pos = cursor + paste_len;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

static void perform_text_edit(notepad_state_t *state, int command) {
    gui_control_t *ctrl = sys_win_get_control(state->form, ID_TEXT);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return;

    int changed = 0;
    if (command == TEXTBOX_EDIT_COPY) {
        copy_selection(state, ctrl);
    } else if (command == TEXTBOX_EDIT_CUT) {
        if (copy_selection(state, ctrl)) {
            changed = delete_selection(ctrl);
        }
    } else if (command == TEXTBOX_EDIT_PASTE) {
        changed = paste_clipboard(state, ctrl);
    } else if (command == TEXTBOX_EDIT_SELECT_ALL) {
        char *text = get_textbox_buffer(ctrl);
        int len = text_len(text);
        ctrl->textbox.sel_start = len > 0 ? 0 : -1;
        ctrl->textbox.sel_end = len > 0 ? len : -1;
        ctrl->textbox.cursor_pos = len;
        changed = 1;
    }

    if (changed) {
        sys_win_draw(state->form);
    }
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

static int handle_shortcut(notepad_state_t *state, int key) {
    switch (key) {
        case 14: /* Ctrl+N */
            return notepad_new(state);
        case 19: /* Ctrl+S */
            return notepad_save(state, 0);
        default:
            return 0;
    }
}

static int handle_events(void *form, int event, void *userdata) {
    notepad_state_t *state = userdata;
    (void)form;

    if (event >= SYS_EVENT_CTRL_KEY_BASE && event < SYS_EVENT_CTRL_KEY_BASE + 32) {
        return handle_shortcut(state, event - SYS_EVENT_CTRL_KEY_BASE);
    }

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
            case MENU_EDIT_CUT:
                perform_text_edit(state, TEXTBOX_EDIT_CUT);
                return 0;
            case MENU_EDIT_COPY:
                perform_text_edit(state, TEXTBOX_EDIT_COPY);
                return 0;
            case MENU_EDIT_PASTE:
                perform_text_edit(state, TEXTBOX_EDIT_PASTE);
                return 0;
            case MENU_EDIT_SELECT_ALL:
                perform_text_edit(state, TEXTBOX_EDIT_SELECT_ALL);
                return 0;
            case MENU_HELP_ABOUT:
                sys_win_msgbox("osLET Notepad 0.4, EinarTheSad 2026", "OK", "About");
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

    sys_win_menubar_add_item(form, menu_file, "New  Ctrl+N", MENU_FILE_NEW);
    sys_win_menubar_add_item(form, menu_file, "Open", MENU_FILE_OPEN);
    sys_win_menubar_add_item(form, menu_file, "Save  Ctrl+S", MENU_FILE_SAVE);
    sys_win_menubar_add_item(form, menu_file, "Save As...", MENU_FILE_SAVE_AS);
    sys_win_menubar_add_item(form, menu_file, "Exit", MENU_FILE_EXIT);

    sys_win_menubar_add_item(form, menu_edit, "Cut  Ctrl+X", MENU_EDIT_CUT);
    sys_win_menubar_add_item(form, menu_edit, "Copy  Ctrl+C", MENU_EDIT_COPY);
    sys_win_menubar_add_item(form, menu_edit, "Paste  Ctrl+V", MENU_EDIT_PASTE);
    sys_win_menubar_add_item(form, menu_edit, "Select All  Ctrl+A", MENU_EDIT_SELECT_ALL);

    sys_win_menubar_add_item(form, menu_help, "About", MENU_HELP_ABOUT);
}

__attribute__((section(".entry"), used))
void _start(void) {
    static notepad_state_t state;
    state.form = sys_win_create_form("Notepad", NOTEPAD_X, NOTEPAD_Y, NOTEPAD_W, NOTEPAD_H);
    state.current_file_path[0] = '\0';
    state.clipboard[0] = '\0';

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
