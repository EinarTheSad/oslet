#include "controls.h"
#include "theme.h"
#include "bitmap.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"

// External font references from window.c
extern bmf_font_t font_b, font_n, font_i, font_bi;

void ctrl_draw_button(gui_control_t *control, int abs_x, int abs_y) {
    extern void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label, int pressed);

    if (control->pressed) {
        win_draw_button(abs_x, abs_y, control->w, control->h, control->bg, control->text, 1);
    } else {
        win_draw_button(abs_x, abs_y, control->w, control->h, control->bg, control->text, 0);
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

        /* Draw background */
        gfx_fillrect(abs_x, abs_y, label_w, label_h, control->bg);

        /* Draw border if enabled */
        if (control->border) {
            gfx_rect(abs_x, abs_y, label_w, label_h, control->border_color);
        }

        /* Draw text - convert \n to real newlines */
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
        int text_y = abs_y + 2;
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
}

// Helper function: split text by \n sequences
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

// Helper function: measure text height with \n sequences
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
