#include "internal.h"

void ctrl_draw_icon(gui_control_t *control, int abs_x, int abs_y, uint8_t win_bg) {
    window_theme_t *theme = theme_get_current();

    int icon_size = 32;
    int label_max_w = control->w > 0 ? control->w : 48;
    int total_w = label_max_w;
    int max_line_width = label_max_w - 2;

    int label_lines = icon_count_label_lines(control->text, max_line_width);
    int total_h = icon_calc_total_height(icon_size, label_lines);

    int bg_width = total_w + 2;
    int bg_height = total_h + 2;
    int bg_start_x = abs_x - 1;
    int bg_start_y = abs_y - 1;
    int row_bytes = (bg_width + 1) / 2;

    if (!control->icon.saved_bg) {
        control->icon.saved_bg = (uint8_t*)kmalloc(row_bytes * bg_height);
        if (control->icon.saved_bg) {
            if (bg_start_x >= 0 && bg_start_y >= 0 &&
                bg_start_x + bg_width <= WM_SCREEN_WIDTH &&
                bg_start_y + bg_height <= WM_SCREEN_HEIGHT && (bg_start_x & 1) == 0) {
                gfx_read_screen_region_packed(control->icon.saved_bg, bg_width, bg_height, bg_start_x, bg_start_y);
            } else {
                for (int y = 0; y < bg_height; y++) {
                    uint8_t *dst_row = control->icon.saved_bg + y * row_bytes;
                    for (int b = 0; b < row_bytes; b++) dst_row[b] = 0;
                    for (int x = 0; x < bg_width; x++) {
                        if (bg_start_y + y >= 0 && bg_start_y + y < WM_SCREEN_HEIGHT &&
                            bg_start_x + x >= 0 && bg_start_x + x < WM_SCREEN_WIDTH) {
                            uint8_t pix = gfx_getpixel(bg_start_x + x, bg_start_y + y);
                            int byte_idx = x / 2;
                            if (x & 1) dst_row[byte_idx] = (dst_row[byte_idx] & 0xF0) | (pix & 0x0F);
                            else dst_row[byte_idx] = (dst_row[byte_idx] & 0x0F) | (pix << 4);
                        }
                    }
                }
            }
        }
    } else {
        if (bg_start_x >= 0 && bg_start_y >= 0 &&
            bg_start_x + bg_width <= WM_SCREEN_WIDTH &&
            bg_start_y + bg_height <= WM_SCREEN_HEIGHT && (bg_start_x & 1) == 0) {
            gfx_write_screen_region_packed(control->icon.saved_bg, bg_width, bg_height, bg_start_x, bg_start_y);
        } else {
            for (int y = 0; y < bg_height; y++) {
                uint8_t *src_row = control->icon.saved_bg + y * row_bytes;
                for (int x = 0; x < bg_width; x++) {
                    int sx = bg_start_x + x;
                    int sy = bg_start_y + y;
                    if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                        int byte_idx = x / 2;
                        uint8_t packed = src_row[byte_idx];
                        uint8_t pix = (x & 1) ? (packed & 0x0F) : (packed >> 4);
                        gfx_putpixel(sx, sy, pix);
                    }
                }
            }
        }
    }

    (void)win_bg;

    int icon_x = abs_x + (total_w - icon_size) / 2;
    int icon_y = abs_y;

    if (control->icon.cached_bitmap_orig) {
        bitmap_draw(control->icon.cached_bitmap_orig, icon_x, icon_y);
    } else {
        gfx_fillrect(icon_x, icon_y, icon_size, icon_size, theme->button_color);
        gfx_rect(icon_x, icon_y, icon_size, icon_size, theme->frame_dark);

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

    if (control->icon.checked) {
        for (int py = 0; py < total_h; py++) {
            for (int px = 0; px < total_w; px++) {
                if ((px + py) % 2 == 0) {
                    gfx_putpixel(abs_x + px, abs_y + py, 1);
                }
            }
        }
    }

    if (control->text[0]) {
        uint8_t text_color = control->icon.checked ? 15 : theme->icon_text_color;
        int text_y = abs_y + icon_size + 4;
        icon_draw_label_wrapped(control->text, abs_x, text_y, total_w, max_line_width, text_color);
    }
}
