#include "window.h"
#include "wm.h"
#include "wm_config.h"
#include "theme.h"
#include "bitmap.h"
#include "icon.h"
#include "controls.h"
#include "../syscall.h"
#include "../drivers/graphics.h"
#include "../drivers/mouse.h"
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
    win->dirty = 1;
    win->saved_bg = NULL;
    win->is_minimized = 0;
    win->minimized_icon_id = -1;
    win->resizable = 1;
    win->is_taskbar = 0;
    win->is_maximized = 0;
    win->saved_x = 0;
    win->saved_y = 0;
    win->saved_w = 0;
    win->saved_h = 0;

    strcpy_s(win->title, title, 64);
}

void win_draw(window_t *win) {
    win_draw_focused(win, 1);
}

void win_draw_focused(window_t *win, int is_focused) {
    if (!win->is_visible) return;
    if (win->is_minimized) return;

    /* Save background BEFORE first draw */
    if (!win->saved_bg) {
        win_save_background(win);
    }

    /* Taskbar windows don't have frames or titlebars - just the background */
    if (win->is_taskbar) {
        window_theme_t *theme = theme_get_current();
        gfx_fillrect(win->x, win->y, win->w, win->h, theme->taskbar_color);
        /* Draw top highlight line */
        gfx_hline(win->x, win->y, win->w, COLOR_WHITE);
    } else {
        win_draw_frame(win->x, win->y, win->w, win->h);
        win_draw_titlebar(win->x+2, win->y+2, win->w-4, win->title, is_focused);
    }

    win->dirty = 0;
}

void win_destroy(window_t *win) {
    /* Restore background before destroying */
    if (win->saved_bg && !win->is_minimized) {
        win_restore_background(win);
    }

    win->is_visible = 0;
    if (win->saved_bg) {
        kfree(win->saved_bg);
        win->saved_bg = NULL;
    }
    mouse_invalidate_buffer();
}

void win_draw_frame(int x, int y, int w, int h) {
    window_theme_t *theme = theme_get_current();
    gfx_fillrect(x, y + WM_TITLEBAR_HEIGHT, w, h - WM_TITLEBAR_HEIGHT, theme->bg_color);
    gfx_rect(x, y, w, h, theme->frame_dark);
    gfx_rect(x+1, y+1, w-2, h-2, theme->frame_light);
}

void win_draw_titlebar(int x, int y, int w, const char *title, int is_active) {
    window_theme_t *theme = theme_get_current();
    uint8_t tb_color = is_active ? theme->titlebar_color : theme->titlebar_inactive;

    gfx_fillrect(x, y, w, WM_TITLEBAR_HEIGHT, tb_color);

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

void win_draw_3d_border_raised(int x, int y, int w, int h) {
    window_theme_t *theme = theme_get_current();
    gfx_rect(x, y, w, h, theme->text_color);
    gfx_rect(x+1, y+1, w-2, h-2, theme->frame_dark);
    gfx_hline(x+1, y+1, w-3, 15);
    gfx_vline(x+1, y+1, h-3, 15);
}

void win_draw_3d_border_sunken(int x, int y, int w, int h) {
    window_theme_t *theme = theme_get_current();
    gfx_rect(x, y, w, h, theme->text_color);
    gfx_rect(x+1, y+1, w-2, h-2, 15);
    gfx_hline(x+1, y+1, w-3, theme->frame_dark);
    gfx_vline(x+1, y+1, h-3, theme->frame_dark);
}

void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label, int pressed) {
    window_theme_t *theme = theme_get_current();
    gfx_fillrect(x+2, y+2, w-3, h-3, color);
    
    if (pressed) {
        win_draw_3d_border_sunken(x, y, w, h);
    } else {
        win_draw_3d_border_raised(x, y, w, h);
    }

    if (font_b.data && label) {
        int text_w = bmf_measure_text(&font_b, 12, label);
        int text_x = x + (w - text_w) / 2 + 1;
        int text_y = y + (h / 2) - 3;
        bmf_printf(text_x, text_y, &font_b, 12, theme->text_color, "%s", label);
    }
}

void win_draw_control(window_t *win, void *ctrl) {
    gui_control_t *control = (gui_control_t*)ctrl;
    ctrl_draw(win, control);
}

void win_draw_dropdown_list(window_t *win, void *ctrl) {
    gui_control_t *control = (gui_control_t*)ctrl;
    ctrl_draw_dropdown_list(win, control);
}

static int contains_any(const char *s, const char *chars) {
    if (!s || !chars) return 0;
    for (; *s; s++) {
        for (const char *c = chars; *c; c++) {
            if (*s == *c)
                return 1;
        }
    }
    return 0;
}

void win_msgbox_create(msgbox_t *box, const char *msg, const char *btn, const char *title) {
    /* Initialize fields */
    box->message[0] = '\0';
    box->icon[0] = '\0';
    for (int i = 0; i < 3; i++) {
        box->buttons[i][0] = '\0';
        box->button_w[i] = 0;
        box->button_x[i] = 0;
    }
    box->button_count = 0;
    box->default_button = 0;

    strcpy_s(box->message, msg ? msg : "", 256);

    const char *p = btn ? btn : "OK";

    /* Parse ICON= prefix (case-insensitive) */
    if (p[0] &&
        ((p[0] == 'I' || p[0] == 'i') &&
         (p[1] == 'C' || p[1] == 'c') &&
         (p[2] == 'O' || p[2] == 'o') &&
         (p[3] == 'N' || p[3] == 'n') &&
         p[4] == '=')) {

        const char *semi = p + 5;
        const char *q = semi;

        while (*q && *q != ';') q++;

        if (*q == ';') {
            int len = (int)(q - semi);
            int copy_len = len < (int)sizeof(box->icon) - 1
                         ? len
                         : (int)sizeof(box->icon) - 1;

            for (int i = 0; i < copy_len; i++) {
                char c = semi[i];
                if (c >= 'A' && c <= 'Z') c += 32;
                box->icon[i] = c;
            }
            box->icon[copy_len] = '\0';
            p = q + 1;
        } else {
            int i = 0;
            while (semi[i] && i < (int)sizeof(box->icon) - 1) {
                char c = semi[i];
                if (c >= 'A' && c <= 'Z') c += 32;
                box->icon[i] = c;
                i++;
            }
            box->icon[i] = '\0';
            strcpy_s(box->buttons[0], "OK", 32);
            box->button_count = 1;
            p = NULL;
        }
    }

    /* Parse buttons separated by '|' */
    if (p && box->button_count == 0) {
        const char *q = p;
        int idx = 0;

        while (*q && idx < 3) {
            const char *bar = q;
            int len = 0;

            while (*bar && *bar != '|') {
                len++;
                bar++;
            }

            if (len > 0) {
                int copy_len = len < 31 ? len : 31;
                for (int i = 0; i < copy_len; i++)
                    box->buttons[idx][i] = q[i];
                box->buttons[idx][copy_len] = '\0';
                idx++;
            }

            if (*bar == '|')
                q = bar + 1;
            else
                break;
        }

        if (idx == 0) {
            strcpy_s(box->buttons[0], "OK", 32);
            box->button_count = 1;
        } else {
            box->button_count = idx;
        }
    }

    /* Measure message text (multiline) */
    char lines[8][256];
    int line_count = text_split_lines(box->message, lines, 8);
    int max_line_w = 0;

    for (int i = 0; i < line_count; i++) {
        int w = bmf_measure_text(&font_n, 12, lines[i]);
        if (w > max_line_w)
            max_line_w = w;
    }

    /* Compute button sizes */
    const int BTN_SPACING = 6;
    const int MIN_BTN_W   = 65;

    int total_btn_w = 0;

    for (int i = 0; i < box->button_count; i++) {
        int tw = bmf_measure_text(&font_b, 12, box->buttons[i]);
        int bw = tw + 16;
        if (bw < MIN_BTN_W) bw = MIN_BTN_W;
        box->button_w[i] = bw;
        box->button_h = 22;
        total_btn_w += bw;
    }

    total_btn_w += (box->button_count - 1) * BTN_SPACING;

    const int MARGIN_LEFT = 10;
    const int MARGIN_RIGHT = 10;
    const int MARGIN_TOP = 10;
    const int MARGIN_BOTTOM = 8;
    const int ICON_TEXT_GAP = 10;
    const int TEXT_BUTTON_GAP = 12;
    const int TITLEBAR_H = 24;

    int has_icon = box->icon[0] ? 1 : 0;

    /* Calculate content area width */
    int text_content_w = max_line_w;
    int min_content_w = total_btn_w;

    int content_w;
    if (has_icon) {
        if (line_count == 1) {
            /* Single line: icon + gap + text, minimum for buttons */
            content_w = WM_ICON_SIZE + ICON_TEXT_GAP + text_content_w;
            if (content_w < min_content_w)
                content_w = min_content_w;
        } else {
            /* Multiline: give text reasonable width, icon beside it */
            int text_area_w = text_content_w;
            if (text_area_w < 160) text_area_w = 160;
            if (text_area_w > 280) text_area_w = 280;
            content_w = WM_ICON_SIZE + ICON_TEXT_GAP + text_area_w;
            if (content_w < min_content_w)
                content_w = min_content_w;
        }
    } else {
        /* No icon: just text width */
        if (line_count == 1) {
            content_w = text_content_w;
        } else {
            /* Multiline: reasonable width */
            content_w = text_content_w;
            if (content_w < 160) content_w = 160;
            if (content_w > 280) content_w = 280;
        }
        if (content_w < min_content_w)
            content_w = min_content_w;
    }

    int win_w = MARGIN_LEFT + content_w + MARGIN_RIGHT;
    if (win_w < 160) win_w = 160;
    if (win_w > 400) win_w = 400;

    /* Height calculation */
    int line_h = text_measure_height("AQYJaqpjy129", &font_n, 12);
    int text_h = line_h * line_count + (line_count > 1 ? 2 : 0);

    /* Content height: max of icon or text */
    int content_h;
    if (has_icon) {
        int icon_h = WM_ICON_SIZE;
        content_h = (text_h > icon_h) ? text_h : icon_h;
    } else {
        content_h = text_h;
    }

    int win_h = TITLEBAR_H + MARGIN_TOP + content_h + TEXT_BUTTON_GAP + box->button_h + MARGIN_BOTTOM;

    int win_x = (WM_SCREEN_WIDTH  - win_w) / 2;
    int win_y = (WM_SCREEN_HEIGHT - win_h) / 2;

    win_create(&box->base, win_x, win_y, win_w, win_h, title);
    box->base.resizable = 0;  /* Message boxes are not resizable */

    /* Button positions - centered at bottom */
    int start_x = (win_w - total_btn_w) / 2;
    int cur_x   = start_x;

    for (int i = 0; i < box->button_count; i++) {
        box->button_x[i] = cur_x;
        box->button_y    = win_h - MARGIN_BOTTOM - box->button_h;
        cur_x += box->button_w[i] + BTN_SPACING;
    }
}

void win_msgbox_draw(msgbox_t *box) {
    win_draw(&box->base);

    const int MARGIN_LEFT = 10;
    const int MARGIN_TOP = 10;
    const int ICON_TEXT_GAP = 10;
    const int TITLEBAR_H = 24;

    int has_icon = box->icon[0] ? 1 : 0;

    /* Split lines for text measurement */
    char lines[8][256];
    int line_count = text_split_lines(box->message, lines, 8);
    int line_h = text_measure_height("AQYJaqpjy129", &font_n, 12);
    int text_block_h = line_h * line_count + (line_count > 1 ? 2 : 0);

    int content_y = box->base.y + TITLEBAR_H + MARGIN_TOP;

    /* Draw icon if present */
    int icon_x = 0;
    int icon_y = 0;

    if (has_icon) {
        icon_x = box->base.x + MARGIN_LEFT;
        
        /* Icon vertical centering */
        int content_h = (text_block_h > WM_ICON_SIZE) ? text_block_h : WM_ICON_SIZE;
        icon_y = content_y + (content_h - WM_ICON_SIZE) / 2;

        int is_path = contains_any(box->icon, ":/\\.") ? 1 : 0;
        int drawn = 0;

        if (is_path) {
            gui_control_t pic = {0};
            pic.type = CTRL_PICTUREBOX;
            pic.w = WM_ICON_SIZE;
            pic.h = WM_ICON_SIZE;
            pic.bg = -1;
            strcpy_s(pic.text, box->icon, sizeof(pic.text));
            ctrl_draw_picturebox(&pic, icon_x, icon_y);
            drawn = 1;
        } else {
            char token[128];
            int j = 0;

            while (box->icon[j] && j < (int)sizeof(token) - 10) {
                char c = box->icon[j];
                if (c >= 'a' && c <= 'z') c -= 32;
                token[j++] = c;
            }
            token[j] = '\0';

            if (j > 0) {
                char path[160];
                snprintf(path, sizeof(path), "C:/ICONS/%s.ICO", token);
                bitmap_t *bmp = bitmap_load_from_file(path);
                if (bmp) {
                    bitmap_draw(bmp, icon_x, icon_y);
                    bitmap_free(bmp);
                    drawn = 1;
                }
            }

            if (!drawn) {
                /* Fallback placeholder circle */
                gfx_circle(icon_x + WM_ICON_SIZE/2, icon_y + WM_ICON_SIZE/2, WM_ICON_SIZE/2 - 2, 7);
            }
        }
    }

    /* Calculate text area */
    int text_x;
    
    if (has_icon) {
        text_x = box->base.x + MARGIN_LEFT + WM_ICON_SIZE + ICON_TEXT_GAP;
    } else {
        text_x = box->base.x + MARGIN_LEFT;
    }

    /* Text vertical positioning */
    int text_y;
    
    if (has_icon) {
        /* Center text vertically relative to icon */
        int content_h = (text_block_h > WM_ICON_SIZE) ? text_block_h : WM_ICON_SIZE;
        text_y = content_y + (content_h - text_block_h) / 2;
    } else {
        /* Center text in the content area */
        text_y = content_y;
    }

    /* Draw text lines */
    for (int i = 0; i < line_count; i++) {
        int lx;
        
        if (line_count == 1) {
            /* Single line: left-align in text area */
            lx = text_x;
        } else {
            /* Multiline: left-align for better readability */
            lx = text_x;
        }

        bmf_printf(
            lx,
            text_y + 5 + i * (line_h + (line_count > 1 ? 1 : 0)),
            &font_n,
            12,
            0,
            "%s",
            lines[i]
        );
    }

    /* Draw buttons */
    for (int i = 0; i < box->button_count; i++) {
        gui_control_t btn = {0};
        btn.type = CTRL_BUTTON;
        btn.w    = box->button_w[i];
        btn.h    = box->button_h;
        btn.bg   = -1;
        btn.font_size = 12;
        btn.font_type = 1;
        strcpy_s(btn.text, box->buttons[i], sizeof(btn.text));

        ctrl_draw_button(
            &btn,
            box->base.x + box->button_x[i],
            box->base.y + box->button_y
        );
    }
}

int win_msgbox_handle_click(msgbox_t *box, int mx, int my) {
    for (int i = 0; i < box->button_count; i++) {
        int abs_x = box->base.x + box->button_x[i];
        int abs_y = box->base.y + box->button_y;
        if (mx >= abs_x && mx < abs_x + box->button_w[i] &&
            my >= abs_y && my < abs_y + box->button_h) {
            return i + 1; /* return 1-based index of clicked button */
        }
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
    extern int buffer_valid;
    if (buffer_valid) {
        mouse_restore();
        mouse_invalidate_buffer();
    }

    win_restore_background(win);
    win->x += dx;
    win->y += dy;

    /* Keep window on screen */
    if (win->x < 0) win->x = 0;
    if (win->y < 0) win->y = 0;
    if (win->x + win->w > WM_SCREEN_WIDTH) win->x = WM_SCREEN_WIDTH - win->w;
    if (win->y + win->h > WM_SCREEN_HEIGHT) win->y = WM_SCREEN_HEIGHT - win->h;

    win_save_background(win);
    win->dirty = 1;
}

void win_resize(window_t *win, int new_w, int new_h) {
    /* Restore cursor before any window operations to prevent artifacts */
    extern int buffer_valid;
    if (buffer_valid) {
        mouse_restore();
        mouse_invalidate_buffer();
    }

    win_restore_background(win);
    
    /* Apply minimum size constraints */
    if (new_w < 100) new_w = 100;
    if (new_h < 80) new_h = 80;
    
    /* Keep window on screen */
    if (win->x + new_w > WM_SCREEN_WIDTH) new_w = WM_SCREEN_WIDTH - win->x;
    if (win->y + new_h > WM_SCREEN_HEIGHT) new_h = WM_SCREEN_HEIGHT - win->y;
    
    /* Free old saved_bg since size is changing */
    if (win->saved_bg) {
        kfree(win->saved_bg);
        win->saved_bg = NULL;
    }
    
    win->w = new_w;
    win->h = new_h;
    
    win_save_background(win);
    win->dirty = 1;
}

void win_save_background(window_t *win) {
    if (!win->is_visible) return;

    int margin = WM_BG_MARGIN;
    int w = win->w + 2 * margin;
    int h = win->h + 2 * margin;
    int row_bytes = (w + 1) / 2; /* packed 2 pixels per byte */
    int buf_size = row_bytes * h;

    if (win->saved_bg) {
        kfree(win->saved_bg);
    }
    win->saved_bg = kmalloc(buf_size);
    if (!win->saved_bg) return;

    /* Fast path: fully on-screen and byte-aligned (win->x - margin even) */
    int sx = win->x - margin;
    int sy = win->y - margin;
    if (sx >= 0 && sy >= 0 && sx + w <= WM_SCREEN_WIDTH && sy + h <= WM_SCREEN_HEIGHT && (sx & 1) == 0) {
        gfx_read_screen_region_packed(win->saved_bg, w, h, sx, sy);
        return;
    }

    /* Fallback: safe per-pixel read and pack into saved buffer */
    for (int py = 0; py < h; py++) {
        int sy_row = win->y + py - margin;
        uint8_t *dst_row = win->saved_bg + py * row_bytes;
        /* init to 0 */
        for (int b = 0; b < row_bytes; b++) dst_row[b] = 0;

        for (int px = 0; px < w; px++) {
            int sx_col = win->x + px - margin;
            uint8_t pix = 0;
            if (sx_col >= 0 && sx_col < WM_SCREEN_WIDTH && sy_row >= 0 && sy_row < WM_SCREEN_HEIGHT) {
                pix = gfx_getpixel(sx_col, sy_row);
            }
            int byte_idx = px / 2;
            if (px & 1) {
                dst_row[byte_idx] = (dst_row[byte_idx] & 0xF0) | (pix & 0x0F);
            } else {
                dst_row[byte_idx] = (dst_row[byte_idx] & 0x0F) | (pix << 4);
            }
        }
    }
}

void win_restore_background(window_t *win) {
    if (!win->saved_bg) return;

    int margin = WM_BG_MARGIN;
    int w = win->w + 2 * margin;
    int h = win->h + 2 * margin;
    int row_bytes = (w + 1) / 2;

    int sx = win->x - margin;
    int sy = win->y - margin;
    /* Fast path: fully on-screen and byte-aligned */
    if (sx >= 0 && sy >= 0 && sx + w <= WM_SCREEN_WIDTH && sy + h <= WM_SCREEN_HEIGHT && (sx & 1) == 0) {
        gfx_write_screen_region_packed(win->saved_bg, w, h, sx, sy);
        return;
    }

    /* Fallback: per-pixel unpack and write */
    for (int py = 0; py < h; py++) {
        int sy_row = win->y + py - margin;
        uint8_t *src_row = win->saved_bg + py * row_bytes;

        for (int px = 0; px < w; px++) {
            int sx_col = win->x + px - margin;
            if (sx_col < 0 || sx_col >= WM_SCREEN_WIDTH || sy_row < 0 || sy_row >= WM_SCREEN_HEIGHT) continue;
            int byte_idx = px / 2;
            uint8_t packed = src_row[byte_idx];
            uint8_t pix = (px & 1) ? (packed & 0x0F) : (packed >> 4);
            gfx_putpixel(sx_col, sy_row, pix);
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

int win_is_resize_corner(window_t *win, int mx, int my) {
    /* Don't allow resize if window is not resizable */
    if (!win->resizable) return 0;
    
    /* Bottom-right corner resize area (12x12 pixels) */
    int corner_x = win->x + win->w - 12;
    int corner_y = win->y + win->h - 12;
    
    if (mx >= corner_x && mx < win->x + win->w &&
        my >= corner_y && my < win->y + win->h) {
        return 1;
    }
    return 0;
}

void win_minimize(struct gui_form_s *form, int icon_x, int icon_y, const char *icon_path) {
    window_t *win = &form->win;
    if (win->is_minimized) return;

    mouse_invalidate_buffer();
    win_restore_background(win);

    if (win->saved_bg) {
        kfree(win->saved_bg);
        win->saved_bg = NULL;
    }

    win->is_minimized = 1;

    /* Check if form already has an icon control from sys_win_set_icon */
    #define FORM_ICON_CONTROL_ID 5000
    gui_control_t *existing_icon = NULL;
    if (form->controls) {
        for (int i = 0; i < form->ctrl_count; i++) {
            /* Check for CTRL_ICON with or without hidden flag (0x80) */
            if ((form->controls[i].type & 0x7F) == CTRL_ICON && form->controls[i].id == FORM_ICON_CONTROL_ID) {
                existing_icon = &form->controls[i];
                break;
            }
        }
    }

    if (win->minimized_icon_id == -1) {
        if (existing_icon) {
            /* Use existing icon control from sys_win_set_icon - clear hidden flag */
            existing_icon->type = CTRL_ICON;
            win->minimized_icon_id = FORM_ICON_CONTROL_ID;
            ctrl_set_pos(form, FORM_ICON_CONTROL_ID, icon_x, icon_y);
            /* Use form's stored icon path */
            if (form->icon_path[0]) {
                ctrl_set_image(form, FORM_ICON_CONTROL_ID, form->icon_path);
            }
        } else {
            /* Create new icon control */
            gui_control_t icon_ctrl = {0};
            icon_ctrl.type = CTRL_ICON;
            icon_ctrl.id = form->ctrl_count;
            icon_ctrl.x = icon_x;
            icon_ctrl.y = icon_y;
            icon_ctrl.w = WM_ICON_TOTAL_WIDTH;
            icon_ctrl.h = 0;
            icon_ctrl.fg = 0;
            icon_ctrl.bg = 15;
            for (size_t i = 0; i < sizeof(icon_ctrl.text) && win->title[i]; i++) {
                icon_ctrl.text[i] = win->title[i];
            }
            icon_ctrl.text[sizeof(icon_ctrl.text) - 1] = '\0';
            sys_win_add_control(form, &icon_ctrl);
            win->minimized_icon_id = icon_ctrl.id;
            if (icon_path) {
                ctrl_set_image(form, icon_ctrl.id, icon_path);
            }
        }
    } else {
        /* Icon control already exists - move to new position */
        gui_control_t *ctrl = sys_win_get_control(form, win->minimized_icon_id);
        if (ctrl) {
            /* Clear selection */
            ctrl->icon.checked = 0;
            /* Update position */
            ctrl_set_pos(form, win->minimized_icon_id, icon_x, icon_y);
            /* Just clear saved_bg - will be re-saved on next draw */
            if (ctrl->icon.saved_bg) {
                kfree(ctrl->icon.saved_bg);
                ctrl->icon.saved_bg = NULL;
            }
        }
    }
}

void win_restore(struct gui_form_s *form) {
    window_t *win = &form->win;
    if (!win->is_minimized) return;

    /* Invalidate cursor buffer since window is appearing */
    mouse_invalidate_buffer();

    /* Clear the minimized state and reset position if it's a non-form icon */
    #define FORM_ICON_CONTROL_ID 5000
    if (win->minimized_icon_id != FORM_ICON_CONTROL_ID && win->minimized_icon_id != -1 && form->controls) {
        /* Remove dynamically created minimized icon controls */
        for (int i = 0; i < form->ctrl_count; i++) {
            if (form->controls[i].id == win->minimized_icon_id) {
                /* Free any allocated resources */
                if (form->controls[i].icon.cached_bitmap_orig) {
                    bitmap_free(form->controls[i].icon.cached_bitmap_orig);
                    form->controls[i].icon.cached_bitmap_orig = NULL;
                }
                if (form->controls[i].icon.saved_bg) {
                    kfree(form->controls[i].icon.saved_bg);
                    form->controls[i].icon.saved_bg = NULL;
                }
                /* Shift remaining controls down */
                for (int j = i; j < form->ctrl_count - 1; j++) {
                    form->controls[j] = form->controls[j + 1];
                }
                form->ctrl_count--;
                break;
            }
        }
        win->minimized_icon_id = -1;
    } else if (win->minimized_icon_id == FORM_ICON_CONTROL_ID) {
        /* For form icon, just clear selection, keep control on form */
        gui_control_t *ctrl = sys_win_get_control(form, FORM_ICON_CONTROL_ID);
        if (ctrl) {
            ctrl->icon.checked = 0;
            /* Restore hidden flag so it's not visible in the window */
            ctrl->type = CTRL_ICON | 0x80;
            /* Invalidate saved_bg so next minimize saves new position */
            if (ctrl->icon.saved_bg) {
                kfree(ctrl->icon.saved_bg);
                ctrl->icon.saved_bg = NULL;
            }
        }
        /* Reset position to default for re-minimize */
        ctrl_set_pos(form, FORM_ICON_CONTROL_ID, -100, -100);
        win->minimized_icon_id = -1;
    }

    win->is_minimized = 0;
    win->dirty = 1;

    win_save_background(win);
}

void win_maximize(struct gui_form_s *form) {
    window_t *win = &form->win;
    if (!win->resizable || win->is_maximized) return;

    win->saved_x = win->x;
    win->saved_y = win->y;
    win->saved_w = win->w;
    win->saved_h = win->h;

    if (win->saved_bg) {
        kfree(win->saved_bg);
        win->saved_bg = NULL;
    }

    win->x = 0;
    win->y = 0;
    win->w = WM_SCREEN_WIDTH;
    win->h = WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT;

    win->is_maximized = 1;
    win->dirty = 1;

    mouse_invalidate_buffer();
}

void win_restore_from_maximize(struct gui_form_s *form) {
    window_t *win = &form->win;
    if (!win->is_maximized) return;

    win->x = win->saved_x;
    win->y = win->saved_y;
    win->w = win->saved_w;
    win->h = win->saved_h;

    win->is_maximized = 0;
    win->dirty = 1;

    mouse_invalidate_buffer();
}

int win_is_icon_clicked(struct gui_form_s *form, int mx, int my) {
    window_t *win = &form->win;
    if (!win->is_minimized) return 0;
    
    if (win->minimized_icon_id != -1 && form->controls) {
        gui_control_t *ctrl = sys_win_get_control(form, win->minimized_icon_id);
        if (ctrl) {
            int total_w = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
            int label_lines = icon_count_label_lines(ctrl->text, 49);
            int total_h = icon_calc_total_height(32, label_lines);
            if (mx >= ctrl->x && mx < ctrl->x + total_w &&
                my >= ctrl->y && my < ctrl->y + total_h) {
                return 1;
            }
        }
    }
    return 0;
}