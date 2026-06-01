#include "internal.h"

static int scrollbar_is_vertical(gui_control_t *control) {
    if (control->w > control->h) return 0;
    if (control->h > control->w) return 1;
    return control->scrollbar.checked ? 0 : 1;
}

void ctrl_draw_scrollbar(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    
    /* checked is kept for compatibility; shape decides orientation first. */
    int vertical = scrollbar_is_vertical(control);
    int arrow_size = vertical ? control->w : control->h;
    int length = vertical ? control->h : control->w;
    if (length < 1) return;
    if (arrow_size < 1) arrow_size = 1;
    if (arrow_size * 2 >= length) {
        arrow_size = length / 2;
        if (arrow_size < 1) arrow_size = 1;
    }

    int track_len = vertical ? (control->h - 2 * arrow_size) : (control->w - 2 * arrow_size);
    if (track_len < 1) track_len = 1;
    
    int thumb_size = 20;
    if (thumb_size > track_len) thumb_size = track_len;
    if (thumb_size < 1) thumb_size = 1;

    int max_val = control->scrollbar.max_length > 0 ? control->scrollbar.max_length : 100;
    int value = control->scrollbar.cursor_pos;
    if (value < 0) value = 0;
    if (value > max_val) value = max_val;

    int travel = track_len - thumb_size;
    if (travel < 0) travel = 0;

    int thumb_pos = 0;
    if (max_val > 0 && travel > 0) {
        thumb_pos = (travel * value) / max_val;
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
        
        int down_y = abs_y + length - arrow_size;
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
        
        int right_x = abs_x + length - arrow_size;
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
