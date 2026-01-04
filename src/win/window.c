#include "window.h"
#include "wm_config.h"
#include "theme.h"
#include "bitmap.h"
#include "controls.h"
#include "../syscall.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"
#include "../mem/heap.h"
#include "../console.h"

bmf_font_t font_b, font_n, font_i, font_bi;

void win_init_fonts(void) {
    bmf_import(&font_b, "C:/FONTS/LSANS_B.BMF");
    bmf_import(&font_n, "C:/FONTS/LSANS.BMF");
    bmf_import(&font_i, "C:/FONTS/LSANS_I.BMF");
    bmf_import(&font_bi, "C:/FONTS/LSANS_BI.BMF");
}

void win_create(window_t *win, int x, int y, int w, int h, const char *title) {
    win->x = x;
    win->y = y;
    win->w = w;
    win->h = h;
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

    strcpy_s(win->title, title, 64);
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
        bitmap_free(win->icon_bitmap);
        win->icon_bitmap = NULL;
    }
    if (win->icon_saved_bg) {
        kfree(win->icon_saved_bg);
        win->icon_saved_bg = NULL;
    }
}

void win_draw_frame(int x, int y, int w, int h) {
    window_theme_t *theme = theme_get_current();
    gfx_fillrect(x, y + WM_TITLEBAR_HEIGHT, w, h - WM_TITLEBAR_HEIGHT, theme->bg_color);
    gfx_rect(x, y, w, h, theme->frame_dark);
    gfx_rect(x+1, y+1, w-2, h-2, theme->frame_light);
}

void win_draw_titlebar(int x, int y, int w, const char *title) {
    window_theme_t *theme = theme_get_current();
    gfx_fillrect(x, y, w, WM_TITLEBAR_HEIGHT, theme->titlebar_color);

    if (font_b.data) {
        bmf_printf(x+5, y+5, &font_b, 12, 15, "%s", title);
    }

    int btn_x = x + w - 16;
    int btn_y = y + 2;

    gfx_fillrect(btn_x, btn_y, 14, 14, theme->button_color);
    gfx_rect(btn_x-1, btn_y-1, 16, 16, theme->frame_dark);
    gfx_rect(btn_x+3, btn_y+7, 10, 2, theme->frame_dark);
    gfx_fillrect(btn_x+2, btn_y+6, 10, 2, 15);
}

void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label, int pressed) {
    window_theme_t *theme = theme_get_current();
    int shad_a, shad_b;
    gfx_rect(x, y, w, h, theme->text_color);
    gfx_fillrect(x+2, y+2, w-3, h-3, color);
    if (pressed == 1) {
        shad_a = 15;
        shad_b = theme->frame_dark;
    } else {
        shad_a = theme->frame_dark;
        shad_b = 15;
    }
    gfx_rect(x+1, y+1, w-2, h-2, shad_a);
    gfx_hline(x+1, y+1, w-3, shad_b);
    gfx_vline(x+1, y+1, h-3, shad_b);

    if (font_b.data && label) {
        int text_w = bmf_measure_text(&font_b, 12, label);
        int text_x = x + (w - text_w) / 2 + 2;
        int text_y = y + (h / 2) - 3;
        bmf_printf(text_x, text_y, &font_b, 12, theme->text_color, "%s", label);
    }
}

void win_draw_control(window_t *win, void *ctrl) {
    gui_control_t *control = (gui_control_t*)ctrl;
    ctrl_draw(win, control);
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
    
    int win_x = (WM_SCREEN_WIDTH - win_w) / 2;
    int win_y = (WM_SCREEN_HEIGHT - win_h) / 2;
    
    win_create(&box->base, win_x, win_y, win_w, win_h, title);
    box->base.is_modal = 1;
    
    strcpy_s(box->message, msg, 256);
    strcpy_s(box->button_text, btn, 32);
    
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

    window_theme_t *theme = theme_get_current();
    win_draw_button(abs_btn_x, abs_btn_y,
                    box->button_w, box->button_h,
                    theme->button_color, box->button_text, 0);
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
        my < win->y + WM_TITLEBAR_HEIGHT) {
        return 1;
    }
    return 0;
}

void win_move(window_t *win, int dx, int dy) {
    /* Restore cursor before any window operations to prevent artifacts */
    extern void mouse_restore(void);
    extern int buffer_valid;
    if (buffer_valid) {
        mouse_restore();
        buffer_valid = 0;  /* Invalidate so desktop will redraw cursor */
    }

    /* Restore old background */
    win_restore_background(win);

    /* Update position */
    win->x += dx;
    win->y += dy;

    /* Keep window on screen */
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;
    if (win->x + win->w > WM_SCREEN_WIDTH) win->x = WM_SCREEN_WIDTH - win->w;
    if (win->y + win->h > WM_SCREEN_HEIGHT) win->y = WM_SCREEN_HEIGHT - win->h;

    /* Save new background (cursor already restored above) */
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

    int margin = WM_BG_MARGIN;
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
        if (sy < 0 || sy >= WM_SCREEN_HEIGHT) continue;

        for (int px = 0; px < w; px++) {
            int sx = win->x + px - margin;
            if (sx < 0 || sx >= WM_SCREEN_WIDTH) continue;

            win->saved_bg[py * w + px] = gfx_getpixel(sx, sy);
        }
    }
}

void win_restore_background(window_t *win) {
    if (!win->saved_bg) return;

    int margin = WM_BG_MARGIN;
    int w = win->w + 2 * margin;
    int h = win->h + 2 * margin;

    extern void gfx_putpixel(int x, int y, uint8_t color);

    for (int py = 0; py < h; py++) {
        int sy = win->y + py - margin;
        if (sy < 0 || sy >= WM_SCREEN_HEIGHT) continue;

        for (int px = 0; px < w; px++) {
            int sx = win->x + px - margin;
            if (sx < 0 || sx >= WM_SCREEN_WIDTH) continue;

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

    /* Invalidate cursor buffer since window is disappearing */
    extern int buffer_valid;
    buffer_valid = 0;

    win_restore_background(win);

    win->is_minimized = 1;

    if (win->icon_x == 0 && win->icon_y == 0) {
        win->icon_x = icon_x;
        win->icon_y = icon_y;
    }

    if (win->icon_path[0] && !win->icon_bitmap) {
        win->icon_bitmap = bitmap_load_from_file(win->icon_path);
    }
}

void win_restore(window_t *win) {
    if (!win->is_minimized) return;

    /* Invalidate cursor buffer since window is appearing */
    extern int buffer_valid;
    buffer_valid = 0;

    /* Restore saved background */
    if (win->icon_saved_bg) {
        extern void gfx_putpixel(int x, int y, uint8_t color);
        for (int y = 0; y < WM_ICON_SIZE; y++) {
            for (int x = 0; x < WM_ICON_SIZE; x++) {
                gfx_putpixel(win->icon_x + x, win->icon_y + y, win->icon_saved_bg[y * WM_ICON_SIZE + x]);
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
        win->icon_saved_bg = kmalloc(WM_ICON_SIZE * WM_ICON_SIZE);
        if (win->icon_saved_bg) {
            extern uint8_t gfx_getpixel(int x, int y);
            for (int y = 0; y < WM_ICON_SIZE; y++) {
                for (int x = 0; x < WM_ICON_SIZE; x++) {
                    win->icon_saved_bg[y * WM_ICON_SIZE + x] = gfx_getpixel(win->icon_x + x, win->icon_y + y);
                }
            }
        }
    }

    if (win->icon_bitmap) {
        // Use bitmap API for drawing
        bitmap_draw(win->icon_bitmap, win->icon_x, win->icon_y);
    } else {
        // Draw default icon with title initials
        window_theme_t *theme = theme_get_current();
        gfx_fillrect(win->icon_x, win->icon_y, WM_ICON_SIZE, WM_ICON_SIZE, theme->button_color);
        gfx_rect(win->icon_x, win->icon_y, WM_ICON_SIZE, WM_ICON_SIZE, theme->frame_dark);

        if (font_b.data && win->title[0]) {
            char icon_label[3];
            icon_label[0] = win->title[0];
            icon_label[1] = win->title[1] ? win->title[1] : '\0';
            icon_label[2] = '\0';

            int tw = bmf_measure_text(&font_b, 12, icon_label);
            int tx = win->icon_x + (WM_ICON_SIZE - tw) / 2;
            int ty = win->icon_y + 10;

            bmf_printf(tx, ty, &font_b, 12, theme->text_color, "%s", icon_label);
        }
    }
}

int win_is_icon_clicked(window_t *win, int mx, int my) {
    if (!win->is_minimized) return 0;

    if (mx >= win->icon_x && mx < win->icon_x + WM_ICON_SIZE &&
        my >= win->icon_y && my < win->icon_y + WM_ICON_SIZE) {
        return 1;
    }
    return 0;
}