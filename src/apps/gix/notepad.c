#include "../../syscall.h"

#define ID_TEXT         1
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
    { .type = CTRL_TEXTBOX, .x = 5, .y = 5, .w = 10, .h = 10, .fg = 0, .bg = -1, .text = "", .id = ID_TEXT, .font_type = 0, .font_size = 12, .border = 1, .border_color = 0 },
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

static int handle_events(void *form, int event, void *userdata) {
    (void)userdata;

    if (event > 0) {
        switch (event) {
            case MENU_FILE_NEW:
            case MENU_FILE_OPEN:
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
                sys_win_msgbox("osLET Notepad 0.1, EinarTheSad 2026", "OK", "About");
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
    if (sys_getargs(args, sizeof(args)) && args[0]) {
        ctrl_set_text(form, ID_TEXT, args);
    }

    sys_win_run_event_loop(form, handle_events, NULL);
}