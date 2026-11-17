#include "gui.h"

static char *textbox_clipboard;
static uint32_t textbox_clipboard_length;
static uint32_t textbox_clipboard_capacity;

void textbox_reset_cursor_blink(gui_control_t *ctrl) {
    if (!ctrl || (ctrl->type & 0x7F) != CTRL_TEXTBOX) return;
    ctrl->textbox.cursor_visible = 1;
    ctrl->textbox.cursor_blink_tick = timer_get_ticks();
}

static void textbox_move_bytes(char *destination, const char *source, uint32_t length) {
    if (destination < source) {
        for (uint32_t i = 0; i < length; i++) destination[i] = source[i];
    } else if (destination > source) {
        while (length > 0) {
            length--;
            destination[length] = source[length];
        }
    }
}

uint32_t textbox_text_len(gui_control_t *ctrl) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;
    if (ctrl->textbox.is_multiline) {
        if (!ctrl->textbox.multiline_text ||
            ctrl->textbox.gap_start > ctrl->textbox.gap_end ||
            ctrl->textbox.gap_end > ctrl->textbox.buffer_capacity) return 0;
        return ctrl->textbox.buffer_capacity -
               (ctrl->textbox.gap_end - ctrl->textbox.gap_start);
    }

    uint32_t length = 0;
    while (length < sizeof(ctrl->text) && ctrl->text[length]) length++;
    return length;
}

char textbox_char_at(gui_control_t *ctrl, uint32_t offset) {
    uint32_t length = textbox_text_len(ctrl);
    if (offset >= length) return '\0';
    if (!ctrl->textbox.is_multiline) return ctrl->text[offset];

    if (offset < ctrl->textbox.gap_start) return ctrl->textbox.multiline_text[offset];
    return ctrl->textbox.multiline_text[
        offset + (ctrl->textbox.gap_end - ctrl->textbox.gap_start)];
}

int textbox_copy_range(gui_control_t *ctrl, uint32_t offset, char *destination, uint32_t length) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX || (!destination && length)) return -1;

    uint32_t total = textbox_text_len(ctrl);
    if (offset > total) return -1;
    if (length > total - offset) length = total - offset;
    if (!length) return 0;

    if (!ctrl->textbox.is_multiline) {
        for (uint32_t i = 0; i < length; i++) destination[i] = ctrl->text[offset + i];
        return (int)length;
    }

    uint32_t copied = 0;
    if (offset < ctrl->textbox.gap_start) {
        uint32_t prefix = ctrl->textbox.gap_start - offset;
        if (prefix > length) prefix = length;
        memcpy_s(destination, ctrl->textbox.multiline_text + offset, prefix);
        copied = prefix;
    }

    if (copied < length) {
        uint32_t logical_offset = offset + copied;
        uint32_t physical_offset = logical_offset +
            (ctrl->textbox.gap_end - ctrl->textbox.gap_start);
        memcpy_s(destination + copied, ctrl->textbox.multiline_text + physical_offset,
                 length - copied);
    }

    return (int)length;
}

static int textbox_gap_reserve(gui_control_t *ctrl, uint32_t required_gap) {
    uint32_t length = textbox_text_len(ctrl);
    uint32_t gap_size = ctrl->textbox.gap_end - ctrl->textbox.gap_start;
    if (gap_size >= required_gap) return 1;
    if (required_gap > 0x7FFFFFFFU - length) return 0;

    uint32_t new_capacity = ctrl->textbox.buffer_capacity;
    if (new_capacity < TEXTBOX_INITIAL_CAPACITY) new_capacity = TEXTBOX_INITIAL_CAPACITY;
    uint32_t minimum_capacity = length + required_gap;
    while (new_capacity < minimum_capacity) {
        if (new_capacity > 0x3FFFFFFFU) {
            new_capacity = minimum_capacity;
            break;
        }
        new_capacity *= 2;
    }
    if (new_capacity < minimum_capacity || new_capacity > 0x7FFFFFFFU) return 0;

    char *new_buffer = (char*)kmalloc(new_capacity);
    if (!new_buffer) return 0;

    uint32_t prefix_length = ctrl->textbox.gap_start;
    uint32_t tail_length = ctrl->textbox.buffer_capacity - ctrl->textbox.gap_end;
    if (prefix_length)
        memcpy_s(new_buffer, ctrl->textbox.multiline_text, prefix_length);
    uint32_t new_gap_end = new_capacity - tail_length;
    if (tail_length)
        memcpy_s(new_buffer + new_gap_end,
                 ctrl->textbox.multiline_text + ctrl->textbox.gap_end, tail_length);

    kfree(ctrl->textbox.multiline_text);
    ctrl->textbox.multiline_text = new_buffer;
    ctrl->textbox.buffer_capacity = new_capacity;
    ctrl->textbox.gap_end = new_gap_end;
    return 1;
}

static void textbox_gap_move(gui_control_t *ctrl, uint32_t position) {
    if (position < ctrl->textbox.gap_start) {
        uint32_t count = ctrl->textbox.gap_start - position;
        textbox_move_bytes(ctrl->textbox.multiline_text + ctrl->textbox.gap_end - count,
                           ctrl->textbox.multiline_text + position, count);
        ctrl->textbox.gap_start -= count;
        ctrl->textbox.gap_end -= count;
    } else if (position > ctrl->textbox.gap_start) {
        uint32_t count = position - ctrl->textbox.gap_start;
        textbox_move_bytes(ctrl->textbox.multiline_text + ctrl->textbox.gap_start,
                           ctrl->textbox.multiline_text + ctrl->textbox.gap_end, count);
        ctrl->textbox.gap_start += count;
        ctrl->textbox.gap_end += count;
    }
}

int textbox_set_text(gui_control_t *ctrl, const char *text, uint32_t length) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX || (!text && length)) return -1;
    if (length > 0x7FFFFFFFU) return -1;

    if (!ctrl->textbox.is_multiline) {
        uint32_t max_length = ctrl->textbox.max_length;
        if (!max_length || max_length > sizeof(ctrl->text)) max_length = sizeof(ctrl->text);
        if (length >= max_length) return -1;
        for (uint32_t i = 0; i < length; i++) ctrl->text[i] = text[i];
        ctrl->text[length] = '\0';
        ctrl->textbox.cursor_pos = 0;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        ctrl->textbox.revision++;
        textbox_reset_cursor_blink(ctrl);
        ctrl->textbox.last_cursor_pos = 0xFFFFFFFFU;
        ctrl->textbox.preferred_cursor_x_valid = 0;
        return 0;
    }

    uint32_t reserve = length > 0x7FFFFF00U ? 0 : 256;
    uint32_t capacity = length + reserve;
    if (capacity < TEXTBOX_INITIAL_CAPACITY) capacity = TEXTBOX_INITIAL_CAPACITY;
    if (capacity < length || capacity > 0x7FFFFFFFU) capacity = length;
    if (capacity == 0) capacity = TEXTBOX_INITIAL_CAPACITY;

    char *new_buffer = (char*)kmalloc(capacity);
    if (!new_buffer) return -1;
    if (length) memcpy_s(new_buffer, text, length);

    if (ctrl->textbox.multiline_text) kfree(ctrl->textbox.multiline_text);
    ctrl->textbox.multiline_text = new_buffer;
    ctrl->textbox.buffer_capacity = capacity;
    ctrl->textbox.gap_start = length;
    ctrl->textbox.gap_end = capacity;
    ctrl->textbox.cursor_pos = 0;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    ctrl->textbox.current_line = 0;
    ctrl->textbox.revision++;
    textbox_reset_cursor_blink(ctrl);
    ctrl->textbox.last_cursor_pos = 0xFFFFFFFFU;
    ctrl->textbox.preferred_cursor_x_valid = 0;
    return 0;
}

static int textbox_selection_bounds(gui_control_t *ctrl, uint32_t length,
                                    uint32_t *selection_start, uint32_t *selection_end) {
    if (ctrl->textbox.sel_start < 0 ||
        ctrl->textbox.sel_start == ctrl->textbox.sel_end) return 0;

    int32_t start = ctrl->textbox.sel_start;
    int32_t end = ctrl->textbox.sel_end;
    if (start > end) {
        int32_t tmp = start;
        start = end;
        end = tmp;
    }
    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if ((uint32_t)start > length) start = (int32_t)length;
    if ((uint32_t)end > length) end = (int32_t)length;
    if (start == end) return 0;

    *selection_start = (uint32_t)start;
    *selection_end = (uint32_t)end;
    return 1;
}

static int textbox_clipboard_reserve(uint32_t length) {
    if (length <= textbox_clipboard_capacity) return 1;
    if (length > 0x7FFFFFFFU) return 0;

    uint32_t new_capacity = textbox_clipboard_capacity;
    if (new_capacity < TEXTBOX_INITIAL_CAPACITY) new_capacity = TEXTBOX_INITIAL_CAPACITY;
    while (new_capacity < length) {
        if (new_capacity > 0x3FFFFFFFU) {
            new_capacity = length;
            break;
        }
        new_capacity *= 2;
    }

    char *new_clipboard = (char*)kmalloc(new_capacity);
    if (!new_clipboard) return 0;
    if (textbox_clipboard_length)
        memcpy_s(new_clipboard, textbox_clipboard, textbox_clipboard_length);
    if (textbox_clipboard) kfree(textbox_clipboard);
    textbox_clipboard = new_clipboard;
    textbox_clipboard_capacity = new_capacity;
    return 1;
}

static int textbox_copy_selection(gui_control_t *ctrl) {
    uint32_t length = textbox_text_len(ctrl);
    uint32_t selection_start, selection_end;
    if (!textbox_selection_bounds(ctrl, length, &selection_start, &selection_end)) return 0;

    uint32_t copy_length = selection_end - selection_start;
    if (!textbox_clipboard_reserve(copy_length)) return -1;
    if (textbox_copy_range(ctrl, selection_start, textbox_clipboard, copy_length) < 0) return -1;
    textbox_clipboard_length = copy_length;
    return copy_length ? 1 : 0;
}

static int textbox_delete_range(gui_control_t *ctrl, uint32_t start, uint32_t end) {
    uint32_t length = textbox_text_len(ctrl);
    if (start > end || end > length) return 0;
    if (start == end) return 0;

    if (ctrl->textbox.is_multiline) {
        textbox_gap_move(ctrl, start);
        ctrl->textbox.gap_end += end - start;
    } else {
        uint32_t tail_length = length - end;
        for (uint32_t i = 0; i < tail_length; i++)
            ctrl->text[start + i] = ctrl->text[end + i];
        ctrl->text[length - (end - start)] = '\0';
    }

    ctrl->textbox.cursor_pos = start;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    ctrl->textbox.revision++;
    textbox_reset_cursor_blink(ctrl);
    ctrl->textbox.last_cursor_pos = 0xFFFFFFFFU;
    ctrl->textbox.preferred_cursor_x_valid = 0;
    return 1;
}

static int textbox_delete_selection(gui_control_t *ctrl) {
    uint32_t length = textbox_text_len(ctrl);
    uint32_t selection_start, selection_end;
    if (!textbox_selection_bounds(ctrl, length, &selection_start, &selection_end)) return 0;
    return textbox_delete_range(ctrl, selection_start, selection_end);
}

static int textbox_insert_text(gui_control_t *ctrl, const char *insert_text,
                               uint32_t insert_length) {
    if (!insert_text || !insert_length) return 0;

    uint32_t length = textbox_text_len(ctrl);
    uint32_t selection_start = ctrl->textbox.cursor_pos;
    uint32_t selection_end = selection_start;
    textbox_selection_bounds(ctrl, length, &selection_start, &selection_end);
    if (selection_start > length) selection_start = length;
    if (selection_end > length) selection_end = length;
    uint32_t removed = selection_end - selection_start;

    if (ctrl->textbox.is_multiline) {
        uint32_t needed_gap = insert_length > removed ? insert_length - removed : 0;
        if (!textbox_gap_reserve(ctrl, needed_gap)) return -1;
        textbox_gap_move(ctrl, selection_start);
        ctrl->textbox.gap_end += removed;
        for (uint32_t i = 0; i < insert_length; i++)
            ctrl->textbox.multiline_text[ctrl->textbox.gap_start + i] = insert_text[i];
        ctrl->textbox.gap_start += insert_length;
    } else {
        uint32_t max_length = ctrl->textbox.max_length;
        if (!max_length || max_length > sizeof(ctrl->text)) max_length = sizeof(ctrl->text);
        uint32_t final_length = length - removed;
        if (insert_length >= max_length || final_length > max_length - 1 - insert_length)
            return 0;
        uint32_t tail_length = length - selection_end;
        textbox_move_bytes(ctrl->text + selection_start + insert_length,
                           ctrl->text + selection_end, tail_length + 1);
        for (uint32_t i = 0; i < insert_length; i++)
            ctrl->text[selection_start + i] = insert_text[i];
    }

    ctrl->textbox.cursor_pos = selection_start + insert_length;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    ctrl->textbox.revision++;
    textbox_reset_cursor_blink(ctrl);
    ctrl->textbox.last_cursor_pos = 0xFFFFFFFFU;
    ctrl->textbox.preferred_cursor_x_valid = 0;
    return 1;
}

static int textbox_select_all(gui_control_t *ctrl) {
    uint32_t length = textbox_text_len(ctrl);
    if (!length) {
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        ctrl->textbox.cursor_pos = 0;
    } else {
        ctrl->textbox.sel_start = 0;
        ctrl->textbox.sel_end = (int32_t)length;
        ctrl->textbox.cursor_pos = length;
    }
    textbox_reset_cursor_blink(ctrl);
    ctrl->textbox.last_cursor_pos = 0xFFFFFFFFU;
    ctrl->textbox.preferred_cursor_x_valid = 0;
    return 1;
}

int textbox_run_edit_command(gui_control_t *ctrl, int command) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return -1;

    switch (command) {
        case TEXTBOX_EDIT_COPY:
            return textbox_copy_selection(ctrl) < 0 ? -1 : 0;
        case TEXTBOX_EDIT_CUT: {
            int copied = textbox_copy_selection(ctrl);
            if (copied <= 0) return copied;
            return textbox_delete_selection(ctrl);
        }
        case TEXTBOX_EDIT_PASTE:
            return textbox_insert_text(ctrl, textbox_clipboard, textbox_clipboard_length);
        case TEXTBOX_EDIT_SELECT_ALL:
            return textbox_select_all(ctrl);
        default:
            return -1;
    }
}

static int textbox_move_vertical(gui_control_t *ctrl, int direction) {
    if (!ctrl->textbox.is_multiline) return 0;

    extern bmf_font_t font_n;
    int size = ctrl->font_size > 0 ? ctrl->font_size : 12;
    int content_width;
    int total_lines;
    textbox_multiline_layout(&font_n, size, ctrl, &content_width, NULL, &total_lines);

    int current_line;
    int cursor_x;
    textbox_wrapped_cursor_position(&font_n, size, ctrl, ctrl->textbox.cursor_pos,
                                    content_width, &current_line, &cursor_x);
    if (!ctrl->textbox.preferred_cursor_x_valid) {
        ctrl->textbox.preferred_cursor_x = cursor_x;
        ctrl->textbox.preferred_cursor_x_valid = 1;
    }

    int target_line = current_line + direction;
    int has_selection = ctrl->textbox.sel_start >= 0 &&
                        ctrl->textbox.sel_start != ctrl->textbox.sel_end;
    if (target_line < 0 || target_line >= total_lines) {
        if (has_selection) {
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
            return 1;
        }
        return 0;
    }

    uint32_t new_pos = textbox_pos_from_xy(&font_n, size, ctrl, 0,
                                           content_width, target_line,
                                           ctrl->textbox.preferred_cursor_x, 0);
    int changed = has_selection || new_pos != ctrl->textbox.cursor_pos;
    ctrl->textbox.cursor_pos = new_pos;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return changed;
}

int pump_handle_keyboard(gui_form_t *form) {
    if (form->focused_control_id < 0) return 0;

    gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    int key = kbd_getchar_nonblock();
    if (key == 0) return 0;

    uint32_t text_length = textbox_text_len(ctrl);
    int needs_redraw = 0;
    int has_selection = (ctrl->textbox.sel_start >= 0 &&
                         ctrl->textbox.sel_start != ctrl->textbox.sel_end);

    if (key != KEY_UP && key != KEY_DOWN)
        ctrl->textbox.preferred_cursor_x_valid = 0;

    if (key == 14 || key == 15 || key == 19 || key == 6) {
        form->clicked_event.type = SYS_WIN_EVENT_CONTROL_KEY;
        form->clicked_event.id = key;
        return 2;
    }

    if (key == 1 || key == 3 || key == 22 || key == 24) {
        int result = textbox_run_edit_command(ctrl,
            key == 1 ? TEXTBOX_EDIT_SELECT_ALL :
            key == 3 ? TEXTBOX_EDIT_COPY :
            key == 22 ? TEXTBOX_EDIT_PASTE : TEXTBOX_EDIT_CUT);
        if (result < 0) {
            form->clicked_event.type = SYS_WIN_EVENT_TEXT_ERROR;
            form->clicked_event.id = SYS_WIN_TEXT_ERROR_NO_MEMORY;
            return 2;
        }
        if (result) {
            form->clicked_event.type = SYS_WIN_EVENT_TEXT_UPDATE;
            form->clicked_event.id = ctrl->id;
            return 2;
        }
        return 0;
    }

    if (key == KEY_UP) {
        needs_redraw = textbox_move_vertical(ctrl, -1);
    } else if (key == KEY_DOWN) {
        needs_redraw = textbox_move_vertical(ctrl, 1);
    } else if (key == KEY_LEFT) {
        if (has_selection) {
            int32_t sel_min = ctrl->textbox.sel_start < ctrl->textbox.sel_end
                ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = (uint32_t)sel_min;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            ctrl->textbox.cursor_pos--;
        }
        needs_redraw = 1;
    } else if (key == KEY_RIGHT) {
        if (has_selection) {
            int32_t sel_max = ctrl->textbox.sel_start > ctrl->textbox.sel_end
                ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = (uint32_t)sel_max;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos < text_length) {
            ctrl->textbox.cursor_pos++;
        }
        needs_redraw = 1;
    } else if (key == KEY_HOME) {
        ctrl->textbox.cursor_pos = 0;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    } else if (key == KEY_END) {
        ctrl->textbox.cursor_pos = text_length;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    } else if (key == '\b') {
        if (has_selection) {
            needs_redraw = textbox_delete_selection(ctrl);
        } else if (ctrl->textbox.cursor_pos > 0) {
            needs_redraw = textbox_delete_range(ctrl, ctrl->textbox.cursor_pos - 1,
                                                ctrl->textbox.cursor_pos);
        }
    } else if (key == KEY_DELETE) {
        if (has_selection) {
            needs_redraw = textbox_delete_selection(ctrl);
        } else if (ctrl->textbox.cursor_pos < text_length) {
            needs_redraw = textbox_delete_range(ctrl, ctrl->textbox.cursor_pos,
                                                ctrl->textbox.cursor_pos + 1);
        }
    } else if (key == '\n' || key == '\r') {
        if (ctrl->textbox.is_multiline) {
            char newline = '\n';
            needs_redraw = textbox_insert_text(ctrl, &newline, 1);
            if (needs_redraw < 0) {
                form->clicked_event.type = SYS_WIN_EVENT_TEXT_ERROR;
                form->clicked_event.id = SYS_WIN_TEXT_ERROR_NO_MEMORY;
                return 2;
            }
        } else {
            form->clicked_event.type = SYS_WIN_EVENT_CONTROL;
            form->clicked_event.id = ctrl->id;
            return 2;
        }
    } else if (key == KEY_ESC) {
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        form->focused_control_id = -1;
        needs_redraw = 1;
    } else if (key >= 0x20 && key < 0x80) {
        char character = (char)key;
        needs_redraw = textbox_insert_text(ctrl, &character, 1);
        if (needs_redraw < 0) {
            form->clicked_event.type = SYS_WIN_EVENT_TEXT_ERROR;
            form->clicked_event.id = SYS_WIN_TEXT_ERROR_NO_MEMORY;
            return 2;
        }
    }

    if (needs_redraw) {
        textbox_reset_cursor_blink(ctrl);
        form->clicked_event.type = SYS_WIN_EVENT_TEXT_UPDATE;
        form->clicked_event.id = ctrl->id;
        return 2;
    }
    return 0;
}
