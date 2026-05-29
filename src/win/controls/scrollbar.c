#include "internal.h"

void ctrl_draw_frame(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;

    if (control->font_type == 1) font = &font_b;
    else if (control->font_type == 2) font = &font_i;
    else if (control->font_type == 3) font = &font_bi;

    int size = control->font_size > 0 ? control->font_size : 12;

    int seq_idx = -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == size) {
            seq_idx = i;
            break;
        }
    }
    int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

    int title_width = 0;
    if (control->text[0] && font->data) {
        title_width = bmf_measure_text(font, size, control->text);
    }

    int title_offset = 8;
    int border_y = abs_y + font_height / 2 + 2;

    if (title_width > 0) {
        gfx_hline(abs_x, border_y, title_offset, theme->frame_dark);
        gfx_hline(abs_x + title_offset + title_width + 8, border_y,
                  control->w - title_offset - title_width - 8, theme->frame_dark);
    } else {
        gfx_hline(abs_x, border_y, control->w, theme->frame_dark);
    }

    gfx_vline(abs_x, border_y, control->h - (border_y - abs_y), theme->frame_dark);
    gfx_hline(abs_x, abs_y + control->h - 1, control->w, theme->frame_dark); /* Can be changed to light */
    gfx_vline(abs_x + control->w - 1, border_y, control->h - (border_y - abs_y), theme->frame_dark); /* Can be changed to light */

    if (control->text[0] && font->data) {
        int text_x = abs_x + title_offset + 4;
        int text_y = abs_y + 5;

        bmf_printf(text_x, text_y, font, size, control->fg, "%s", control->text);
    }
}

void ctrl_draw_scrollbar(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    
    /* checked is orientation here: 0=vertical, 1=horizontal. */
    int vertical = !control->scrollbar.checked;
    int arrow_size = vertical ? control->w : control->h;
    int track_len = vertical ? (control->h - 2 * arrow_size) : (control->w - 2 * arrow_size);
    if (track_len < 0) track_len = 0;
    
    int thumb_size = 20;
    if (thumb_size > track_len) thumb_size = track_len;
    int max_val = control->scrollbar.max_length > 0 ? control->scrollbar.max_length : 100;
    int thumb_pos = 0;
    if (max_val > 0 && track_len > thumb_size) {
        thumb_pos = ((track_len - thumb_size) * control->scrollbar.cursor_pos) / max_val;
    }
    
    if (vertical) {
        int up_pressed = (control->scrollbar.hovered_item == 0 && control->scrollbar.pressed);
        gfx_fillrect(abs_x, abs_y, control->w, arrow_size, theme->button_color);
        gfx_rect(abs_x, abs_y, control->w, arrow_size, COLOR_BLACK);
        if (up_pressed) {
            gfx_hline(abs_x + 1, abs_y + 1, control->w - 2, theme->frame_dark);
            gfx_vline(abs_x + 1, abs_y + 1, arrow_size - 2, theme->frame_dark);
        } else {
            gfx_hline(abs_x + 1, abs_y + 1, control->w - 2, COLOR_WHITE);
            gfx_vline(abs_x + 1, abs_y + 1, arrow_size - 2, COLOR_WHITE);
            gfx_hline(abs_x + 1, abs_y + arrow_size - 2, control->w - 2, theme->frame_dark);
            gfx_vline(abs_x + control->w - 2, abs_y + 1, arrow_size - 2, theme->frame_dark);
        }
        
        int arrow_cx = abs_x + control->w / 2;
        int arrow_cy = abs_y + arrow_size / 2;
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_cx - 3 + i, arrow_cy - i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx - 1, arrow_cy + 1, 3, 3, COLOR_BLACK);
        
        int down_y = abs_y + control->h - arrow_size;
        int down_pressed = (control->scrollbar.hovered_item == 2 && control->scrollbar.pressed);
        gfx_fillrect(abs_x, down_y, control->w, arrow_size, theme->button_color);
        gfx_rect(abs_x, down_y, control->w, arrow_size, COLOR_BLACK);
        if (down_pressed) {
            gfx_hline(abs_x + 1, down_y + 1, control->w - 2, theme->frame_dark);
            gfx_vline(abs_x + 1, down_y + 1, arrow_size - 2, theme->frame_dark);
        } else {
            gfx_hline(abs_x + 1, down_y + 1, control->w - 2, COLOR_WHITE);
            gfx_vline(abs_x + 1, down_y + 1, arrow_size - 2, COLOR_WHITE);
            gfx_hline(abs_x + 1, down_y + arrow_size - 2, control->w - 2, theme->frame_dark);
            gfx_vline(abs_x + control->w - 2, down_y + 1, arrow_size - 2, theme->frame_dark);
        }
        
        arrow_cy = down_y + arrow_size / 2;
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_cx - 3 + i, arrow_cy + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx - 1, arrow_cy - 3, 3, 3, COLOR_BLACK);
        
        int track_y = abs_y + arrow_size;
        for (int py = 0; py < track_len; py++) {
            for (int px = 0; px < control->w; px++) {
                uint8_t color = ((px + py) & 1) ? COLOR_WHITE : 7;
                gfx_putpixel(abs_x + px, track_y + py, color);
            }
        }
        
        int thumb_y = track_y + thumb_pos;
        int thumb_pressed = (control->scrollbar.hovered_item == 1 && control->scrollbar.pressed);
        gfx_fillrect(abs_x, thumb_y, control->w, thumb_size, theme->button_color);
        gfx_rect(abs_x, thumb_y, control->w, thumb_size, COLOR_BLACK);
        if (thumb_pressed) {
            gfx_hline(abs_x + 1, thumb_y + 1, control->w - 2, theme->frame_dark);
            gfx_vline(abs_x + 1, thumb_y + 1, thumb_size - 2, theme->frame_dark);
        } else {
            gfx_hline(abs_x + 1, thumb_y + 1, control->w - 2, COLOR_WHITE);
            gfx_vline(abs_x + 1, thumb_y + 1, thumb_size - 2, COLOR_WHITE);
            gfx_hline(abs_x + 1, thumb_y + thumb_size - 2, control->w - 2, theme->frame_dark);
            gfx_vline(abs_x + control->w - 2, thumb_y + 1, thumb_size - 2, theme->frame_dark);
        }
        
    } else {
        int left_pressed = (control->scrollbar.hovered_item == 0 && control->scrollbar.pressed);
        gfx_fillrect(abs_x, abs_y, arrow_size, control->h, theme->button_color);
        gfx_rect(abs_x, abs_y, arrow_size, control->h, COLOR_BLACK);
        if (left_pressed) {
            gfx_hline(abs_x + 1, abs_y + 1, arrow_size - 2, theme->frame_dark);
            gfx_vline(abs_x + 1, abs_y + 1, control->h - 2, theme->frame_dark);
        } else {
            gfx_hline(abs_x + 1, abs_y + 1, arrow_size - 2, COLOR_WHITE);
            gfx_vline(abs_x + 1, abs_y + 1, control->h - 2, COLOR_WHITE);
            gfx_hline(abs_x + 1, abs_y + control->h - 2, arrow_size - 2, theme->frame_dark);
            gfx_vline(abs_x + arrow_size - 2, abs_y + 1, control->h - 2, theme->frame_dark);
        }
        
        int arrow_cx = abs_x + arrow_size / 2;
        int arrow_cy = abs_y + control->h / 2;
        for (int i = 0; i < 4; i++) {
            gfx_vline(arrow_cx - i, arrow_cy - 3 + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx + 1, arrow_cy - 1, 3, 3, COLOR_BLACK);
        
        int right_x = abs_x + control->w - arrow_size;
        int right_pressed = (control->scrollbar.hovered_item == 2 && control->scrollbar.pressed);
        gfx_fillrect(right_x, abs_y, arrow_size, control->h, theme->button_color);
        gfx_rect(right_x, abs_y, arrow_size, control->h, COLOR_BLACK);
        if (right_pressed) {
            gfx_hline(right_x + 1, abs_y + 1, arrow_size - 2, theme->frame_dark);
            gfx_vline(right_x + 1, abs_y + 1, control->h - 2, theme->frame_dark);
        } else {
            gfx_hline(right_x + 1, abs_y + 1, arrow_size - 2, COLOR_WHITE);
            gfx_vline(right_x + 1, abs_y + 1, control->h - 2, COLOR_WHITE);
            gfx_hline(right_x + 1, abs_y + control->h - 2, arrow_size - 2, theme->frame_dark);
            gfx_vline(right_x + arrow_size - 2, abs_y + 1, control->h - 2, theme->frame_dark);
        }
        
        arrow_cx = right_x + arrow_size / 2;
        for (int i = 0; i < 4; i++) {
            gfx_vline(arrow_cx + i, arrow_cy - 3 + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx - 3, arrow_cy - 1, 3, 3, COLOR_BLACK);
        
        int track_x = abs_x + arrow_size;
        for (int py = 0; py < control->h; py++) {
            for (int px = 0; px < track_len; px++) {
                uint8_t color = ((px + py) & 1) ? COLOR_WHITE : 7;
                gfx_putpixel(track_x + px, abs_y + py, color);
            }
        }
        
        int thumb_x = track_x + thumb_pos;
        int thumb_pressed = (control->scrollbar.hovered_item == 1 && control->scrollbar.pressed);
        gfx_fillrect(thumb_x, abs_y, thumb_size, control->h, theme->button_color);
        gfx_rect(thumb_x, abs_y, thumb_size, control->h, COLOR_BLACK);
        if (thumb_pressed) {
            gfx_hline(thumb_x + 1, abs_y + 1, thumb_size - 2, theme->frame_dark);
            gfx_vline(thumb_x + 1, abs_y + 1, control->h - 2, theme->frame_dark);
        } else {
            gfx_hline(thumb_x + 1, abs_y + 1, thumb_size - 2, COLOR_WHITE);
            gfx_vline(thumb_x + 1, abs_y + 1, control->h - 2, COLOR_WHITE);
            gfx_hline(thumb_x + 1, abs_y + control->h - 2, thumb_size - 2, theme->frame_dark);
            gfx_vline(thumb_x + thumb_size - 2, abs_y + 1, control->h - 2, theme->frame_dark);
        }
    }
}
