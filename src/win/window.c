#include "window.h"
#include "../syscall.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"
#include "../mem/heap.h"

#define WIN_BG_COLOR        15
#define WIN_TITLEBAR_COLOR  1
#define WIN_FRAME_DARK      8
#define WIN_FRAME_LIGHT     7
#define WIN_BUTTON_COLOR    7

bmf_font_t font_b, font_n, font_i, font_bi;

void win_init_fonts(void) {
    bmf_import(&font_b, "C:/FONTS/OSANS_B.BMF");
    bmf_import(&font_n, "C:/FONTS/OSANS.BMF");
    bmf_import(&font_i, "C:/FONTS/OSANS_I.BMF");
    bmf_import(&font_bi, "C:/FONTS/OSANS_BI.BMF");
}

void win_create(window_t *win, int x, int y, int w, int h, const char *title) {
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
    win->prev_x = x;
    win->prev_y = y;
    win->is_visible = 1;
    win->is_modal = 0;
    win->dirty = 1;
    win->saved_bg = NULL;
    win->is_minimized = 0;
    win->icon_x = 0;
    win->icon_y = 0;
    win->icon_bitmap = NULL;
    win->icon_saved_bg = NULL;
    win->icon_path[0] = '\0';
    
    int i = 0;
    while (title[i] && i < 63) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = '\0';
}

void win_draw(window_t *win) {
    if (!win->is_visible) return;

    if (win->is_minimized) {
        win_draw_icon(win);
        return;
    }
    
    /* Save background BEFORE first draw */
    if (!win->saved_bg) {
        win_save_background(win);
    }
    
    win_draw_frame(win->x, win->y, win->w, win->h);
    win_draw_titlebar(win->x+2, win->y+2, win->w-4, win->title);
    
    win->dirty = 0;
}

void win_destroy(window_t *win) {
    win->is_visible = 0;
    if (win->saved_bg) {
        kfree(win->saved_bg);
        win->saved_bg = NULL;
    }
    if (win->icon_bitmap) {
        kfree(win->icon_bitmap);
        win->icon_bitmap = NULL;
    }
    if (win->icon_saved_bg) {
        kfree(win->icon_saved_bg);
        win->icon_saved_bg = NULL;
    }
}

void win_draw_frame(int x, int y, int w, int h) {
    gfx_fillrect(x, y + 18, w, h - 18, WIN_BG_COLOR);
    gfx_rect(x, y, w, h, WIN_FRAME_DARK);
    gfx_rect(x+1, y+1, w-2, h-2, WIN_FRAME_LIGHT);
}

void win_draw_titlebar(int x, int y, int w, const char *title) {
    gfx_fillrect(x, y, w, 18, WIN_TITLEBAR_COLOR);
       
    if (font_b.data) {
        bmf_printf(x+5, y+4, &font_b, 12, 15, "%s", title);
    }
    
    int btn_x = x + w - 16;
    int btn_y = y + 2;
    
    gfx_fillrect(btn_x, btn_y, 14, 14, 7);
    gfx_rect(btn_x-1, btn_y-1, 16, 16, 8);
    gfx_rect(btn_x+3, btn_y+7, 10, 2, 8);
    gfx_fillrect(btn_x+2, btn_y+6, 10, 2, 15);
}

void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label, int pressed) {
    int shad_a, shad_b;
    gfx_rect(x, y, w, h, 0);
    gfx_fillrect(x+2, y+2, w-3, h-3, color);
    if (pressed == 1) {
        shad_a = 15;
        shad_b = 8;
    } else {
        shad_a = 8;
        shad_b = 15;
    }
    gfx_rect(x+1, y+1, w-2, h-2, shad_a);
    gfx_hline(x+1, y+1, w-3, shad_b);
    gfx_vline(x+1, y+1, h-3, shad_b);

    if (font_b.data && label) {
        int text_w = bmf_measure_text(&font_b, 12, label);
        int text_x = x + (w - text_w) / 2;
        int text_y = y + (h / 2) - 4;
        bmf_printf(text_x, text_y, &font_b, 12, 0, "%s", label);
    }
}

void win_draw_control(window_t *win, void *ctrl) {
    gui_control_t *control = (gui_control_t*)ctrl;

    int abs_x = win->x + control->x;
    int abs_y = win->y + control->y + 20;

    if (control->type == 1) { /* CTRL_BUTTON */
        if (control->pressed) {
            win_draw_button(abs_x, abs_y, control->w, control->h, control->bg, control->text, 1);
        } else {
            win_draw_button(abs_x, abs_y, control->w, control->h, control->bg, control->text, 0);
        }
    }
    else if (control->type == 2) { /* CTRL_LABEL */
        bmf_font_t *font = &font_n;
        
        if (control->font_type == 1) font = &font_b;
        else if (control->font_type == 2) font = &font_i;
        else if (control->font_type == 3) font = &font_bi;
        
        int size = control->font_size > 0 ? control->font_size : 12;
        
        if (font->data) {
            /* Get actual font height from sequence */
            extern int bmf_find_sequence_for_size(bmf_font_t *font, uint8_t point_size);
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
    else if (control->type == 3) { /* CTRL_PICTUREBOX */
        gfx_fillrect(abs_x, abs_y, control->w, control->h, 8);
        gfx_rect(abs_x, abs_y, control->w, control->h, 7);

        if (control->text[0]) {
            /* Load bitmap to cache if not already loaded */
            if (!control->cached_bitmap) {
                extern uint8_t* gfx_load_bmp_to_buffer(const char *path, int *out_width, int *out_height);
                int width, height;
                control->cached_bitmap = gfx_load_bmp_to_buffer(control->text, &width, &height);
                if (control->cached_bitmap) {
                    control->bmp_width = width;
                    control->bmp_height = height;
                }
            }

            /* Draw from cached bitmap */
            if (control->cached_bitmap) {
                extern void gfx_draw_cached_bmp(uint8_t *cached_data, int width, int height, int dest_x, int dest_y);
                gfx_draw_cached_bmp(control->cached_bitmap, control->bmp_width, control->bmp_height, abs_x, abs_y);
            }
        }
    }
}

void win_msgbox_create(msgbox_t *box, const char *msg, const char *btn, const char *title) {
    int msg_len = 0;
    while (msg[msg_len]) msg_len++;
    
    int btn_len = 0;
    while (btn[btn_len]) btn_len++;
    
    int msg_w = 0;
    int btn_text_w = 0;
    
    if (font_n.data) {
        msg_w = bmf_measure_text(&font_n, 12, msg);
    }
    
    if (font_b.data) {
        btn_text_w = bmf_measure_text(&font_b, 12, btn);
    }
    
    int btn_w = btn_text_w + 24;
    int btn_h = 18;
    
    int content_w = msg_w > btn_w ? msg_w : btn_w;
    int win_w = content_w + 40;
    int win_h = 82;
    
    if (win_w < 120) win_w = 120;
    
    int win_x = (640 - win_w) / 2;
    int win_y = (480 - win_h) / 2;
    
    win_create(&box->base, win_x, win_y, win_w, win_h, title);
    box->base.is_modal = 1;
    
    int i = 0;
    while (msg[i] && i < 255) {
        box->message[i] = msg[i];
        i++;
    }
    box->message[i] = '\0';
    
    i = 0;
    while (btn[i] && i < 31) {
        box->button_text[i] = btn[i];
        i++;
    }
    box->button_text[i] = '\0';
    
    /* Store button offsets relative to window */
    box->button_x = (win_w - btn_w) / 2;
    box->button_y = 57;
    box->button_w = btn_w;
    box->button_h = btn_h;
}

void win_msgbox_draw(msgbox_t *box) {
    win_draw(&box->base);
    
    if (font_n.data) {
        int text_w = bmf_measure_text(&font_n, 12, box->message);
        int text_x = box->base.x + (box->base.w - text_w) / 2;
        bmf_printf(text_x, box->base.y + 31, &font_n, 12, 0, "%s", box->message);
    }
    
    /* Calculate absolute button position */
    int abs_btn_x = box->base.x + box->button_x;
    int abs_btn_y = box->base.y + box->button_y;
    
    win_draw_button(abs_btn_x, abs_btn_y, 
                    box->button_w, box->button_h, 
                    WIN_BUTTON_COLOR, box->button_text, 0);
}

int win_msgbox_handle_click(msgbox_t *box, int mx, int my) {
    /* Calculate absolute button position */
    int abs_btn_x = box->base.x + box->button_x;
    int abs_btn_y = box->base.y + box->button_y;
    
    if (mx >= abs_btn_x && mx < abs_btn_x + box->button_w &&
        my >= abs_btn_y && my < abs_btn_y + box->button_h) {
        return 1;
    }
    return 0;
}

int win_is_titlebar(window_t *win, int mx, int my) {
    int close_btn_x = win->x + win->w - 16;
    
    if (mx >= win->x && 
        mx < close_btn_x &&
        my >= win->y && 
        my < win->y + 18) {
        return 1;
    }
    return 0;
}

void win_move(window_t *win, int dx, int dy) {
    /* Restore old background */
    win_restore_background(win);
    
    /* Update position */
    win->x += dx;
    win->y += dy;
    
    /* Keep window on screen */
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;
    if (win->x + win->w > 640) win->x = 640 - win->w;
    if (win->y + win->h > 480) win->y = 480 - win->h;
    
    /* Save new background */
    win_save_background(win);
    win->dirty = 1;
}

void win_mark_dirty(window_t *win) {
    win->dirty = 1;
}

void win_clear_dirty(window_t *win) {
    win->dirty = 0;
}

int win_needs_redraw(window_t *win) {
    return win->dirty;
}

void win_save_background(window_t *win) {
    if (!win->is_visible) return;
    
    int margin = 10;
    int w = win->w + 2 * margin;
    int h = win->h + 2 * margin;
    int buf_size = w * h;
    
    if (!win->saved_bg) {
        win->saved_bg = kmalloc(buf_size);
        if (!win->saved_bg) return;
    }
    
    extern uint8_t gfx_getpixel(int x, int y);
    
    for (int py = 0; py < h; py++) {
        int sy = win->y + py - margin;
        if (sy < 0 || sy >= GFX_HEIGHT) continue;
        
        for (int px = 0; px < w; px++) {
            int sx = win->x + px - margin;
            if (sx < 0 || sx >= GFX_WIDTH) continue;
            
            win->saved_bg[py * w + px] = gfx_getpixel(sx, sy);
        }
    }
}

void win_restore_background(window_t *win) {
    if (!win->saved_bg) return;
    
    int margin = 10;
    int w = win->w + 2 * margin;
    int h = win->h + 2 * margin;
    
    extern void gfx_putpixel(int x, int y, uint8_t color);
    
    for (int py = 0; py < h; py++) {
        int sy = win->y + py - margin;
        if (sy < 0 || sy >= GFX_HEIGHT) continue;
        
        for (int px = 0; px < w; px++) {
            int sx = win->x + px - margin;
            if (sx < 0 || sx >= GFX_WIDTH) continue;
            
            gfx_putpixel(sx, sy, win->saved_bg[py * w + px]);
        }
    }
}

int win_is_minimize_button(window_t *win, int mx, int my) {
    int btn_x = win->x + win->w - 18;
    int btn_y = win->y + 4;
    
    if (mx >= btn_x && mx < btn_x + 14 &&
        my >= btn_y && my < btn_y + 14) {
        return 1;
    }
    return 0;
}

void win_minimize(window_t *win, int icon_x, int icon_y) {
    if (win->is_minimized) return;
    
    win_restore_background(win);
    
    win->is_minimized = 1;
    
    if (win->icon_x == 0 && win->icon_y == 0) {
        win->icon_x = icon_x;
        win->icon_y = icon_y;
    }
    
    if (win->icon_path[0] && !win->icon_bitmap) {
        extern uint8_t* gfx_load_bmp_to_buffer(const char *path, int *out_width, int *out_height);
        int w, h;
        win->icon_bitmap = gfx_load_bmp_to_buffer(win->icon_path, &w, &h);
    }
}

void win_restore(window_t *win) {
    if (!win->is_minimized) return;
    
    /* Restore saved background */
    if (win->icon_saved_bg) {
        extern void gfx_putpixel(int x, int y, uint8_t color);
        for (int y = 0; y < 32; y++) {
            for (int x = 0; x < 32; x++) {
                gfx_putpixel(win->icon_x + x, win->icon_y + y, win->icon_saved_bg[y * 32 + x]);
            }
        }
        kfree(win->icon_saved_bg);
        win->icon_saved_bg = NULL;
    }
    
    win->is_minimized = 0;
    win->dirty = 1;
    
    win_save_background(win);
}

void win_draw_icon(window_t *win) {
    if (!win->is_minimized) return;
    
    /* Save background before drawing icon (only once) */
    if (!win->icon_saved_bg) {
        win->icon_saved_bg = kmalloc(32 * 32);
        if (win->icon_saved_bg) {
            extern uint8_t gfx_getpixel(int x, int y);
            for (int y = 0; y < 32; y++) {
                for (int x = 0; x < 32; x++) {
                    win->icon_saved_bg[y * 32 + x] = gfx_getpixel(win->icon_x + x, win->icon_y + y);
                }
            }
        }
    }
    
    if (win->icon_bitmap) {
        extern void gfx_putpixel(int x, int y, uint8_t color);
        
        for (int y = 0; y < 32; y++) {
            int src_offset = y * ((32 + 1) / 2);
            
            for (int x = 0; x < 32; x++) {
                int byte_idx = x / 2;
                uint8_t color = (x & 1) 
                    ? (win->icon_bitmap[src_offset + byte_idx] & 0x0F)
                    : (win->icon_bitmap[src_offset + byte_idx] >> 4);
                
                if (color != 5) {
                    gfx_putpixel(win->icon_x + x, win->icon_y + y, color);
                }
            }
        }
    } else {
        gfx_fillrect(win->icon_x, win->icon_y, 32, 32, 7);
        gfx_rect(win->icon_x, win->icon_y, 32, 32, 8);
        
        if (font_b.data && win->title[0]) {
            char icon_label[3];
            icon_label[0] = win->title[0];
            icon_label[1] = win->title[1] ? win->title[1] : '\0';
            icon_label[2] = '\0';
            
            int tw = bmf_measure_text(&font_b, 12, icon_label);
            int tx = win->icon_x + (32 - tw) / 2;
            int ty = win->icon_y + 10;
            
            bmf_printf(tx, ty, &font_b, 12, 0, "%s", icon_label);
        }
    }
}

int win_is_icon_clicked(window_t *win, int mx, int my) {
    if (!win->is_minimized) return 0;
    
    if (mx >= win->icon_x && mx < win->icon_x + 32 &&
        my >= win->icon_y && my < win->icon_y + 32) {
        return 1;
    }
    return 0;
}