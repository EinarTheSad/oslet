#include "internal.h"

static int textbox_measure_to_pos(bmf_font_t *font, int size, gui_control_t *ctrl,
                                  uint32_t pos) {
    int width = 0;
    uint32_t length = textbox_text_len(ctrl);
    if (pos > length) pos = length;
    for (uint32_t i = 0; i < pos; i++) {
        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)textbox_char_at(ctrl, i));
        if (g) width += g->width;
    }
    return width;
}

int textbox_pos_from_x(bmf_font_t *font, int size, gui_control_t *ctrl,
                       int scroll_offset, int rel_x) {
    int x = -scroll_offset;
    uint32_t pos = 0;
    uint32_t length = textbox_text_len(ctrl);
    while (pos < length) {
        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)textbox_char_at(ctrl, pos));
        int char_w = g ? g->width : 6;
        if (x + char_w / 2 >= rel_x) {
            return (int)pos;
        }
        x += char_w;
        pos++;
    }
    return (int)pos;
}

int textbox_wrap_line_count(bmf_font_t *font, int size, gui_control_t *ctrl,
                            int text_area_w) {
    int line_count = 1;
    int line_x = 0;
    uint32_t length = textbox_text_len(ctrl);

    for (uint32_t i = 0; i < length; i++) {
        char character = textbox_char_at(ctrl, i);
        if (character == '\n') {
            line_count++;
            line_x = 0;
            continue;
        }

        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)character);
        int char_w = g ? g->width : 6;
        if (line_x + char_w > text_area_w) {
            line_count++;
            line_x = 0;
        }
        line_x += char_w;
    }

    return line_count;
}

void textbox_wrapped_cursor_position(bmf_font_t *font, int size, gui_control_t *ctrl,
                                     uint32_t pos, int text_area_w,
                                     int *out_line, int *out_x) {
    int line = 0;
    int x = 0;
    uint32_t length = textbox_text_len(ctrl);
    if (pos > length) pos = length;

    for (uint32_t i = 0; i < pos; i++) {
        char character = textbox_char_at(ctrl, i);
        if (character == '\n') {
            line++;
            x = 0;
            continue;
        }

        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)character);
        int char_w = g ? g->width : 6;
        if (x + char_w > text_area_w) {
            line++;
            x = 0;
        }
        x += char_w;
    }

    if (pos < length) {
        char character = textbox_char_at(ctrl, pos);
        if (character != '\n') {
            const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)character);
            int char_w = g ? g->width : 6;
            if (x + char_w > text_area_w) {
                line++;
                x = 0;
            }
        }
    }

    if (out_line) *out_line = line;
    if (out_x) *out_x = x;
}

int textbox_multiline_layout(bmf_font_t *font, int size, gui_control_t *ctrl,
                             int *content_width, int *visible_lines, int *total_lines) {
    int width = ctrl->w - 6;
    if (width < 1) width = 1;

    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;
    int line_height = font_height + 2;
    int lines_visible = (ctrl->h - 9) / line_height;
    if (lines_visible < 1) lines_visible = 1;

    int lines_total = textbox_wrap_line_count(font, size, ctrl, width);
    int needs_scrollbar = lines_total > lines_visible;
    if (needs_scrollbar) {
        width -= 18;
        if (width < 10) width = 10;
        lines_total = textbox_wrap_line_count(font, size, ctrl, width);
    }

    if (content_width) *content_width = width;
    if (visible_lines) *visible_lines = lines_visible;
    if (total_lines) *total_lines = lines_total;
    return needs_scrollbar;
}

uint32_t textbox_pos_from_xy(bmf_font_t *font, int size, gui_control_t *ctrl,
                             int text_area_x, int text_area_w, int start_line,
                             int rel_x, int rel_y) {
    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;
    int line_height = font_height + 2;

    int target_line = start_line + rel_y / line_height;
    if (target_line < 0) target_line = 0;

    int line_x = text_area_x;
    int line_y = 0;
    uint32_t char_idx = 0;
    uint32_t text_len = textbox_text_len(ctrl);

    int current_line = 0;
    while (char_idx < text_len) {
        if (textbox_char_at(ctrl, char_idx) == '\n') {
            if (current_line == target_line) {
                return char_idx;
            }
            current_line++;
            line_x = text_area_x;
            line_y += line_height;
            char_idx++;
            continue;
        }

        const bmf_glyph_t *g = bmf_get_glyph(font, size,
                                             (uint8_t)textbox_char_at(ctrl, char_idx));
        int char_w = g ? g->width : 6;

        if (line_x + char_w > text_area_x + text_area_w) {
            if (current_line == target_line) {
                return char_idx;
            }
            current_line++;
            line_x = text_area_x;
            line_y += line_height;
        }

        if (current_line == target_line) {
            if (line_x + char_w / 2 >= rel_x + text_area_x) {
                return char_idx;
            }
        }

        line_x += char_w;
        char_idx++;
    }

    if (current_line == target_line) {
        return text_len;
    }

    return text_len;
}

void ctrl_draw_textbox(gui_control_t *control, int abs_x, int abs_y) {
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    gfx_fillrect(abs_x, abs_y, control->w, control->h, COLOR_WHITE);
    ctrl_draw_sunken_border(abs_x, abs_y, control->w, control->h);

    int text_area_x = abs_x + 3;
    int text_area_w = control->w - 6;
    int text_x = text_area_x + 1;
    int text_y = abs_y + 6;

    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;
    int line_height = font_height + 2;

    if (!font->data) return;

    uint32_t cursor_char_pos = control->textbox.cursor_pos;
    uint32_t text_len = textbox_text_len(control);

    int scroll = control->textbox.scroll_offset;

    if (control->textbox.is_multiline) {
        int sb_w = 18;
        int visible_lines;
        int total_lines;
        int needs_scrollbar = textbox_multiline_layout(
            font, size, control, &text_area_w, &visible_lines, &total_lines);

        int max_scroll = total_lines > visible_lines ? total_lines - visible_lines : 0;
        int cursor_line = 0;
        int cursor_x_offset = 0;
        textbox_wrapped_cursor_position(font, size, control, cursor_char_pos,
                                        text_area_w, &cursor_line, &cursor_x_offset);
        if (control->textbox.last_cursor_pos != cursor_char_pos) {
            if (cursor_line < (int)control->textbox.current_line)
                control->textbox.current_line = cursor_line;
            if (cursor_line >= (int)control->textbox.current_line + visible_lines)
                control->textbox.current_line = cursor_line - visible_lines + 1;
            control->textbox.last_cursor_pos = cursor_char_pos;
        }
        if (control->textbox.current_line > (uint32_t)max_scroll)
            control->textbox.current_line = (uint32_t)max_scroll;

        int start_line = (int)control->textbox.current_line;
        int cursor_y = text_y + (cursor_line - start_line) * line_height;
        int cursor_x = text_x + cursor_x_offset;

        int line_index = 0;
        int line_x = text_x;
        int line_y = text_y - start_line * line_height;
        uint32_t char_idx = 0;

        int32_t sel_min = -1, sel_max = -1;
        if (control->textbox.sel_start >= 0 && control->textbox.sel_start != control->textbox.sel_end) {
            sel_min = control->textbox.sel_start < control->textbox.sel_end ? control->textbox.sel_start : control->textbox.sel_end;
            sel_max = control->textbox.sel_start > control->textbox.sel_end ? control->textbox.sel_start : control->textbox.sel_end;
        }

        while (char_idx < text_len) {
            char character = textbox_char_at(control, char_idx);
            if (character == '\n') {
                line_x = text_x;
                line_y += line_height;
                line_index++;
                char_idx++;
                if (line_index >= start_line + visible_lines) break;
                continue;
            }

            const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)character);
            int char_w = g ? g->width : 6;

            if (line_x + char_w > text_area_x + text_area_w) {
                line_x = text_x;
                line_y += line_height;
                line_index++;
                if (line_index >= start_line + visible_lines) break;
            }

            if (line_index >= start_line && line_index < start_line + visible_lines) {
                int in_selection = 0;
                if (sel_min >= 0 && char_idx >= (uint32_t)sel_min &&
                    char_idx < (uint32_t)sel_max) {
                    in_selection = 1;
                    gfx_fillrect(line_x, line_y - 3, char_w, font_height + 2, COLOR_BLUE);
                }

                if (line_y + font_height < abs_y + control->h - 3) {
                    uint8_t color = in_selection ? COLOR_WHITE : control->fg;
                    bmf_draw_char(line_x, line_y, font, size,
                                  (uint8_t)textbox_char_at(control, char_idx), color);
                }
            }

            line_x += char_w;
            char_idx++;
        }

        if (control->textbox.is_focused && control->textbox.cursor_visible &&
            cursor_line >= start_line && cursor_line < start_line + visible_lines &&
            cursor_y + font_height < abs_y + control->h - 3) {
            gfx_vline(cursor_x, cursor_y - 3, font_height + 1, control->fg);
        }

        if (needs_scrollbar) {
            gui_control_t sb = {0};
            sb.type = CTRL_SCROLLBAR;
            sb.w = sb_w;
            sb.h = control->h - 2;
            sb.scrollbar.checked = 0;
            sb.scrollbar.cursor_pos = control->textbox.current_line;
            sb.scrollbar.max_length = max_scroll > 0 ? max_scroll : 1;
            sb.scrollbar.hovered_item = control->textbox.scrollbar_hovered_item;
            sb.scrollbar.pressed = control->textbox.scrollbar_pressed;

            ctrl_draw_scrollbar(&sb, abs_x + control->w - sb_w, abs_y + 1);
        }
    } else {
        int cursor_pixel_x = textbox_measure_to_pos(font, size, control, cursor_char_pos);

        if (cursor_pixel_x - scroll > text_area_w - 2) {
            scroll = cursor_pixel_x - text_area_w + 10;
        } else if (cursor_pixel_x - scroll < 0) {
            scroll = cursor_pixel_x - 10;
            if (scroll < 0) scroll = 0;
        }
        control->textbox.scroll_offset = scroll;

        if (control->textbox.sel_start >= 0 && control->textbox.sel_start != control->textbox.sel_end) {
            int sel_min = control->textbox.sel_start < control->textbox.sel_end ? control->textbox.sel_start : control->textbox.sel_end;
            int sel_max = control->textbox.sel_start > control->textbox.sel_end ? control->textbox.sel_start : control->textbox.sel_end;

            int sel_x1 = text_x + textbox_measure_to_pos(font, size, control, (uint32_t)sel_min) - scroll;
            int sel_x2 = text_x + textbox_measure_to_pos(font, size, control, (uint32_t)sel_max) - scroll;

            if (sel_x1 < text_area_x) sel_x1 = text_area_x;
            if (sel_x2 > text_area_x + text_area_w) sel_x2 = text_area_x + text_area_w;

            if (sel_x2 > sel_x1) {
                gfx_fillrect(sel_x1, abs_y + 3, sel_x2 - sel_x1, font_height + 2, COLOR_BLUE);
            }
        }

        if (text_len) {
            int x = text_x - scroll;
            uint32_t i = 0;

            while (i < text_len) {
                const bmf_glyph_t *g = bmf_get_glyph(font, size,
                                                     (uint8_t)textbox_char_at(control, i));
                int char_w = g ? g->width : 0;
                if (x + char_w > text_area_x) break;
                x += char_w;
                i++;
            }

            while (i < text_len) {
                if (x >= text_area_x + text_area_w) break;

                char character = textbox_char_at(control, i);
                const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)character);
                if (g && x >= text_area_x - g->width) {
                    int in_selection = 0;
                    if (control->textbox.sel_start >= 0 && control->textbox.sel_start != control->textbox.sel_end) {
                        int sel_min = control->textbox.sel_start < control->textbox.sel_end ? control->textbox.sel_start : control->textbox.sel_end;
                        int sel_max = control->textbox.sel_start > control->textbox.sel_end ? control->textbox.sel_start : control->textbox.sel_end;
                        in_selection = (i >= (uint32_t)sel_min &&
                                        i < (uint32_t)sel_max);
                    }
                    uint8_t color = in_selection ? COLOR_WHITE : control->fg;
                    bmf_draw_char(x, text_y, font, size, (uint8_t)character, color);
                }
                x += g ? g->width : 0;
                i++;
            }
        }

        if (control->textbox.is_focused && control->textbox.cursor_visible) {
            int cx = text_x + cursor_pixel_x - scroll;

            if (cx >= text_area_x && cx < text_area_x + text_area_w) {
                int cursor_y = text_y - 3;
                int cursor_height = font_height + 1;
                gfx_vline(cx, cursor_y, cursor_height, control->fg);
            }
        }
    }
}

