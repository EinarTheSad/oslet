#include "gui.h"

static char textbox_clipboard[TEXTBOX_MULTILINE_SIZE];

char *textbox_get_text(gui_control_t *ctrl) {
    if (ctrl->type == CTRL_TEXTBOX && ctrl->textbox.is_multiline && ctrl->textbox.multiline_text) {
        return ctrl->textbox.multiline_text;
    }
    return ctrl->text;
}

static int textbox_text_len(gui_control_t *ctrl) {
    char *text = textbox_get_text(ctrl);
    int len = 0;
    while (text && text[len]) len++;
    return len;
}

static int textbox_selection_bounds(gui_control_t *ctrl, int text_len, int *sel_min, int *sel_max) {
    if (ctrl->textbox.sel_start < 0 || ctrl->textbox.sel_start == ctrl->textbox.sel_end) return 0;

    int start = ctrl->textbox.sel_start;
    int end = ctrl->textbox.sel_end;
    if (start > end) {
        int tmp = start;
        start = end;
        end = tmp;
    }

    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > text_len) start = text_len;
    if (end > text_len) end = text_len;
    if (start == end) return 0;

    *sel_min = start;
    *sel_max = end;
    return 1;
}

static int textbox_copy_selection(gui_control_t *ctrl) {
    int text_len = textbox_text_len(ctrl);
    int sel_min, sel_max;
    if (!textbox_selection_bounds(ctrl, text_len, &sel_min, &sel_max)) return 0;

    char *text = textbox_get_text(ctrl);
    int copy_len = sel_max - sel_min;
    if (copy_len >= TEXTBOX_MULTILINE_SIZE) copy_len = TEXTBOX_MULTILINE_SIZE - 1;

    for (int i = 0; i < copy_len; i++) {
        textbox_clipboard[i] = text[sel_min + i];
    }
    textbox_clipboard[copy_len] = '\0';
    return copy_len > 0;
}

static int textbox_delete_selection(gui_control_t *ctrl) {
    int text_len = textbox_text_len(ctrl);
    int sel_min, sel_max;
    if (!textbox_selection_bounds(ctrl, text_len, &sel_min, &sel_max)) return 0;

    char *text = textbox_get_text(ctrl);
    int del_count = sel_max - sel_min;
    for (int i = sel_min; i < text_len - del_count; i++) {
        text[i] = text[i + del_count];
    }
    text[text_len - del_count] = '\0';

    ctrl->textbox.cursor_pos = sel_min;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

static int textbox_insert_text(gui_control_t *ctrl, const char *insert_text) {
    if (!insert_text || !insert_text[0]) return 0;

    int max_len = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
    if (max_len < 2) return 0;

    if (textbox_delete_selection(ctrl)) {
        /* cursor and text length are recalculated below */
    }

    char *text = textbox_get_text(ctrl);
    int text_len = textbox_text_len(ctrl);
    int cursor = ctrl->textbox.cursor_pos;
    if (cursor < 0) cursor = 0;
    if (cursor > text_len) cursor = text_len;

    int room = (max_len - 1) - text_len;
    if (room <= 0) return 0;

    int insert_len = 0;
    while (insert_text[insert_len] && insert_len < room) insert_len++;
    if (insert_len <= 0) return 0;

    for (int i = text_len; i >= cursor; i--) {
        text[i + insert_len] = text[i];
    }

    for (int i = 0; i < insert_len; i++) {
        text[cursor + i] = insert_text[i];
    }

    ctrl->textbox.cursor_pos = cursor + insert_len;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

static int textbox_select_all(gui_control_t *ctrl) {
    int text_len = textbox_text_len(ctrl);
    if (text_len <= 0) {
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        ctrl->textbox.cursor_pos = 0;
        return 1;
    }

    ctrl->textbox.sel_start = 0;
    ctrl->textbox.sel_end = text_len;
    ctrl->textbox.cursor_pos = text_len;
    return 1;
}

int textbox_run_edit_command(gui_control_t *ctrl, int command) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    switch (command) {
        case TEXTBOX_EDIT_COPY:
            textbox_copy_selection(ctrl);
            return 0;
        case TEXTBOX_EDIT_CUT:
            if (!textbox_copy_selection(ctrl)) return 0;
            return textbox_delete_selection(ctrl);
        case TEXTBOX_EDIT_PASTE:
            return textbox_insert_text(ctrl, textbox_clipboard);
        case TEXTBOX_EDIT_SELECT_ALL:
            return textbox_select_all(ctrl);
        default:
            return 0;
    }
}

int pump_handle_keyboard(gui_form_t *form) {
    if (form->focused_control_id < 0) return 0;

    gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    int key = kbd_getchar_nonblock();
    if (key == 0) return 0;

    char *text = textbox_get_text(ctrl);
    int text_len = 0;
    while (text[text_len]) text_len++;

    int max_len = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
    int needs_redraw = 0;
    int has_selection = (ctrl->textbox.sel_start >= 0 && ctrl->textbox.sel_start != ctrl->textbox.sel_end);

    if (key == 14 || key == 19) {
        form->clicked_id = SYS_EVENT_CTRL_KEY(key);
        return 2;
    }

    if (key == 1 || key == 3 || key == 22 || key == 24) {
        if (key == 1) {
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_SELECT_ALL);
        } else if (key == 3) {
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_COPY);
        } else if (key == 22) {
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_PASTE);
        } else if (key == 24) {
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_CUT);
        }
        return needs_redraw;
    }

    if (key == KEY_LEFT) {
        if (has_selection) {
            int sel_min = ctrl->textbox.sel_start < ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = sel_min;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            ctrl->textbox.cursor_pos--;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_RIGHT) {
        if (has_selection) {
            int sel_max = ctrl->textbox.sel_start > ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = sel_max;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos < text_len) {
            ctrl->textbox.cursor_pos++;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_HOME) {
        ctrl->textbox.cursor_pos = 0;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == KEY_END) {
        ctrl->textbox.cursor_pos = text_len;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == '\b') {
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            text = textbox_get_text(ctrl);
            for (int i = ctrl->textbox.cursor_pos - 1; i < text_len; i++) {
                text[i] = text[i + 1];
            }
            ctrl->textbox.cursor_pos--;
            needs_redraw = 1;
        }
    }
    else if (key == KEY_DELETE) {
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->textbox.cursor_pos < text_len) {
            text = textbox_get_text(ctrl);
            for (int i = ctrl->textbox.cursor_pos; i < text_len; i++) {
                text[i] = text[i + 1];
            }
            needs_redraw = 1;
        }
    }
    else if (key == '\t') {
    }
    else if (key == '\n' || key == '\r') {
        if (ctrl->textbox.is_multiline && text_len < max_len - 1) {
            text = textbox_get_text(ctrl);
            for (int i = text_len; i >= ctrl->textbox.cursor_pos; i--) {
                text[i + 1] = text[i];
            }
            text[ctrl->textbox.cursor_pos] = '\n';
            ctrl->textbox.cursor_pos++;
            needs_redraw = 1;
        } else {
            form->clicked_id = ctrl->id;
            needs_redraw = 1;
        }
    }
    else if (key == KEY_ESC) {
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        form->focused_control_id = -1;
        needs_redraw = 1;
    }
    else if (key >= 0x20 && key < 0x80) {
        if (has_selection) {
            textbox_delete_selection(ctrl);
            text = textbox_get_text(ctrl);
            text_len = 0;
            while (text[text_len]) text_len++;
        }

        if (text_len < max_len - 1) {
            text = textbox_get_text(ctrl);
            for (int i = text_len; i >= ctrl->textbox.cursor_pos; i--) {
                text[i + 1] = text[i];
            }
            text[ctrl->textbox.cursor_pos] = (char)key;
            ctrl->textbox.cursor_pos++;
            needs_redraw = 1;
        }
    }

    return needs_redraw;
}
