#include "window.h"
#include "../drivers/graphics.h"
#include "../fonts/bmf.h"

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
    
    int i = 0;
    while (title[i] && i < 63) {
        win->title[i] = title[i];
        i++;
    }
    win->title[i] = '\0';
}

void win_draw(window_t *win) {
    if (!win->is_visible) return;
    
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
    
    gfx_fillrect(btn_x, btn_y, 14, 14, WIN_BUTTON_COLOR);
    gfx_rect(btn_x-1, btn_y-1, 16, 16, WIN_FRAME_DARK);
    gfx_rect(btn_x+3, btn_y+7, 10, 2, WIN_FRAME_DARK);
    gfx_fillrect(btn_x+2, btn_y+6, 10, 2, 15);
}

void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label) {
    gfx_fillrect(x, y, w, h, color);
    gfx_rect(x, y, w, h, 0);
    gfx_rect(x+1, y+1, w-2, h-2, WIN_FRAME_DARK);
    
    if (font_b.data && label) {
        int text_w = bmf_measure_text(&font_b, 12, label);
        int text_x = x + (w - text_w) / 2;
        int text_y = y + (h / 2) - 5;
        bmf_printf(text_x, text_y, &font_b, 12, 0, "%s", label);
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
                    WIN_BUTTON_COLOR, box->button_text);
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
    
    /* Allocate buffer if needed */
    int buf_size = win->w * win->h / 2;  // 4-bit packed
    if (!win->saved_bg) {
        win->saved_bg = kmalloc(buf_size);
        if (!win->saved_bg) return;
    }
    
    uint8_t *bb = gfx_get_backbuffer();
    
    /* Copy background region to saved buffer */
    for (int py = 0; py < win->h; py++) {
        int sy = win->y + py;
        if (sy < 0 || sy >= GFX_HEIGHT) continue;
        
        for (int px = 0; px < win->w; px += 2) {
            int sx = win->x + px;
            if (sx < 0 || sx >= GFX_WIDTH) continue;
            
            uint32_t screen_offset = sy * (GFX_WIDTH / 2) + (sx / 2);
            uint32_t buf_offset = py * (win->w / 2) + (px / 2);
            
            win->saved_bg[buf_offset] = bb[screen_offset];
        }
    }
}

void win_restore_background(window_t *win) {
    if (!win->saved_bg) return;
    
    uint8_t *bb = gfx_get_backbuffer();
    
    /* Restore background from saved buffer */
    for (int py = 0; py < win->h; py++) {
        int sy = win->y + py;
        if (sy < 0 || sy >= GFX_HEIGHT) continue;
        
        for (int px = 0; px < win->w; px += 2) {
            int sx = win->x + px;
            if (sx < 0 || sx >= GFX_WIDTH) continue;
            
            uint32_t screen_offset = sy * (GFX_WIDTH / 2) + (sx / 2);
            uint32_t buf_offset = py * (win->w / 2) + (px / 2);
            
            bb[screen_offset] = win->saved_bg[buf_offset];
        }
    }
}