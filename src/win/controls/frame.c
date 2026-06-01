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
    gfx_hline(abs_x, abs_y + control->h - 1, control->w, theme->frame_dark);
    gfx_vline(abs_x + control->w - 1, border_y, control->h - (border_y - abs_y), theme->frame_dark);

    if (control->text[0] && font->data) {
        int text_x = abs_x + title_offset + 4;
        int text_y = abs_y + 5;

        bmf_printf(text_x, text_y, font, size, control->fg, "%s", control->text);
    }
}
