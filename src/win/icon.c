#include "icon.h"
#include "bitmap.h"
#include "wm_config.h"
#include "theme.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"
#include "../mem/heap.h"
#include "../console.h"

extern bmf_font_t font_b, font_n;

void icon_create(icon_t *icon, int x, int y, const char *label, const char *bitmap_path) {
    icon->x = x;
    icon->y = y;
    icon->bitmap = NULL;
    icon->saved_bg = NULL;
    icon->selected = 0;
    icon->user_data = NULL;

    /* Copy label */
    if (label) {
        strcpy_s(icon->label, label, 64);
    } else {
        icon->label[0] = '\0';
    }

    /* Copy bitmap path */
    if (bitmap_path) {
        strcpy_s(icon->bitmap_path, bitmap_path, 64);
    } else {
        icon->bitmap_path[0] = '\0';
    }
}

void icon_draw(icon_t *icon) {
    if (!icon) return;

    int total_width = WM_ICON_TOTAL_WIDTH;
    int total_height = WM_ICON_TOTAL_HEIGHT;

    /* Save background before first drawing */
    if (!icon->saved_bg) {
        icon->saved_bg = kmalloc(total_width * total_height);
        if (icon->saved_bg) {
            for (int y = 0; y < total_height; y++) {
                for (int x = 0; x < total_width; x++) {
                    icon->saved_bg[y * total_width + x] = gfx_getpixel(icon->x + x, icon->y + y);
                }
            }
        }
    } else {
        /* Restore original background before redrawing */
        for (int y = 0; y < total_height; y++) {
            for (int x = 0; x < total_width; x++) {
                gfx_putpixel(icon->x + x, icon->y + y, icon->saved_bg[y * total_width + x]);
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
    if (font_n.data && icon->label[0]) {
        window_theme_t *theme = theme_get_current();
        const int MAX_LINE_WIDTH = 49;
        char line[64];
        char word[32];
        int line_idx = 0;
        int text_y = icon->y + WM_ICON_SIZE + 5;
        int i = 0;

        /* Choose text color based on selection state */
        uint8_t text_color = icon->selected ? 15 : theme->text_color; /* White if selected */

        while (icon->label[i]) {
            /* Extract next word */
            int word_idx = 0;
            while (icon->label[i] && icon->label[i] != ' ' && word_idx < 31) {
                word[word_idx++] = icon->label[i++];
            }
            word[word_idx] = '\0';

            /* Try adding word to current line */
            char test_line[64];
            if (line_idx > 0) {
                /* Add space before word if line not empty */
                int k = 0;
                for (int j = 0; j < line_idx; j++) test_line[k++] = line[j];
                test_line[k++] = ' ';
                for (int j = 0; word[j]; j++) test_line[k++] = word[j];
                test_line[k] = '\0';
            } else {
                /* First word on line */
                int k = 0;
                for (int j = 0; word[j]; j++) test_line[k++] = word[j];
                test_line[k] = '\0';
            }

            int test_w = bmf_measure_text(&font_n, 10, test_line);

            if (test_w > MAX_LINE_WIDTH && line_idx > 0) {
                /* Word doesn't fit - draw current line centered and start new */
                line[line_idx] = '\0';
                int line_w = bmf_measure_text(&font_n, 10, line);
                int line_x = icon->x + (total_width - line_w) / 2;
                bmf_printf(line_x, text_y, &font_n, 10, text_color, "%s", line);
                text_y += 11;

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
            while (icon->label[i] == ' ') i++;
        }

        /* Draw remaining text centered */
        if (line_idx > 0) {
            line[line_idx] = '\0';
            int final_w = bmf_measure_text(&font_n, 10, line);
            int final_x = icon->x + (total_width - final_w) / 2;
            bmf_printf(final_x, text_y, &font_n, 10, text_color, "%s", line);
        }
    }
}

void icon_hide(icon_t *icon) {
    if (!icon) return;

    /* Restore background */
    if (icon->saved_bg) {
        int total_width = WM_ICON_TOTAL_WIDTH;
        int total_height = WM_ICON_TOTAL_HEIGHT;

        for (int y = 0; y < total_height; y++) {
            for (int x = 0; x < total_width; x++) {
                gfx_putpixel(icon->x + x, icon->y + y, icon->saved_bg[y * total_width + x]);
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

    /* Click area includes both icon and label */
    int total_width = WM_ICON_TOTAL_WIDTH;
    int total_height = WM_ICON_TOTAL_HEIGHT;

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

    /* Restore old background */
    if (icon->saved_bg) {
        int total_width = WM_ICON_TOTAL_WIDTH;
        int total_height = WM_ICON_TOTAL_HEIGHT;

        for (int y = 0; y < total_height; y++) {
            for (int x = 0; x < total_width; x++) {
                gfx_putpixel(icon->x + x, icon->y + y, icon->saved_bg[y * total_width + x]);
            }
        }

        kfree(icon->saved_bg);
        icon->saved_bg = NULL;
    }

    /* Update position */
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
}
