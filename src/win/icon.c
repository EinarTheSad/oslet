#include "icon.h"
#include "bitmap.h"
#include "wm_config.h"
#include "theme.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"
#include "../mem/heap.h"
#include "../console.h"

#define ICON_BG_MARGIN 4

extern bmf_font_t font_b, font_n;

typedef struct {
    int x, y, total_width;
    uint8_t color;
} label_draw_params_t;

static int label_wordwrap_process(const char *label, int max_line_width,
                                  label_draw_params_t *draw) {
    if (!label || !label[0] || !font_n.data) return 1;

    char line[64];
    char word[32];
    int line_idx = 0;
    int line_count = 0;
    int text_y = draw ? draw->y : 0;
    int i = 0;

    while (label[i]) {
        /* Extract next word */
        int word_idx = 0;
        while (label[i] && label[i] != ' ' && word_idx < 31) {
            word[word_idx++] = label[i++];
        }
        word[word_idx] = '\0';

        /* Try adding word to current line */
        char test_line[64];
        if (line_idx > 0) {
            int k = 0;
            for (int j = 0; j < line_idx; j++) test_line[k++] = line[j];
            test_line[k++] = ' ';
            for (int j = 0; word[j]; j++) test_line[k++] = word[j];
            test_line[k] = '\0';
        } else {
            int k = 0;
            for (int j = 0; word[j]; j++) test_line[k++] = word[j];
            test_line[k] = '\0';
        }

        int test_w = bmf_measure_text(&font_n, 10, test_line);

        if (test_w > max_line_width && line_idx > 0) {
            /* Word doesn't fit - finish current line */
            line[line_idx] = '\0';
            line_count++;

            if (draw) {
                int line_w = bmf_measure_text(&font_n, 10, line);
                int line_x = draw->x + (draw->total_width - line_w) / 2;
                bmf_printf(line_x, text_y, &font_n, 10, draw->color, "%s", line);
                text_y += 11;
            }

            /* Start new line with current word */
            line_idx = 0;
            for (int j = 0; word[j]; j++) {
                line[line_idx++] = word[j];
            }
        } else {
            /* Word fits - add it to line */
            if (line_idx > 0) {
                line[line_idx++] = ' ';
            }
            for (int j = 0; word[j]; j++) {
                if (line_idx < 63) line[line_idx++] = word[j];
            }
        }

        /* Skip spaces */
        while (label[i] == ' ') i++;
    }

    /* Handle remaining text */
    if (line_idx > 0) {
        line[line_idx] = '\0';
        line_count++;

        if (draw) {
            int final_w = bmf_measure_text(&font_n, 10, line);
            int final_x = draw->x + (draw->total_width - final_w) / 2;
            bmf_printf(final_x, text_y, &font_n, 10, draw->color, "%s", line);
        }
    }

    return line_count > 0 ? line_count : 1;
}

int icon_count_label_lines(const char *label, int max_line_width) {
    return label_wordwrap_process(label, max_line_width, NULL);
}

int icon_calc_total_height(int icon_size, int label_lines) {
    return icon_size + 5 + label_lines * 11 + 2;
}

void icon_draw_label_wrapped(const char *label, int x, int y, int total_width,
                             int max_line_width, uint8_t color) {
    label_draw_params_t params = { x, y, total_width, color };
    label_wordwrap_process(label, max_line_width, &params);
}

static int icon_calc_height(const char *label) {
    int lines = icon_count_label_lines(label, 49);
    return icon_calc_total_height(WM_ICON_SIZE, lines);
}

void icon_create(icon_t *icon, int x, int y, const char *label, const char *bitmap_path) {
    icon->x = x;
    icon->y = y;
    icon->bitmap = NULL;
    icon->saved_bg = NULL;
    icon->selected = 0;
    icon->user_data = NULL;
    icon->dragging = 0;
    icon->drag_offset_x = 0;
    icon->drag_offset_y = 0;
    icon->original_x = x;
    icon->original_y = y;
    icon->click_start_x = 0;
    icon->click_start_y = 0;

    if (label) {
        strcpy_s(icon->label, label, 64);
    } else {
        icon->label[0] = '\0';
    }

    if (bitmap_path) {
        strcpy_s(icon->bitmap_path, bitmap_path, 64);
    } else {
        icon->bitmap_path[0] = '\0';
    }

    icon->height = icon_calc_height(icon->label);
}

void icon_draw(icon_t *icon) {
    if (!icon) return;

    int total_width = WM_ICON_TOTAL_WIDTH;
    int total_height = icon->height;

    int bg_width = total_width + ICON_BG_MARGIN * 2;
    int bg_height = total_height + ICON_BG_MARGIN * 2;
    int bg_start_x = icon->x - ICON_BG_MARGIN;
    int bg_start_y = icon->y - ICON_BG_MARGIN;

    /* Save background before first drawing */
    if (!icon->saved_bg) {
        icon->saved_bg = kmalloc(bg_width * bg_height);
        if (icon->saved_bg) {
            for (int y = 0; y < bg_height; y++) {
                for (int x = 0; x < bg_width; x++) {
                    icon->saved_bg[y * bg_width + x] = gfx_getpixel(bg_start_x + x, bg_start_y + y);
                }
            }
        }
    } else {
        /* Restore original background before redrawing */
        for (int y = 0; y < bg_height; y++) {
            for (int x = 0; x < bg_width; x++) {
                gfx_putpixel(bg_start_x + x, bg_start_y + y, icon->saved_bg[y * bg_width + x]);
            }
        }
    }

    /* Calculate centered icon position */
    int icon_draw_x = icon->x + WM_ICON_CENTER_OFFSET;
    int icon_width = WM_ICON_SIZE;

    /* Load bitmap if needed */
    if (icon->bitmap_path[0] && !icon->bitmap) {
        icon->bitmap = bitmap_load_from_file(icon->bitmap_path);
    }

    /* Draw icon bitmap or default icon */
    if (icon->bitmap) {
        /* Recalculate icon_draw_x to center the bitmap within the icon area */
        icon_draw_x = icon->x + (WM_ICON_TOTAL_WIDTH - icon_width) / 2;
        bitmap_draw(icon->bitmap, icon_draw_x, icon->y + 1);
    } else {
        /* Draw default icon with label initials */
        window_theme_t *theme = theme_get_current();
        gfx_fillrect(icon_draw_x, icon->y, WM_ICON_SIZE, WM_ICON_SIZE, theme->button_color);
        gfx_rect(icon_draw_x, icon->y, WM_ICON_SIZE, WM_ICON_SIZE, theme->frame_dark);

        if (font_b.data && icon->label[0]) {
            char icon_label[3];
            icon_label[0] = icon->label[0];
            icon_label[1] = icon->label[1] ? icon->label[1] : '\0';
            icon_label[2] = '\0';

            int tw = bmf_measure_text(&font_b, 12, icon_label);
            int tx = icon_draw_x + (WM_ICON_SIZE - tw) / 2;
            int ty = icon->y + 10;

            bmf_printf(tx, ty, &font_b, 12, theme->text_color, "%s", icon_label);
        }
    }

    /* Draw selection */
    if (icon->selected) {
        /* 1x1 dither pattern */
        for (int py = 0; py < total_height; py++) {
            for (int px = 0; px < total_width; px++) {
                if ((px + py) % 2 == 0) {
                    gfx_putpixel(icon->x + px, icon->y + py, 1);
                }
            }
        }
    }

    /* Draw label below icon with word wrapping */
    if (icon->label[0]) {
        window_theme_t *theme = theme_get_current();
        uint8_t text_color = icon->selected ? 15 : theme->text_color;
        int text_y = icon->y + WM_ICON_SIZE + 5;
        icon_draw_label_wrapped(icon->label, icon->x, text_y, total_width, 49, text_color);
    }
}

void icon_hide(icon_t *icon) {
    if (!icon) return;

    /* Restore background (with margin) */
    if (icon->saved_bg) {
        int total_width = WM_ICON_TOTAL_WIDTH;
        int total_height = icon->height;
        int bg_width = total_width + ICON_BG_MARGIN * 2;
        int bg_height = total_height + ICON_BG_MARGIN * 2;
        int bg_start_x = icon->x - ICON_BG_MARGIN;
        int bg_start_y = icon->y - ICON_BG_MARGIN;

        for (int y = 0; y < bg_height; y++) {
            for (int x = 0; x < bg_width; x++) {
                gfx_putpixel(bg_start_x + x, bg_start_y + y, icon->saved_bg[y * bg_width + x]);
            }
        }

        kfree(icon->saved_bg);
        icon->saved_bg = NULL;
    }
}

void icon_destroy(icon_t *icon) {
    if (!icon) return;

    icon_hide(icon);

    if (icon->bitmap) {
        bitmap_free(icon->bitmap);
        icon->bitmap = NULL;
    }
}

int icon_is_clicked(icon_t *icon, int mx, int my) {
    if (!icon) return 0;

    int total_width = WM_ICON_TOTAL_WIDTH;
    int total_height = icon->height;

    if (mx >= icon->x && mx < icon->x + total_width &&
        my >= icon->y && my < icon->y + total_height) {
        return 1;
    }
    return 0;
}

void icon_set_selected(icon_t *icon, int selected) {
    if (!icon) return;
    icon->selected = selected;
}

void icon_move(icon_t *icon, int new_x, int new_y) {
    if (!icon) return;

    icon_hide(icon);

    icon->x = new_x;
    icon->y = new_y;
}

void icon_set_label(icon_t *icon, const char *new_label) {
    if (!icon) return;

    if (new_label) {
        strcpy_s(icon->label, new_label, 64);
    } else {
        icon->label[0] = '\0';
    }

    int old_height = icon->height;
    icon->height = icon_calc_height(icon->label);

    if (icon->height != old_height && icon->saved_bg) {
        kfree(icon->saved_bg);
        icon->saved_bg = NULL;
    }
}

void icon_invalidate_bg(icon_t *icon) {
    if (!icon) return;

    if (icon->saved_bg) {
        kfree(icon->saved_bg);
        icon->saved_bg = NULL;
    }
}

int icon_get_height(icon_t *icon) {
    if (!icon) return WM_ICON_TOTAL_HEIGHT;
    return icon->height;
}

void icon_start_drag(icon_t *icon, int mouse_x, int mouse_y) {
    if (!icon) return;

    icon->dragging = 1;
    icon->drag_offset_x = mouse_x - icon->x;
    icon->drag_offset_y = mouse_y - icon->y;
    icon->original_x = icon->x;
    icon->original_y = icon->y;
}

void icon_update_drag(icon_t *icon, int mouse_x, int mouse_y) {
    if (!icon || !icon->dragging) return;

    int new_x = mouse_x - icon->drag_offset_x;
    int new_y = mouse_y - icon->drag_offset_y;

    /* Clamp to screen bounds (leave room for taskbar) */
    if (new_x < 0) new_x = 0;
    if (new_y < 0) new_y = 0;
    if (new_x > 640 - WM_ICON_TOTAL_WIDTH) new_x = 640 - WM_ICON_TOTAL_WIDTH;
    if (new_y > 480 - 27 - icon->height) new_y = 480 - 27 - icon->height;

    if (new_x != icon->x || new_y != icon->y) {
        icon_move(icon, new_x, new_y);
    }
}

void icon_end_drag(icon_t *icon, int new_x, int new_y) {
    if (!icon) return;

    icon->dragging = 0;
    icon_move(icon, new_x, new_y);
    /* Update original position to new snapped position */
    icon->original_x = new_x;
    icon->original_y = new_y;
}

void icon_cancel_drag(icon_t *icon) {
    if (!icon) return;

    icon->dragging = 0;
    /* Move back to original position */
    icon_move(icon, icon->original_x, icon->original_y);
}
