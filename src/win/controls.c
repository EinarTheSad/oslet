#include "controls.h"
#include "theme.h"
#include "bitmap.h"
#include "window.h"
#include "icon.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"

extern bmf_font_t font_b, font_n, font_i, font_bi;

void ctrl_draw_button(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    /* Use theme button_color when bg is -1 (unset) */
    uint8_t btn_color = (control->bg == -1) ? theme->button_color : control->bg;

    if (control->pressed) {
        win_draw_button(abs_x, abs_y, control->w, control->h, btn_color, control->text, 1);
    } else {
        win_draw_button(abs_x, abs_y, control->w, control->h, btn_color, control->text, 0);
    }
}

void ctrl_draw_label(gui_control_t *control, int abs_x, int abs_y) {
    bmf_font_t *font = &font_n;

    if (control->font_type == 1) font = &font_b;
    else if (control->font_type == 2) font = &font_i;
    else if (control->font_type == 3) font = &font_bi;

    int size = control->font_size > 0 ? control->font_size : 12;

    if (font->data) {
        /* Get actual font height from sequence */
        int seq_idx = -1;
        for (int i = 0; i < font->size_count; i++) {
            if (font->sequences[i].point_size == size) {
                seq_idx = i;
                break;
            }
        }

        int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

        /* Calculate text dimensions */
        int max_line_width = 0;
        int line_count = 1;
        int current_line_width = 0;

        const char *p = control->text;
        const char *line_start = p;

        while (*p) {
            if (*p == '\\' && *(p+1) == 'n') {
                /* Measure current line */
                int len = p - line_start;
                char temp[256];
                if (len > 0 && len < 256) {
                    int i;
                    for (i = 0; i < len; i++) temp[i] = line_start[i];
                    temp[i] = '\0';
                    current_line_width = bmf_measure_text(font, size, temp);
                    if (current_line_width > max_line_width) {
                        max_line_width = current_line_width;
                    }
                }
                line_count++;
                p += 2;
                line_start = p;
                current_line_width = 0;
            } else {
                p++;
            }
        }

        /* Measure last line */
        if (line_start < p) {
            int len = p - line_start;
            char temp[256];
            if (len > 0 && len < 256) {
                int i;
                for (i = 0; i < len; i++) temp[i] = line_start[i];
                temp[i] = '\0';
                current_line_width = bmf_measure_text(font, size, temp);
                if (current_line_width > max_line_width) {
                    max_line_width = current_line_width;
                }
            }
        }

        /* Calculate height: first_line_height + (other_lines * (font_height + 2)) */
        int text_h = font_height + ((line_count - 1) * (font_height + 2)) + 4;

        int label_w = control->w > 0 ? control->w : max_line_width + 4;
        int label_h = control->h > 0 ? control->h : text_h;

        /* Labels with bg=-1 are transparent (no background fill) */
        if (control->bg != -1) {
            gfx_fillrect(abs_x, abs_y, label_w, label_h, control->bg);
        }

        if (control->border) {
            gfx_rect(abs_x, abs_y, label_w, label_h, control->border_color);
        }

        char formatted_text[256];
        int j = 0;
        for (int i = 0; control->text[i] && j < 255; i++) {
            if (control->text[i] == '\\' && control->text[i+1] == 'n') {
                formatted_text[j++] = '\n';
                i++;
            } else {
                formatted_text[j++] = control->text[i];
            }
        }
        formatted_text[j] = '\0';

        int text_x = abs_x + 2;
        int text_y = abs_y + 5;
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", formatted_text);
    }
}

void ctrl_draw_picturebox(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    gfx_fillrect(abs_x, abs_y, control->w, control->h, theme->frame_dark);
    gfx_rect(abs_x, abs_y, control->w, control->h, theme->button_color);

    if (control->text[0]) {
        /* Load bitmap to cache if not already loaded */
        if (!control->cached_bitmap) {
            control->cached_bitmap = bitmap_load_from_file(control->text);
        }

        /* Draw from cached bitmap */
        if (control->cached_bitmap) {
            bitmap_draw(control->cached_bitmap, abs_x, abs_y);
        }
    }
}

void ctrl_draw_checkbox(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    int box_size = 13;

    gfx_fillrect(abs_x, abs_y, box_size, box_size, COLOR_WHITE);

    // 3D border effect (sunken)
    gfx_hline(abs_x, abs_y, box_size, theme->frame_dark);  // Top
    gfx_vline(abs_x, abs_y, box_size, theme->frame_dark);  // Left
    gfx_hline(abs_x, abs_y + box_size - 1, box_size, theme->frame_light);  // Bottom
    gfx_vline(abs_x + box_size - 1, abs_y, box_size, theme->frame_light);  // Right

    // Inner border
    gfx_hline(abs_x + 1, abs_y + 1, box_size - 2, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, box_size - 2, COLOR_DARK_GRAY);

    // Draw checkmark if checked
    if (control->checked) {
        // Simple X checkmark
        for (int i = 0; i < 7; i++) {
            gfx_putpixel(abs_x + 3 + i, abs_y + 3 + i, control->fg);
            gfx_putpixel(abs_x + 9 - i, abs_y + 3 + i, control->fg);
        }
    }

    // Draw label text
    if (control->text[0] && font->data) {
        int text_x = abs_x + box_size + 4;
        int text_y = abs_y + 3;
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", control->text);
    }
}

void ctrl_draw_radiobutton(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    // Draw radio button circle (12x12 pixels)
    int radius = 6;
    int center_x = abs_x + radius;
    int center_y = abs_y + radius;

    // Fill background circle
    gfx_fillrect(abs_x, abs_y, 12, 12, COLOR_WHITE);

    // Draw outer circle
    gfx_circle(center_x, center_y, radius - 1, theme->frame_dark);

    // Draw selected dot if checked
    if (control->checked) {
        gfx_circle(center_x, center_y, 2, control->fg);
        gfx_circle(center_x, center_y, 1, control->fg);
        gfx_putpixel(center_x, center_y, control->fg);
    }

    // Draw label text
    if (control->text[0] && font->data) {
        int text_x = abs_x + 12 + 4;
        int text_y = abs_y + 3;
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", control->text);
    }
}

static int textbox_measure_to_pos(bmf_font_t *font, int size, const char *text, int pos) {
    int width = 0;
    for (int i = 0; i < pos && text[i]; i++) {
        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)text[i]);
        if (g) width += g->width;
    }
    return width;
}

int textbox_pos_from_x(bmf_font_t *font, int size, const char *text, int scroll_offset, int rel_x) {
    int x = -scroll_offset;
    int pos = 0;
    while (text[pos]) {
        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)text[pos]);
        int char_w = g ? g->width : 6;
        if (x + char_w / 2 >= rel_x) {
            return pos;
        }
        x += char_w;
        pos++;
    }
    return pos;
}

void ctrl_draw_textbox(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    // Draw textbox background
    gfx_fillrect(abs_x, abs_y, control->w, control->h, COLOR_WHITE);

    // Draw 3D sunken border effect
    gfx_hline(abs_x, abs_y, control->w, theme->frame_dark);  // Top
    gfx_vline(abs_x, abs_y, control->h, theme->frame_dark);  // Left
    gfx_hline(abs_x, abs_y + control->h - 1, control->w, theme->frame_light);  // Bottom
    gfx_vline(abs_x + control->w - 1, abs_y, control->h, theme->frame_light);  // Right

    // Inner shadow
    gfx_hline(abs_x + 1, abs_y + 1, control->w - 2, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 2, COLOR_DARK_GRAY);

    // Text area dimensions (inside borders)
    int text_area_x = abs_x + 3;
    int text_area_w = control->w - 6;
    int text_x = text_area_x + 1;
    int text_y = abs_y + 6;

    // Get font height for cursor
    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

    if (!font->data) return;

    // Calculate cursor position in pixels (before scroll adjustment)
    int cursor_pixel_x = textbox_measure_to_pos(font, size, control->text, control->cursor_pos);

    // Auto-scroll to keep cursor visible
    int scroll = control->scroll_offset;
    if (cursor_pixel_x - scroll > text_area_w - 2) {
        // Cursor past right edge - scroll right
        scroll = cursor_pixel_x - text_area_w + 10;
    } else if (cursor_pixel_x - scroll < 0) {
        // Cursor past left edge - scroll left
        scroll = cursor_pixel_x - 10;
        if (scroll < 0) scroll = 0;
    }
    control->scroll_offset = scroll;

    // Draw selection highlight if there's a selection
    if (control->sel_start >= 0 && control->sel_start != control->sel_end) {
        int sel_min = control->sel_start < control->sel_end ? control->sel_start : control->sel_end;
        int sel_max = control->sel_start > control->sel_end ? control->sel_start : control->sel_end;

        int sel_x1 = text_x + textbox_measure_to_pos(font, size, control->text, sel_min) - scroll;
        int sel_x2 = text_x + textbox_measure_to_pos(font, size, control->text, sel_max) - scroll;

        // Clip selection to text area
        if (sel_x1 < text_area_x) sel_x1 = text_area_x;
        if (sel_x2 > text_area_x + text_area_w) sel_x2 = text_area_x + text_area_w;

        if (sel_x2 > sel_x1) {
            gfx_fillrect(sel_x1, abs_y + 3, sel_x2 - sel_x1, font_height + 2, COLOR_BLUE);
        }
    }

    // Draw text with clipping (character by character)
    if (control->text[0]) {
        int x = text_x - scroll;
        int i = 0;

        // Skip characters that are completely scrolled off left
        while (control->text[i]) {
            const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)control->text[i]);
            int char_w = g ? g->width : 0;
            if (x + char_w > text_area_x) break;
            x += char_w;
            i++;
        }

        // Draw visible characters
        while (control->text[i]) {
            if (x >= text_area_x + text_area_w) break;  // Past right edge

            const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)control->text[i]);
            if (g && x >= text_area_x - g->width) {
                // Determine text color (white on selection, normal otherwise)
                int in_selection = 0;
                if (control->sel_start >= 0 && control->sel_start != control->sel_end) {
                    int sel_min = control->sel_start < control->sel_end ? control->sel_start : control->sel_end;
                    int sel_max = control->sel_start > control->sel_end ? control->sel_start : control->sel_end;
                    in_selection = (i >= sel_min && i < sel_max);
                }
                uint8_t color = in_selection ? COLOR_WHITE : control->fg;
                bmf_draw_char(x, text_y, font, size, (uint8_t)control->text[i], color);
            }
            x += g ? g->width : 0;
            i++;
        }
    }

    // Draw cursor if focused (and no selection, or at selection edge)
    if (control->is_focused) {
        int cursor_x = text_x + cursor_pixel_x - scroll;

        // Only draw cursor if within visible area
        if (cursor_x >= text_area_x && cursor_x < text_area_x + text_area_w) {
            int cursor_y = text_y - 3;
            int cursor_height = font_height + 1;
            gfx_vline(cursor_x, cursor_y, cursor_height, control->fg);
        }
    }
}

void ctrl_draw_icon(gui_control_t *control, int abs_x, int abs_y, uint8_t win_bg) {
    window_theme_t *theme = theme_get_current();

    /* Icon dimensions - use control size or defaults */
    int icon_size = 32;
    int label_max_w = control->w > 0 ? control->w : 48;
    int total_w = label_max_w;
    int max_line_width = label_max_w - 2;

    /* Calculate dynamic height based on label lines */
    int label_lines = icon_count_label_lines(control->text, max_line_width);
    int total_h = icon_calc_total_height(icon_size, label_lines);

    /* Calculate icon position (centered horizontally) */
    int icon_x = abs_x + (total_w - icon_size) / 2;
    int icon_y = abs_y;

    /* Draw icon bitmap or default */
    if (control->cached_bitmap) {
        bitmap_draw(control->cached_bitmap, icon_x, icon_y);
    } else {
        /* Draw default icon rectangle */
        gfx_fillrect(icon_x, icon_y, icon_size, icon_size, theme->button_color);
        gfx_rect(icon_x, icon_y, icon_size, icon_size, theme->frame_dark);

        /* Draw first 2 letters of label as initials */
        if (font_b.data && control->text[0]) {
            char initials[3];
            initials[0] = control->text[0];
            initials[1] = control->text[1] ? control->text[1] : '\0';
            initials[2] = '\0';

            int tw = bmf_measure_text(&font_b, 12, initials);
            int tx = icon_x + (icon_size - tw) / 2;
            int ty = icon_y + 10;
            bmf_printf(tx, ty, &font_b, 12, theme->text_color, "%s", initials);
        }
    }

    /* Draw selection highlight */
    if (control->checked) {
        /* Dither pattern over entire icon area (using dynamic height) */
        for (int py = 0; py < total_h; py++) {
            for (int px = 0; px < total_w; px++) {
                if ((px + py) % 2 == 0) {
                    gfx_putpixel(abs_x + px, abs_y + py, 1);  /* Dark blue */
                }
            }
        }
    }

    /* Draw label below icon using shared function */
    if (control->text[0]) {
        uint8_t text_color = control->checked ? 15 : control->fg;
        int text_y = abs_y + icon_size + 4;
        icon_draw_label_wrapped(control->text, abs_x, text_y, total_w, max_line_width, text_color);
    }

    (void)win_bg;  /* Reserved for future use */
}

/* Helper: get item text from dropdown options (items separated by |) */
static const char* dropdown_get_item(const char *text, int index, char *buf, int buf_size) {
    const char *p = text;
    int current = 0;
    int len = 0;

    while (*p && current < index) {
        if (*p == '|') current++;
        p++;
    }

    while (*p && *p != '|' && len < buf_size - 1) {
        buf[len++] = *p++;
    }
    buf[len] = '\0';
    return buf;
}

/* Count items in dropdown (pipe-separated) */
static int dropdown_count_items(const char *text) {
    if (!text || !text[0]) return 0;
    int count = 1;
    const char *p = text;
    while (*p) {
        if (*p == '|') count++;
        p++;
    }
    return count;
}

void ctrl_draw_dropdown(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    int btn_w = 16;
    int field_w = control->w - btn_w;

    /* Draw text field background */
    gfx_fillrect(abs_x, abs_y, field_w, control->h, COLOR_WHITE);

    /* 3D sunken border for text field */
    gfx_hline(abs_x, abs_y, field_w, theme->frame_dark);
    gfx_vline(abs_x, abs_y, control->h, theme->frame_dark);
    gfx_hline(abs_x, abs_y + control->h - 1, field_w, theme->frame_light);
    gfx_vline(abs_x + field_w - 1, abs_y, control->h, theme->frame_light);

    /* Inner shadow */
    gfx_hline(abs_x + 1, abs_y + 1, field_w - 2, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 2, COLOR_DARK_GRAY);

    /* Draw dropdown button */
    int btn_x = abs_x + field_w;
    gfx_fillrect(btn_x, abs_y, btn_w, control->h, theme->button_color);

    /* Button 3D border */
    if (control->pressed) {
        gfx_hline(btn_x, abs_y, btn_w, theme->frame_dark);
        gfx_vline(btn_x, abs_y, control->h, theme->frame_dark);
        gfx_hline(btn_x, abs_y + control->h - 1, btn_w, COLOR_WHITE);
        gfx_vline(btn_x + btn_w - 1, abs_y, control->h, COLOR_WHITE);
    } else {
        gfx_hline(btn_x, abs_y, btn_w, COLOR_WHITE);
        gfx_vline(btn_x, abs_y, control->h, COLOR_WHITE);
        gfx_hline(btn_x, abs_y + control->h - 1, btn_w, theme->frame_dark);
        gfx_vline(btn_x + btn_w - 1, abs_y, control->h, theme->frame_dark);
    }

    /* Draw arrow in button */
    int arrow_x = btn_x + btn_w / 2;
    int arrow_y = abs_y + control->h / 2 - 2;
    for (int i = 0; i < 4; i++) {
        gfx_hline(arrow_x - 3 + i, arrow_y + i, 7 - i * 2, COLOR_BLACK);
    }

    /* Draw selected item text */
    if (font->data) {
        char item_text[64];
        int selected = control->cursor_pos;  /* cursor_pos used as selected_index */
        dropdown_get_item(control->text, selected, item_text, sizeof(item_text));

        int text_x = abs_x + 4;
        int text_y = abs_y + (control->h - 12) / 2 + 3;
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", item_text);
    }
    /* Note: dropdown list is drawn separately via ctrl_draw_dropdown_list for z-order */
}

/* Draw only the dropdown list (called after all controls for z-order) */
void ctrl_draw_dropdown_list(window_t *win, gui_control_t *control) {
    if (!control->dropdown_open) return;

    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    int abs_x = win->x + control->x;
    int abs_y = win->y + control->y + 20;

    int item_count = dropdown_count_items(control->text);
    int item_h = 16;
    int list_h = item_count * item_h;
    int list_y = abs_y + control->h;

    /* Allow list to extend beyond window - no clipping */

    /* List background */
    gfx_fillrect(abs_x, list_y, control->w, list_h, COLOR_WHITE);
    gfx_rect(abs_x, list_y, control->w, list_h, theme->frame_dark);

    /* Draw items */
    for (int i = 0; i < item_count; i++) {
        char item_text[64];
        dropdown_get_item(control->text, i, item_text, sizeof(item_text));

        int item_y = list_y + i * item_h;

        /* Highlight selected item */
        if (i == control->cursor_pos) {
            gfx_fillrect(abs_x + 1, item_y, control->w - 2, item_h, COLOR_BLUE);
            if (font->data) {
                bmf_printf(abs_x + 4, item_y + 3, font, size, COLOR_WHITE, "%s", item_text);
            }
        } else {
            if (font->data) {
                bmf_printf(abs_x + 4, item_y + 3, font, size, control->fg, "%s", item_text);
            }
        }
    }
}

void ctrl_draw_frame(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;

    // Use same font properties as labels
    if (control->font_type == 1) font = &font_b;
    else if (control->font_type == 2) font = &font_i;
    else if (control->font_type == 3) font = &font_bi;

    int size = control->font_size > 0 ? control->font_size : 12;

    // Get actual font height
    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

    // Calculate title dimensions
    int title_width = 0;
    if (control->text[0] && font->data) {
        title_width = bmf_measure_text(font, size, control->text);
    }

    // Draw frame border
    int title_offset = 8;
    int border_y = abs_y + font_height / 2 + 2;

    // Top line (with gap for title)
    if (title_width > 0) {
        gfx_hline(abs_x, border_y, title_offset, theme->frame_dark);
        gfx_hline(abs_x + title_offset + title_width + 8, border_y,
                  control->w - title_offset - title_width - 8, theme->frame_dark);
    } else {
        gfx_hline(abs_x, border_y, control->w, theme->frame_dark);
    }

    // Other sides
    gfx_vline(abs_x, border_y, control->h - (border_y - abs_y), theme->frame_dark);
    gfx_hline(abs_x, abs_y + control->h - 1, control->w, theme->frame_light);
    gfx_vline(abs_x + control->w - 1, border_y, control->h - (border_y - abs_y), theme->frame_light);

    // Draw title text with background
    if (control->text[0] && font->data) {
        int text_x = abs_x + title_offset + 4;
        int text_y = abs_y + 5;

        // Draw title text
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", control->text);
    }
}

void ctrl_draw(window_t *win, gui_control_t *control) {
    int abs_x = win->x + control->x;
    int abs_y = win->y + control->y + 20;

    if (control->type == 1) { /* CTRL_BUTTON */
        ctrl_draw_button(control, abs_x, abs_y);
    }
    else if (control->type == 2) { /* CTRL_LABEL */
        ctrl_draw_label(control, abs_x, abs_y);
    }
    else if (control->type == 3) { /* CTRL_PICTUREBOX */
        ctrl_draw_picturebox(control, abs_x, abs_y);
    }
    else if (control->type == 4) { /* CTRL_CHECKBOX */
        ctrl_draw_checkbox(control, abs_x, abs_y);
    }
    else if (control->type == 5) { /* CTRL_RADIOBUTTON */
        ctrl_draw_radiobutton(control, abs_x, abs_y);
    }
    else if (control->type == 6) { /* CTRL_TEXTBOX */
        ctrl_draw_textbox(control, abs_x, abs_y);
    }
    else if (control->type == 7) { /* CTRL_FRAME */
        ctrl_draw_frame(control, abs_x, abs_y);
    }
    else if (control->type == 8) { /* CTRL_ICON */
        ctrl_draw_icon(control, abs_x, abs_y, 7);  /* Use default gray background */
    }
    else if (control->type == 9) { /* CTRL_DROPDOWN */
        ctrl_draw_dropdown(control, abs_x, abs_y);
    }
}

int text_split_lines(const char *text, char lines[][256], int max_lines) {
    int line_count = 0;
    const char *p = text;
    const char *line_start = p;

    while (*p && line_count < max_lines) {
        if (*p == '\\' && *(p+1) == 'n') {
            /* Copy current line */
            int len = p - line_start;
            if (len > 0 && len < 256) {
                int i;
                for (i = 0; i < len; i++) {
                    lines[line_count][i] = line_start[i];
                }
                lines[line_count][i] = '\0';
                line_count++;
            }
            p += 2;
            line_start = p;
        } else {
            p++;
        }
    }

    /* Copy last line */
    if (line_start < p && line_count < max_lines) {
        int len = p - line_start;
        if (len > 0 && len < 256) {
            int i;
            for (i = 0; i < len; i++) {
                lines[line_count][i] = line_start[i];
            }
            lines[line_count][i] = '\0';
            line_count++;
        }
    }

    return line_count;
}

int text_measure_height(const char *text, void *font_ptr, int size) {
    bmf_font_t *font = (bmf_font_t*)font_ptr;

    /* Get font height */
    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

    /* Count lines */
    int line_count = 1;
    const char *p = text;
    while (*p) {
        if (*p == '\\' && *(p+1) == 'n') {
            line_count++;
            p += 2;
        } else {
            p++;
        }
    }

    return font_height + ((line_count - 1) * (font_height + 2)) + 4;
}
