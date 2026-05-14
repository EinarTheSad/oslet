#include "../syscall.h"
#include "string.h"

#define PATH_DLG_ID_TEXT   1
#define PATH_DLG_ID_LABEL  2
#define PATH_DLG_ID_OK     3
#define PATH_DLG_ID_CANCEL 4

#define PATH_DLG_X 193
#define PATH_DLG_Y 199
#define PATH_DLG_W 255
#define PATH_DLG_H 82

static gui_control_t path_dialog_controls[] = {
    { .type = CTRL_TEXTBOX, .x = 46, .y = 5, .w = 198, .h = 20, .fg = 0, .bg = -1, .text = "", .id = PATH_DLG_ID_TEXT, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0, .textbox = { .max_length = 255 } },
    { .type = CTRL_LABEL, .x = 9, .y = 6, .w = 0, .h = 0, .fg = 0, .bg = -1, .text = "Path:", .id = PATH_DLG_ID_LABEL, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 56, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "OK", .id = PATH_DLG_ID_OK, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 },
    { .type = CTRL_BUTTON, .x = 130, .y = 33, .w = 70, .h = 23, .fg = 0, .bg = -1, .text = "Cancel", .id = PATH_DLG_ID_CANCEL, .font_type = 0, .font_size = 12, .border = 0, .border_color = 0 }
};

int gui_show_path_dialog(const char *title, const char *initial_path,
                         char *out_path, int out_len) {
    if (!title || !out_path || out_len <= 0) return 0;

    void *dlg = sys_win_create_form(title, PATH_DLG_X, PATH_DLG_Y, PATH_DLG_W, PATH_DLG_H);
    if (!dlg) return 0;

    for (int i = 0; i < (int)(sizeof(path_dialog_controls) / sizeof(path_dialog_controls[0])); i++) {
        sys_win_add_control(dlg, &path_dialog_controls[i]);
    }

    if (initial_path && initial_path[0]) {
        ctrl_set_text(dlg, PATH_DLG_ID_TEXT, initial_path);
    }

    sys_win_draw(dlg);
    sys_win_redraw_all();
    sys_mouse_invalidate();

    out_path[0] = '\0';

    while (1) {
        int ev = sys_win_pump_events(dlg);

        if (ev == -3 || ev == PATH_DLG_ID_CANCEL) {
            break;
        }

        if (ev == -1 || ev == -2 || ev == -4) {
            sys_win_draw(dlg);
            sys_win_redraw_all();
        } else if (ev == PATH_DLG_ID_OK) {
            const char *value = ctrl_get_text(dlg, PATH_DLG_ID_TEXT);
            if (value && value[0]) {
                strncpy(out_path, value, out_len - 1);
                out_path[out_len - 1] = '\0';
                sys_win_destroy_form(dlg);
                sys_win_redraw_all();
                return 1;
            }
            break;
        }

        sys_yield();
    }

    sys_win_destroy_form(dlg);
    sys_win_redraw_all();
    return 0;
}
