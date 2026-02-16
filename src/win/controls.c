#include "controls.h"
#include "theme.h"
#include "bitmap.h"
#include "window.h"
#include "icon.h"
#include "../drivers/graphics.h"
#include "../drivers/mouse.h"
#include "../fonts/bmf.h"
#include "../mem/heap.h"
#include "../rtc.h"

extern bmf_font_t font_b, font_n, font_i, font_bi;

/* Sin/cos lookup table (values * 1000) for 0-59 (6 degree steps) */
static const int sin_table[60] = {
       0,  105,  208,  309,  407,  500,  588,  669,  743,  809,
     866,  914,  951,  978,  995, 1000,  995,  978,  951,  914,
     866,  809,  743,  669,  588,  500,  407,  309,  208,  105,
       0, -105, -208, -309, -407, -500, -588, -669, -743, -809,
    -866, -914, -951, -978, -995,-1000, -995, -978, -951, -914,
    -866, -809, -743, -669, -588, -500, -407, -309, -208, -105
};

static inline int clock_sin(int index) {
    while (index < 0) index += 60;
    return sin_table[index % 60];
}

static inline int clock_cos(int index) {
    return clock_sin(index + 15);
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

/* Midpoint circle algorithm */
static void draw_clock_circle(int cx, int cy, int r, uint8_t color) {
    if (r < 0) return;
    int x = r;
    int y = 0;
    int err = 0;
    while (x >= y) {
        gfx_putpixel(cx + x, cy + y, color);
        gfx_putpixel(cx - x, cy + y, color);
        gfx_putpixel(cx + x, cy - y, color);
        gfx_putpixel(cx - x, cy - y, color);
        gfx_putpixel(cx + y, cy + x, color);
        gfx_putpixel(cx - y, cy + x, color);
        gfx_putpixel(cx + y, cy - x, color);
        gfx_putpixel(cx - y, cy - x, color);
        if (err <= 0) {
            y += 1; err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1; err -= 2*x + 1;
        }
    }
}

/* Bresenham line */
static void draw_clock_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        gfx_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void draw_clock_hand(int cx, int cy, int length, int angle_idx, uint8_t color) {
    int sin_val = clock_sin(angle_idx);
    int cos_val = clock_cos(angle_idx);
    int ex = cx + (length * sin_val) / 1000;
    int ey = cy - (length * cos_val) / 1000;
    draw_clock_line(cx, cy, ex, ey, color);
}

static void draw_clock_face(int cx, int cy, int r, window_theme_t *theme) {
    /* Draw clock outline */
    draw_clock_circle(cx, cy, r, theme->text_color);
    if (r > 2) draw_clock_circle(cx, cy, r - 2, theme->frame_dark);

    /* Draw hour markers */
    for (int i = 0; i < 12; i++) {
        int angle_idx = i * 5;
        int sin_val = clock_sin(angle_idx);
        int cos_val = clock_cos(angle_idx);
        int inner_r = r - 10;
        int outer_r = r - 5;
        int ix = cx + (inner_r * sin_val) / 1000;
        int iy = cy - (inner_r * cos_val) / 1000;
        int ox = cx + (outer_r * sin_val) / 1000;
        int oy = cy - (outer_r * cos_val) / 1000;
        draw_clock_line(ix, iy, ox, oy, theme->text_color);
    }
}

void ctrl_draw_clock(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    uint8_t bg_color = (control->bg == -1) ? theme->bg_color : control->bg;
    
    /* Fill background */
    gfx_fillrect(abs_x, abs_y, control->w, control->h, bg_color);
    
    /* Calculate center and radius based on control size */
    int cx = abs_x + control->w / 2;
    int cy = abs_y + control->h / 2;
    int r = (control->w < control->h ? control->w : control->h) / 2 - 5;
    if (r < 10) r = 10;
    
    /* Draw clock face */
    draw_clock_face(cx, cy, r, theme);
    
    /* Get current time */
    rtc_time_t time;
    rtc_read_time(&time);
    
    /* Calculate hand positions */
    int hour_pos = ((time.hour % 12) * 5) + (time.minute / 12);
    int min_pos = time.minute;
    int sec_pos = time.second;
    
    /* Draw hands (hour, minute, second) */
    draw_clock_hand(cx, cy, r * 5 / 10, hour_pos, theme->text_color);
    draw_clock_hand(cx, cy, r * 7 / 10, min_pos, theme->text_color);
    draw_clock_hand(cx, cy, r * 8 / 10, sec_pos, COLOR_LIGHT_RED);
    
    /* Draw center dot */
    int dotx = cx - 2;
    int doty = cy - 2;
    for (int dy = 0; dy < 4; dy++) {
        for (int dx = 0; dx < 4; dx++) {
            gfx_putpixel(dotx + dx, doty + dy, COLOR_BLUE);
        }
    }
}

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
    /* Respect bg=-1 as transparent */
    if (control->bg != -1) {
        gfx_fillrect(abs_x, abs_y, control->w, control->h, control->bg);
    }

    if (control->text[0]) {
        /* Load original bitmap to cache if not already loaded and not previously failed */
        if (!control->cached_bitmap_orig && !control->load_failed) {
            control->cached_bitmap_orig = bitmap_load_from_file(control->text);
            /* When loading a new image, drop any previous scaled cache */
            if (control->cached_bitmap_scaled) {
                bitmap_free(control->cached_bitmap_scaled);
                control->cached_bitmap_scaled = NULL;
            }

            /* Validate loaded bitmap to avoid processing malformed images */
            if (control->cached_bitmap_orig) {
                bitmap_t *orig = control->cached_bitmap_orig;
                if (orig->width <= 0 || orig->height <= 0 || orig->width > 4096 || orig->height > 4096) {
                    /* Unreasonable dimensions - reject */
                    bitmap_free(control->cached_bitmap_orig);
                    control->cached_bitmap_orig = NULL;
                    control->load_failed = 1;
                }
            } else {
                /* Loading failed - mark to avoid retrying every draw */
                control->load_failed = 1;
            }
        }

        /* Draw from cached bitmap. support optional stretch mode or preserve aspect ratio */
        if (control->cached_bitmap_orig) {
            bitmap_t *orig = control->cached_bitmap_orig;
            int bw = orig->width;
            int bh = orig->height;

            /* Stretch mode: scale image to exactly fill the control (may change aspect) */
            if (control->image_mode == 1) {
                if (!control->cached_bitmap_scaled ||
                    control->cached_bitmap_scaled->width != control->w ||
                    control->cached_bitmap_scaled->height != control->h) {
                    if (control->cached_bitmap_scaled) {
                        bitmap_free(control->cached_bitmap_scaled);
                        control->cached_bitmap_scaled = NULL;
                    }
                    /* Scale to exact control dimensions */
                    control->cached_bitmap_scaled = bitmap_scale_nearest(orig, control->w, control->h);
                }

                if (control->cached_bitmap_scaled) {
                    if (control->bg == -1) bitmap_draw(control->cached_bitmap_scaled, abs_x, abs_y);
                    else bitmap_draw_opaque(control->cached_bitmap_scaled, abs_x, abs_y);
                }

            } else {
                /* Existing behavior: preserve aspect ratio and center */
                /* Exact match: if bitmap size equals control size, draw without scaling */
                if (bw == control->w && bh == control->h) {
                    /* Draw original at control origin with no scaling and clear any scaled cache */
                    if (control->cached_bitmap_scaled) {
                        bitmap_free(control->cached_bitmap_scaled);
                        control->cached_bitmap_scaled = NULL;
                    }
                    /* If bg == -1 treat PictureBox as transparent: use transparent draw path */
                    if (control->bg == -1) bitmap_draw(orig, abs_x, abs_y);
                    else bitmap_draw_opaque(orig, abs_x, abs_y);
                } else if (bw > control->w || bh > control->h) {
                    /* Need to scale down to fit inside control while preserving aspect ratio */
                    int new_w, new_h;
                    if ((int64_t)bw * control->h > (int64_t)bh * control->w) {
                        /* width-limited */
                        new_w = control->w;
                        new_h = (bh * control->w) / bw;
                        if (new_h <= 0) new_h = 1;
                    } else {
                        /* height-limited */
                        new_h = control->h;
                        new_w = (bw * control->h) / bh;
                        if (new_w <= 0) new_w = 1;
                    }

                    /* Create or update scaled cache if size changed */
                    if (!control->cached_bitmap_scaled ||
                        control->cached_bitmap_scaled->width != new_w ||
                        control->cached_bitmap_scaled->height != new_h) {
                        if (control->cached_bitmap_scaled) {
                            bitmap_free(control->cached_bitmap_scaled);
                            control->cached_bitmap_scaled = NULL;
                        }
                        control->cached_bitmap_scaled = bitmap_scale_nearest(orig, new_w, new_h);
                    }

                    if (control->cached_bitmap_scaled) {
                        int dx = abs_x + (control->w - control->cached_bitmap_scaled->width) / 2;
                        int dy = abs_y + (control->h - control->cached_bitmap_scaled->height) / 2;
                        /* Draw scaled bitmap; respect bg==-1 for transparency */
                        if (control->bg == -1) bitmap_draw(control->cached_bitmap_scaled, dx, dy);
                        else bitmap_draw_opaque(control->cached_bitmap_scaled, dx, dy);
                    }
                } else {
                    /* Bitmap smaller - center without scaling */
                    int dx = abs_x + (control->w - bw) / 2;
                    int dy = abs_y + (control->h - bh) / 2;
                    /* Draw original bitmap; respect bg==-1 for transparency */
                    if (control->bg == -1) bitmap_draw(orig, dx, dy);
                    else bitmap_draw_opaque(orig, dx, dy);
                }
            }
        }
    }
    
    if (control->border) {
        gfx_rect(abs_x, abs_y, control->w, control->h, COLOR_BLACK);
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

    // Fill circular background with white
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            if (dx * dx + dy * dy <= radius * radius) {
                gfx_putpixel(center_x + dx, center_y + dy, COLOR_WHITE);
            }
        }
    }

    // Draw outer circle with 3D effect
    gfx_circle(center_x, center_y, radius - 1, theme->frame_dark);

    // Draw selected dot if checked (filled circle)
    if (control->checked) {
        for (int dy = -3; dy <= 3; dy++) {
            for (int dx = -3; dx <= 3; dx++) {
                if (dx * dx + dy * dy <= 9) {
                    gfx_putpixel(center_x + dx, center_y + dy, control->fg);
                }
            }
        }
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
    gfx_hline(abs_x + 1, abs_y + control->h - 1, control->w - 1, theme->frame_light);  // Bottom
    gfx_vline(abs_x + control->w - 1, abs_y, control->h, theme->frame_light);  // Right

    // Inner shadow
    gfx_hline(abs_x + 1, abs_y + 1, control->w - 2, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 1, COLOR_DARK_GRAY);

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
    if (control->cached_bitmap_orig) {
        bitmap_draw(control->cached_bitmap_orig, icon_x, icon_y);
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

    int btn_w = 18;
    int field_w = control->w - btn_w;

    /* Draw text field background */
    gfx_fillrect(abs_x, abs_y, field_w, control->h, COLOR_WHITE);

    /* 3D sunken border for text field */
    gfx_hline(abs_x, abs_y, field_w, theme->frame_dark);
    gfx_vline(abs_x, abs_y, control->h, theme->frame_dark);
    gfx_hline(abs_x+1, abs_y + control->h - 1, field_w, theme->frame_light);

    /* Inner shadow */
    gfx_hline(abs_x + 1, abs_y + 1, field_w - 1, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 1, COLOR_DARK_GRAY);

    /* Draw dropdown button */
    int btn_x = abs_x + field_w;
    gfx_fillrect(btn_x, abs_y, btn_w, control->h, theme->button_color);

    /* Button 3D border */
    gfx_rect(btn_x, abs_y, btn_w, control->h, COLOR_BLACK);
    /* Consider dropdown open as pressed for visual feedback */
    int btn_pressed = control->pressed || control->dropdown_open;
    if (btn_pressed) {
        gfx_hline(btn_x + 1, abs_y + 1, btn_w - 2, theme->frame_dark);
        gfx_vline(btn_x + 1, abs_y + 1, control->h - 2, theme->frame_dark);
        gfx_hline(btn_x + 1, abs_y + control->h - 2, btn_w - 2, COLOR_WHITE);
        gfx_vline(btn_x + btn_w - 2, abs_y + 1, control->h - 2, COLOR_WHITE);
    } else {
        gfx_hline(btn_x + 1, abs_y + 1, btn_w - 2, COLOR_WHITE);
        gfx_vline(btn_x + 1, abs_y + 1, control->h - 2, COLOR_WHITE);
        gfx_hline(btn_x + 1, abs_y + control->h - 2, btn_w - 2, theme->frame_dark);
        gfx_vline(btn_x + btn_w - 2, abs_y + 1, control->h - 2, theme->frame_dark);
    }

    /* Draw arrow in button (flip when dropdown is open) */
    int arrow_x = btn_x + btn_w / 2;
    int arrow_y = abs_y + control->h / 2;
    if (control->dropdown_open) {
        /* Pointing up */
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_x - 3 + i, arrow_y - i, 7 - i * 2, COLOR_BLACK);
        }
        /* Small base to make arrow visually consistent */
        gfx_fillrect(arrow_x - 1, arrow_y + 1, 3, 3, COLOR_BLACK);
    } else {
        /* Pointing down */
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_x - 3 + i, arrow_y + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_x - 1, arrow_y - 3, 3, 3, COLOR_BLACK);
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

    /* Auto-flip: if list extends past screen bottom, render above control */
    if (list_y + list_h > GFX_HEIGHT) {
        list_y = abs_y - list_h;
        /* Ensure list doesn't go above screen top */
        if (list_y < 0) {
            list_y = 0;
            /* Limit list height to available screen space */
            list_h = abs_y;
            if (list_h < item_h) list_h = item_h;  /* Show at least 1 item */
        }
    }

    /* Ensure background for the list is saved so it can be correctly restored when closed.
       This is required because dropdown lists may extend outside their parent window. */
    if (!control->dropdown_saved_bg) {
        control->dropdown_saved_w = control->w;
        control->dropdown_saved_h = list_h;
        control->dropdown_saved_x = abs_x;
        control->dropdown_saved_y = list_y;
        int row_bytes = (control->dropdown_saved_w + 1) / 2;
        control->dropdown_saved_bg = kmalloc(row_bytes * control->dropdown_saved_h);
        if (control->dropdown_saved_bg) {
            gfx_read_screen_region_packed(control->dropdown_saved_bg,
                                          control->dropdown_saved_w,
                                          control->dropdown_saved_h,
                                          control->dropdown_saved_x,
                                          control->dropdown_saved_y);
        }
    }

    /* List background */
    gfx_fillrect(abs_x, list_y, control->w, list_h, COLOR_WHITE);
    gfx_rect(abs_x, list_y, control->w, list_h, theme->frame_dark);

    /* Draw items */
    for (int i = 0; i < item_count; i++) {
        char item_text[64];
        dropdown_get_item(control->text, i, item_text, sizeof(item_text));

        int item_y = list_y + i * item_h;

        /* Highlight selected item in blue, hovered item in gray */
        if (i == control->cursor_pos) {
            gfx_fillrect(abs_x + 1, item_y, control->w - 2, item_h, COLOR_BLUE);
            if (font->data) {
                bmf_printf(abs_x + 4, item_y + 3, font, size, COLOR_WHITE, "%s", item_text);
            }
        } else if (i == control->hovered_item) {
            gfx_fillrect(abs_x + 1, item_y, control->w - 2, item_h, 7);  /* Light gray hover color */
            if (font->data) {
                bmf_printf(abs_x + 4, item_y + 3, font, size, control->fg, "%s", item_text);
            }
        } else {
            if (font->data) {
                bmf_printf(abs_x + 4, item_y + 3, font, size, control->fg, "%s", item_text);
            }
        }
    }
}

/* Restore and free saved dropdown background (integrated here so all control logic
   resides in one implementation file). */
void ctrl_hide_dropdown_list(window_t *win, gui_control_t *control) {
    (void)win; /* unused: kept for API consistency */
    if (!control) return;

    if (control->dropdown_saved_bg) {
        gfx_write_screen_region_packed(control->dropdown_saved_bg,
                                       control->dropdown_saved_w,
                                       control->dropdown_saved_h,
                                       control->dropdown_saved_x,
                                       control->dropdown_saved_y);
        kfree(control->dropdown_saved_bg);
        control->dropdown_saved_bg = NULL;
        control->dropdown_saved_w = 0;
        control->dropdown_saved_h = 0;
        control->dropdown_saved_x = 0;
        control->dropdown_saved_y = 0;
    }

    control->dropdown_open = 0;
    mouse_invalidate_buffer();
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
    gfx_hline(abs_x, abs_y + control->h - 1, control->w, theme->frame_dark); /* Can be changed to light */
    gfx_vline(abs_x + control->w - 1, border_y, control->h - (border_y - abs_y), theme->frame_dark); /* Can be changed to light */

    // Draw title text with background
    if (control->text[0] && font->data) {
        int text_x = abs_x + title_offset + 4;
        int text_y = abs_y + 5;

        // Draw title text
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", control->text);
    }
}

void ctrl_draw_scrollbar(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    
    /* Reusing fields: checked=orientation (0=vert, 1=horiz), cursor_pos=value, max_length=max_value */
    int vertical = !control->checked;
    int arrow_size = vertical ? control->w : control->h;
    int track_len = vertical ? (control->h - 2 * arrow_size) : (control->w - 2 * arrow_size);
    
    /* Calculate thumb size and position */
    int thumb_size = vertical ? 20 : 20;
    if (thumb_size > track_len) thumb_size = track_len;
    int max_val = control->max_length > 0 ? control->max_length : 100;
    int thumb_pos = 0;
    if (max_val > 0 && track_len > thumb_size) {
        /* Thumb position ranges from 0 to (track_len - thumb_size) */
        thumb_pos = ((track_len - thumb_size) * control->cursor_pos) / max_val;
    }
    
    /* Draw arrow buttons */
    if (vertical) {
        /* Up arrow button */
        int up_pressed = (control->hovered_item == 0 && control->pressed);
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
        
        /* Up arrow (pointing up) with base */
        int arrow_cx = abs_x + control->w / 2;
        int arrow_cy = abs_y + arrow_size / 2;
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_cx - 3 + i, arrow_cy - i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx - 1, arrow_cy + 1, 3, 3, COLOR_BLACK);
        
        /* Down arrow button */
        int down_y = abs_y + control->h - arrow_size;
        int down_pressed = (control->hovered_item == 2 && control->pressed);
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
        
        /* Down arrow (pointing down) with base */
        arrow_cy = down_y + arrow_size / 2;
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_cx - 3 + i, arrow_cy + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx - 1, arrow_cy - 3, 3, 3, COLOR_BLACK);
        
        /* Track area - dithered pattern */
        int track_y = abs_y + arrow_size;
        for (int py = 0; py < track_len; py++) {
            for (int px = 0; px < control->w; px++) {
                uint8_t color = ((px + py) & 1) ? COLOR_WHITE : 7; /* 1x1 dither white/light grey */
                gfx_putpixel(abs_x + px, track_y + py, color);
            }
        }
        
        /* Draw thumb (looks like empty button) */
        int thumb_y = track_y + thumb_pos;
        int thumb_pressed = (control->hovered_item == 1 && control->pressed);
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
        /* Horizontal scrollbar */
        /* Left arrow button */
        int left_pressed = (control->hovered_item == 0 && control->pressed);
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
        
        /* Left arrow (pointing left) with base */
        int arrow_cx = abs_x + arrow_size / 2;
        int arrow_cy = abs_y + control->h / 2;
        for (int i = 0; i < 4; i++) {
            gfx_vline(arrow_cx - i, arrow_cy - 3 + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx + 1, arrow_cy - 1, 3, 3, COLOR_BLACK);
        
        /* Right arrow button */
        int right_x = abs_x + control->w - arrow_size;
        int right_pressed = (control->hovered_item == 2 && control->pressed);
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
        
        /* Right arrow (pointing right) with base */
        arrow_cx = right_x + arrow_size / 2;
        for (int i = 0; i < 4; i++) {
            gfx_vline(arrow_cx + i, arrow_cy - 3 + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_cx - 3, arrow_cy - 1, 3, 3, COLOR_BLACK);
        
        /* Track area - dithered pattern */
        int track_x = abs_x + arrow_size;
        for (int py = 0; py < control->h; py++) {
            for (int px = 0; px < track_len; px++) {
                uint8_t color = ((px + py) & 1) ? COLOR_WHITE : 7;
                gfx_putpixel(track_x + px, abs_y + py, color);
            }
        }
        
        /* Draw thumb */
        int thumb_x = track_x + thumb_pos;
        int thumb_pressed = (control->hovered_item == 1 && control->pressed);
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
        ctrl_draw_icon(control, abs_x, abs_y, 7);
    }
    else if (control->type == 9) { /* CTRL_DROPDOWN */
        ctrl_draw_dropdown(control, abs_x, abs_y);
    }
    else if (control->type == 10) { /* CTRL_CLOCK */
        ctrl_draw_clock(control, abs_x, abs_y);
    }
    else if (control->type == 11) { /* CTRL_SCROLLBAR */
        ctrl_draw_scrollbar(control, abs_x, abs_y);
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
