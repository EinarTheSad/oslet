#include "../../syscall.h"

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
#define MENU_EDIT_CUT   201
#define MENU_EDIT_COPY  202
#define MENU_EDIT_PASTE 203
#define MENU_EDIT_SELECT_ALL 204
#define MENU_FORMAT_FONT 301
#define MENU_HELP_ABOUT 500

static gui_control_t Form1_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 5, .y = 5, .w = 10, .h = 10, .fg = 0, .bg = -1, .text = "", .id = ID_TEXT, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0, .textbox = { .is_multiline = 1 } },
};

static gui_control_t Form2_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 46, .y = 5, .w = 198, .h = 20, .fg = 0, .bg = -1, .text = "", .id = ID_PATH_TEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .textbox = { .max_length = 255 } },
    { .type = CTRL_LABEL, .x = 9, .y = 6, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "Path:", .id = ID_LABEL_PATH, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 56, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "OK", .id = ID_OK, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 130, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "Cancel", .id = ID_CANCEL, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

static void update_layout(void *form) {
    gui_form_t *f = form;
    int win_w = f->win.w;
    int win_h = f->win.h;
    
    int txt_w = win_w - 10;
    int txt_h = win_h - 52;
    
    sys_ctrl_set_prop(form, ID_TEXT, PROP_W, txt_w);
    sys_ctrl_set_prop(form, ID_TEXT, PROP_H, txt_h);
}

static int is_printable_file(const char *path) {
    int fd = sys_open(path, "r");
    if (fd < 0) return 0;
    
    char buffer[16];
    int bytes_read = sys_read(fd, buffer, sizeof(buffer));
    sys_close(fd);
    
    if (bytes_read <= 0) return 0;
    
    for (int i = 0; i < bytes_read; i++) {
        uint8_t c = (uint8_t)buffer[i];
        if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            return 0;
        }
        if (c >= 0x80) {
            return 0;
        }
    }
    return 1;
}

static int load_file_to_textbox(void *form, int textbox_id, const char *path) {
    int fd = sys_open(path, "r");
    if (fd < 0) return -1;
    
    char buffer[256];
    char content[8125];
    int content_len = 0;
    int bytes_read;
    
    while ((bytes_read = sys_read(fd, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        int len = bytes_read;
        for (int i = 0; i < len && content_len < 8124; i++) {
            uint8_t c = (uint8_t)buffer[i];
            if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
            if (c >= 0x80) break;
            content[content_len++] = buffer[i];
        }
        if (content_len >= 8124) break;
    }
    
    sys_close(fd);
    
    if (content_len == 0) return -1;
    
    content[content_len] = '\0';
    ctrl_set_text(form, textbox_id, content);
    return 0;
}

static int handle_events(void *form, int event, void *userdata) {
    (void)userdata;

    if (event > 0) {
        switch (event) {
            case MENU_FILE_OPEN:
                /* Open small dialog to enter path */
                void *dlg = sys_win_create_form("Open", 193, 199, 255, 82);
                if (!dlg) break;
                for (int i = 0; i < (int)(sizeof(Form2_controls) / sizeof(Form2_controls[0])); i++) {
                    sys_win_add_control(dlg, &Form2_controls[i]);
                }

                const char *cur = ctrl_get_text(form, ID_TEXT);
                if (cur && cur[0]) {
                    ctrl_set_text(dlg, ID_PATH_TEXT, cur);
                }
                sys_win_draw(dlg);
                sys_win_redraw_all();
                sys_mouse_invalidate();

                int dlg_running = 1;
                while (dlg_running) {
                    int ev2 = sys_win_pump_events(dlg);
                    if (ev2 == -3) {
                        sys_win_destroy_form(dlg);
                        dlg_running = 0;
                        break;
                    }
                    if (ev2 == -1 || ev2 == -2) {
                        sys_win_draw(dlg);
                        sys_win_redraw_all();
                    }
                    if (ev2 > 0) {
                        if (ev2 == ID_OK) {
                            const char *newpath = ctrl_get_text(dlg, ID_PATH_TEXT);
                            if (newpath && newpath[0] && is_printable_file(newpath)) {
                                if (load_file_to_textbox(form, ID_TEXT, newpath) == 0) {
                                    sys_win_draw(form);
                                } else {
                                    sys_win_msgbox("Cannot read file", "OK", "Error");
                                }
                            } else {
                                sys_win_msgbox("File not found", "OK", "Error");
                            }
                            sys_win_destroy_form(dlg);
                            dlg_running = 0;
                        } else if (ev2 == ID_CANCEL) {
                            sys_win_destroy_form(dlg);
                            dlg_running = 0;
                        }
                    }
                    sys_yield();
                }
                break;
            case MENU_FILE_NEW:
            case MENU_FILE_SAVE:
            case MENU_FILE_SAVE_AS:
                return 0;
            case MENU_FILE_EXIT:
                return 1;
            case MENU_EDIT_CUT:
            case MENU_EDIT_COPY:
            case MENU_EDIT_PASTE:
            case MENU_EDIT_SELECT_ALL:
            case MENU_FORMAT_FONT:
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
    int menu_format = sys_win_menubar_add_menu(form, "Format");
    int menu_help = sys_win_menubar_add_menu(form, "Help");

    sys_win_menubar_add_item(form, menu_file, "New", MENU_FILE_NEW);
    sys_win_menubar_add_item(form, menu_file, "Open", MENU_FILE_OPEN);
    sys_win_menubar_add_item(form, menu_file, "Save", MENU_FILE_SAVE);
    sys_win_menubar_add_item(form, menu_file, "Save As", MENU_FILE_SAVE_AS);
    sys_win_menubar_add_item(form, menu_file, "Exit", MENU_FILE_EXIT);

    sys_win_menubar_add_item(form, menu_edit, "Cut", MENU_EDIT_CUT);
    sys_win_menubar_add_item(form, menu_edit, "Copy", MENU_EDIT_COPY);
    sys_win_menubar_add_item(form, menu_edit, "Paste", MENU_EDIT_PASTE);
    sys_win_menubar_add_item(form, menu_edit, "Select All", MENU_EDIT_SELECT_ALL);

    sys_win_menubar_add_item(form, menu_format, "Font", MENU_FORMAT_FONT);

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
    if (sys_getargs(args, sizeof(args)) && args[0] && is_printable_file(args)) {
        load_file_to_textbox(form, ID_TEXT, args);
        sys_win_draw(form);
    }

    sys_win_run_event_loop(form, handle_events, NULL);
}