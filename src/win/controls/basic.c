#include "internal.h"

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

static void draw_clock_hand_line(int cx, int cy, int length, int angle_idx, uint8_t color) {
    int sin_val = clock_sin(angle_idx);
    int cos_val = clock_cos(angle_idx);
    int ex = cx + (length * sin_val) / 1000;
    int ey = cy - (length * cos_val) / 1000;
    gfx_line(cx, cy, ex, ey, color);
}

static void draw_clock_face(int cx, int cy, int r, window_theme_t *theme) {
    gfx_circle(cx, cy, r, theme->text_color);
    if (r > 2) gfx_circle(cx, cy, r - 2, theme->frame_dark);

    int marker_len = (r * 15) / 100;
    if (marker_len < 3) marker_len = 3;
    int marker_gap = marker_len / 2 + 3;

    for (int i = 0; i < 12; i++) {
        int angle_idx = i * 5;
        int sin_val = clock_sin(angle_idx);
        int cos_val = clock_cos(angle_idx);
        int inner_r = r - marker_len - marker_gap;
        int outer_r = r - marker_gap;
        int ix = cx + (inner_r * sin_val) / 1000;
        int iy = cy - (inner_r * cos_val) / 1000;
        int ox = cx + (outer_r * sin_val) / 1000;
        int oy = cy - (outer_r * cos_val) / 1000;
        gfx_line(ix, iy, ox, oy, theme->text_color);
    }
}

void ctrl_draw_clock(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    uint8_t bg_color = (control->bg == -1) ? theme->bg_color : control->bg;
    
    gfx_fillrect(abs_x, abs_y, control->w, control->h, bg_color);
    
    int cx = abs_x + control->w / 2;
    int cy = abs_y + control->h / 2;
    int r = (control->w < control->h ? control->w : control->h) / 2 - 5;
    if (r < 10) r = 10;
    
    draw_clock_face(cx, cy, r, theme);
    
    rtc_time_t time;
    rtc_read_time(&time);
    
    int hour_pos = ((time.hour % 12) * 5) + (time.minute / 12);
    int min_pos = time.minute;
    int sec_pos = time.second;
    
    draw_clock_hand_line(cx, cy, r * 5 / 10, hour_pos, theme->text_color);
    draw_clock_hand_line(cx, cy, r * 7 / 10, min_pos, theme->text_color);
    draw_clock_hand_line(cx, cy, r * 8 / 10, sec_pos, COLOR_LIGHT_RED);
    
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

    int has_icon = (control->button.cached_bitmap_orig != NULL);
    int has_text = (control->text[0] != '\0');
    
    /* Don't pass text to win_draw_button if we have an icon - we'll draw it ourselves */
    if (control->button.pressed) {
        win_draw_button(abs_x, abs_y, control->w, control->h, btn_color, has_icon ? "" : control->text, 1);
    } else {
        win_draw_button(abs_x, abs_y, control->w, control->h, btn_color, has_icon ? "" : control->text, 0);
    }
    
    if (has_icon) {
        bitmap_t *bmp = control->button.cached_bitmap_orig;
        int offset = control->button.pressed ? 1 : 0;
        
        if (has_text) {
            int icon_x = abs_x + 3 + offset;
            int icon_y = abs_y + 2 + offset;
            bitmap_draw(bmp, icon_x, icon_y);
            
            int text_x = abs_x + 22;
            int text_y = abs_y + 7;
            bmf_printf(text_x, text_y, &font_b, 12, theme->text_color, "%s", control->text);
        } else {
            int icon_x = abs_x + (control->w - bmp->width) / 2 + offset;
            int icon_y = abs_y + (control->h - bmp->height) / 2 + offset;
            bitmap_draw(bmp, icon_x, icon_y);
        }
    }
}

void ctrl_draw_label(gui_control_t *control, int abs_x, int abs_y) {
    bmf_font_t *font = &font_n;

    if (control->font_type == 1) font = &font_b;
    else if (control->font_type == 2) font = &font_i;
    else if (control->font_type == 3) font = &font_bi;

    int size = control->font_size > 0 ? control->font_size : 12;

    if (font->data) {
        int seq_idx = -1;
        for (int i = 0; i < font->size_count; i++) {
            if (font->sequences[i].point_size == size) {
                seq_idx = i;
                break;
            }
        }

        int font_height = seq_idx >= 0 ? font->sequences[seq_idx].height : size;

        int max_line_width = 0;
        int line_count = 1;
        int current_line_width = 0;

        const char *p = control->text;
        const char *line_start = p;

        while (*p) {
            if (*p == '\\' && *(p+1) == 'n') {
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

        int text_h = font_height + ((line_count - 1) * (font_height + 2)) + 4;

        int label_w = control->w > 0 ? control->w : max_line_width + 4;
        int label_h = control->h > 0 ? control->h : text_h;

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
        if (!control->picturebox.cached_bitmap_orig && !control->picturebox.load_failed) {
            control->picturebox.cached_bitmap_orig = bitmap_load_from_file(control->text);
            /* When loading a new image, drop any previous scaled cache */
            if (control->picturebox.cached_bitmap_scaled) {
                bitmap_free(control->picturebox.cached_bitmap_scaled);
                control->picturebox.cached_bitmap_scaled = NULL;
            }

            /* Validate loaded bitmap to avoid processing malformed images */
            if (control->picturebox.cached_bitmap_orig) {
                bitmap_t *orig = control->picturebox.cached_bitmap_orig;
                if (orig->width <= 0 || orig->height <= 0 || orig->width > 4096 || orig->height > 4096) {
                    /* Unreasonable dimensions - reject */
                    bitmap_free(control->picturebox.cached_bitmap_orig);
                    control->picturebox.cached_bitmap_orig = NULL;
                    control->picturebox.load_failed = 1;
                }
            } else {
                /* Loading failed - mark to avoid retrying every draw */
                control->picturebox.load_failed = 1;
            }
        }

        /* Draw from cached bitmap. support optional stretch mode or preserve aspect ratio */
        if (control->picturebox.cached_bitmap_orig) {
            bitmap_t *orig = control->picturebox.cached_bitmap_orig;
            int bw = orig->width;
            int bh = orig->height;

            /* Stretch mode: scale image to exactly fill the control (may change aspect) */
            if (control->picturebox.image_mode == 1) {
                if (!control->picturebox.cached_bitmap_scaled ||
                    control->picturebox.cached_bitmap_scaled->width != control->w ||
                    control->picturebox.cached_bitmap_scaled->height != control->h) {
                    if (control->picturebox.cached_bitmap_scaled) {
                        bitmap_free(control->picturebox.cached_bitmap_scaled);
                        control->picturebox.cached_bitmap_scaled = NULL;
                    }
                    /* Scale to exact control dimensions */
                    control->picturebox.cached_bitmap_scaled = bitmap_scale_nearest(orig, control->w, control->h);
                }

                if (control->picturebox.cached_bitmap_scaled) {
                    if (control->bg == -1) bitmap_draw(control->picturebox.cached_bitmap_scaled, abs_x, abs_y);
                    else bitmap_draw_opaque(control->picturebox.cached_bitmap_scaled, abs_x, abs_y);
                }

            } else {
                /* Existing behavior: preserve aspect ratio and center */
                /* Exact match: if bitmap size equals control size, draw without scaling */
                if (bw == control->w && bh == control->h) {
                    /* Draw original at control origin with no scaling and clear any scaled cache */
                    if (control->picturebox.cached_bitmap_scaled) {
                        bitmap_free(control->picturebox.cached_bitmap_scaled);
                        control->picturebox.cached_bitmap_scaled = NULL;
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
                    if (!control->picturebox.cached_bitmap_scaled ||
                        control->picturebox.cached_bitmap_scaled->width != new_w ||
                        control->picturebox.cached_bitmap_scaled->height != new_h) {
                        if (control->picturebox.cached_bitmap_scaled) {
                            bitmap_free(control->picturebox.cached_bitmap_scaled);
                            control->picturebox.cached_bitmap_scaled = NULL;
                        }
                        control->picturebox.cached_bitmap_scaled = bitmap_scale_nearest(orig, new_w, new_h);
                    }

                    if (control->picturebox.cached_bitmap_scaled) {
                        int dx = abs_x + (control->w - control->picturebox.cached_bitmap_scaled->width) / 2;
                        int dy = abs_y + (control->h - control->picturebox.cached_bitmap_scaled->height) / 2;
                        /* Draw scaled bitmap; respect bg==-1 for transparency */
                        if (control->bg == -1) bitmap_draw(control->picturebox.cached_bitmap_scaled, dx, dy);
                        else bitmap_draw_opaque(control->picturebox.cached_bitmap_scaled, dx, dy);
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
    if (control->checkbox.checked) {
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
    if (control->radiobutton.checked) {
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
