#include "internal.h"

void ctrl_draw_icon(gui_control_t *control, int abs_x, int abs_y, uint8_t win_bg) {
    window_theme_t *theme = theme_get_current();
    icon_geometry_t geometry;
    int label_x;
    int label_total_w;
    int max_line_width;

    icon_get_geometry(control, abs_x, abs_y, &geometry);
    label_x = geometry.x;
    label_total_w = geometry.w;
    max_line_width = geometry.max_line_width;

    int icon_x = geometry.icon_x;
    int icon_y = geometry.icon_y;

    if (control->icon.cached_bitmap_orig) {
        bitmap_draw(control->icon.cached_bitmap_orig, icon_x, icon_y);
    } else {
        gfx_fillrect(icon_x, icon_y, WM_ICON_SIZE, WM_ICON_SIZE, theme->button_color);
        gfx_rect(icon_x, icon_y, WM_ICON_SIZE, WM_ICON_SIZE, theme->frame_dark);

        if (font_b.data && control->text[0]) {
            char initials[3];
            initials[0] = control->text[0];
            initials[1] = control->text[1] ? control->text[1] : '\0';
            initials[2] = '\0';

            int tw = bmf_measure_text(&font_b, 12, initials);
            int tx = icon_x + (WM_ICON_SIZE - tw) / 2;
            int ty = icon_y + 10;
            bmf_printf(tx, ty, &font_b, 12, theme->text_color, "%s", initials);
        }
    }

    if (control->icon.checked) {
        for (int py = 0; py < geometry.h; py++) {
            for (int px = 0; px < label_total_w; px++) {
                if ((px + py) % 2 == 0) {
                    gfx_putpixel(label_x + px, abs_y + py, 1);
                }
            }
        }
    }

    (void)win_bg;

    if (control->text[0]) {
        uint8_t text_color;
        if (control->icon.checked) {
            text_color = 15;
        } else if (control->icon.use_desktop_text_color) {
            text_color = theme->desktop_icon_text_color;
        } else {
            text_color = (control->fg >= 0) ? (uint8_t)control->fg : theme->icon_text_color;
        }
        int text_y = abs_y + WM_ICON_SIZE + 4;
        if (control->h > 0) {
            icon_draw_label_wrapped_limit(control->text, label_x, text_y,
                                          label_total_w, max_line_width,
                                          text_color, 2);
        } else {
            icon_draw_label_wrapped(control->text, label_x, text_y,
                                    label_total_w, max_line_width, text_color);
        }
    }
}
