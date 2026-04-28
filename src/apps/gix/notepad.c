#include "../../syscall.h"
#include "../../lib/string.h"

#define ID_TEXT         1

#define ID_PATH_TEXT  10
#define ID_OK         2
#define ID_CANCEL     3
#define ID_LABEL_PATH 11
#define MENU_FILE_NEW      101
#define MENU_FILE_OPEN    102
#define MENU_FILE_SAVE    103
#define MENU_FILE_SAVE_AS 104
#define MENU_FILE_EXIT    100
#define MENU_EDIT_SELECT_ALL 204
#define MENU_HELP_ABOUT 500

#define MAX_FILE_CONTENT 8192

static char current_file_path[256] = "";

static gui_control_t Form1_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 5, .y = 5, .w = 10, .h = 10, .fg = 0, .bg = -1, .text = "", .id = ID_TEXT, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0, .textbox = { .is_multiline = 1 } },
};

static gui_control_t Form2_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 46, .y = 5, .w = 198, .h = 20, .fg = 0, .bg = -1, .text = "", .id = ID_PATH_TEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .textbox = { .max_length = 255 } },
    { .type = CTRL_LABEL, .x = 9, .y = 6, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "Path:", .id = ID_LABEL_PATH, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 56, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "OK", .id = ID_OK, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 130, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "Cancel", .id = ID_CANCEL, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

static inline int is_valid_text_char(uint8_t c) {
    return (c >= 0x20 || c == '\t' || c == '\n' || c == '\r') && c < 0x80;
}

static void set_current_file_path(const char *path) {
    if (!path) {
        current_file_path[0] = '\0';
        return;
    }
    strncpy(current_file_path, path, sizeof(current_file_path) - 1);
    current_file_path[sizeof(current_file_path) - 1] = '\0';
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

static int show_path_dialog(const char *title, char *out_path, int out_len) {
    if (!out_path || out_len <= 0) return 0;

    void *dlg = sys_win_create_form(title, 193, 199, 255, 82);
    if (!dlg) return 0;

    for (int i = 0; i < (int)(sizeof(Form2_controls) / sizeof(Form2_controls[0])); i++) {
        sys_win_add_control(dlg, &Form2_controls[i]);
    }

    if (current_file_path[0]) {
        ctrl_set_text(dlg, ID_PATH_TEXT, current_file_path);
    }

    sys_win_draw(dlg);
    sys_win_redraw_all();
    sys_mouse_invalidate();

    int confirmed = 0;
    int dlg_running = 1;

    while (dlg_running) {
        int ev = sys_win_pump_events(dlg);
        if (ev == -3 || ev == ID_CANCEL) {
            dlg_running = 0;
        } else if (ev == ID_OK) {
            const char *value = ctrl_get_text(dlg, ID_PATH_TEXT);
            if (value && value[0]) {
                strncpy(out_path, value, out_len - 1);
                out_path[out_len - 1] = '\0';
                confirmed = 1;
            }
            dlg_running = 0;
        } else if (ev == -1 || ev == -2) {
            sys_win_draw(dlg);
            sys_win_redraw_all();
        }
        sys_yield();
    }

    sys_win_destroy_form(dlg);
    sys_win_redraw_all();
    return confirmed;
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

    int txt_w = win_w - 10;
    int txt_h = win_h - 52;
    if (txt_w < 32) txt_w = 32;
    if (txt_h < 16) txt_h = 16;

    sys_ctrl_set_prop(form, ID_TEXT, PROP_W, txt_w);
    sys_ctrl_set_prop(form, ID_TEXT, PROP_H, txt_h);
}

static int handle_events(void *form, int event, void *userdata) {
    (void)userdata;

    if (event > 0) {
        switch (event) {
            case MENU_FILE_OPEN: {
                char path[sizeof(current_file_path)] = "";
                if (!show_path_dialog("Open", path, sizeof(path))) break;
                if (load_text_file(form, ID_TEXT, path) == 0) {
                    set_current_file_path(path);
                    sys_win_draw(form);
                } else {
                    sys_win_msgbox("Cannot read file", "OK", "Error");
                }
                return 0;
            }
            case MENU_FILE_NEW: {
                ctrl_set_text(form, ID_TEXT, "");
                gui_control_t *ctrl = sys_win_get_control(form, ID_TEXT);
                if (ctrl && ctrl->type == CTRL_TEXTBOX) {
                    ctrl->textbox.cursor_pos = 0;
                    ctrl->textbox.sel_start = -1;
                    ctrl->textbox.sel_end = -1;
                }
                set_current_file_path(NULL);
                sys_win_draw(form);
                return 0;
            }
            case MENU_FILE_SAVE: {
                if (!current_file_path[0]) {
                    char path[sizeof(current_file_path)] = "";
                    if (!show_path_dialog("Save As", path, sizeof(path))) break;
                    if (save_text_to_file(path, ctrl_get_text(form, ID_TEXT)) == 0) {
                        set_current_file_path(path);
                        sys_win_msgbox("File saved.", "OK", "Save As");
                    } else {
                        sys_win_msgbox("Failed to save file.", "OK", "Save As");
                    }
                    return 0;
                }
                if (save_text_to_file(current_file_path, ctrl_get_text(form, ID_TEXT)) == 0) {
                    sys_win_msgbox("File saved.", "OK", "Save");
                } else {
                    sys_win_msgbox("Failed to save file.", "OK", "Save");
                }
                return 0;
            }
            case MENU_FILE_SAVE_AS: {
                char path[sizeof(current_file_path)] = "";
                if (!show_path_dialog("Save As", path, sizeof(path))) break;
                if (save_text_to_file(path, ctrl_get_text(form, ID_TEXT)) == 0) {
                    set_current_file_path(path);
                    sys_win_msgbox("File saved.", "OK", "Save As");
                } else {
                    sys_win_msgbox("Failed to save file.", "OK", "Save As");
                }
                return 0;
            }
            case MENU_FILE_EXIT:
                return 1;
            case MENU_EDIT_SELECT_ALL:
                perform_select_all(form);
                return 0;
            case MENU_HELP_ABOUT:
                sys_win_msgbox("osLET Notepad 0.2, EinarTheSad 2026", "OK", "About");
                return 0;
        }
    }

    if (event == -4) {
        update_layout(form);
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
    void *form = sys_win_create_form("Notepad", 100, 50, 360, 255);
    if (!form) {
        sys_exit();
        return;
    }

    for (int i = 0; i < (int)(sizeof(Form1_controls) / sizeof(Form1_controls[0])); i++) {
        sys_win_add_control(form, &Form1_controls[i]);
    }

    sys_win_set_icon(form, "C:/ICONS/TEXT.ICO");

    setup_menus(form);

    update_layout(form);

    /* Load text from launch arguments if provided */
    char args[256];
    if (sys_getargs(args, sizeof(args)) && args[0]) {
        if (load_text_file(form, ID_TEXT, args) == 0) {
            set_current_file_path(args);
            sys_win_draw(form);
        }
    }

    sys_win_run_event_loop(form, handle_events, NULL);
}
