#include "internal.h"

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

    gfx_fillrect(abs_x, abs_y, field_w, control->h, COLOR_WHITE);

    gfx_hline(abs_x, abs_y, field_w, theme->frame_dark);
    gfx_vline(abs_x, abs_y, control->h, theme->frame_dark);
    gfx_hline(abs_x+1, abs_y + control->h - 1, field_w, theme->frame_light);

    gfx_hline(abs_x + 1, abs_y + 1, field_w - 1, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 1, COLOR_DARK_GRAY);

    int btn_x = abs_x + field_w;
    gfx_fillrect(btn_x, abs_y, btn_w, control->h, theme->button_color);

    gfx_rect(btn_x, abs_y, btn_w, control->h, COLOR_BLACK);
    int btn_pressed = control->dropdown.pressed || control->dropdown.dropdown_open;
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

    int arrow_x = btn_x + btn_w / 2;
    int arrow_y = abs_y + control->h / 2;
    if (control->dropdown.dropdown_open) {
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_x - 3 + i, arrow_y - i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_x - 1, arrow_y + 1, 3, 3, COLOR_BLACK);
    } else {
        for (int i = 0; i < 4; i++) {
            gfx_hline(arrow_x - 3 + i, arrow_y + i, 7 - i * 2, COLOR_BLACK);
        }
        gfx_fillrect(arrow_x - 1, arrow_y - 3, 3, 3, COLOR_BLACK);
    }

    if (font->data) {
        char item_text[64];
        int selected = control->dropdown.cursor_pos;
        dropdown_get_item(control->text, selected, item_text, sizeof(item_text));

        int text_x = abs_x + 4;
        int text_y = abs_y + (control->h - 12) / 2 + 3;
        bmf_printf(text_x, text_y, font, size, control->fg, "%s", item_text);
    }
}

void ctrl_draw_dropdown_list(window_t *win, gui_control_t *control, int y_offset) {
    if (!control->dropdown.dropdown_open) return;

    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;

    int abs_x = win->x + control->x;
    int abs_y = win->y + control->y + y_offset;

    int item_count = dropdown_count_items(control->text);
    int item_h = 16;
    int list_h = item_count * item_h;
    int list_y = abs_y + control->h;

    if (list_y + list_h > GFX_HEIGHT) {
        list_y = abs_y - list_h;
        if (list_y < 0) {
            list_y = 0;
            list_h = abs_y;
            if (list_h < item_h) list_h = item_h;
        }
    }

    int visible_count = list_h / item_h;
    if (visible_count < 1) visible_count = 1;
    int need_scrollbar = item_count > visible_count;
    int sb_w = need_scrollbar ? 18 : 0;

    int max_scroll = item_count > visible_count ? (item_count - visible_count) : 0;
    if (control->dropdown.dropdown_scroll > (uint16_t)max_scroll) control->dropdown.dropdown_scroll = max_scroll;

    /* Dropdown lists can extend outside the parent window, so they save their own background. */
    if (!control->dropdown.dropdown_saved_bg) {
        control->dropdown.dropdown_saved_w = control->w;
        control->dropdown.dropdown_saved_h = list_h;
        control->dropdown.dropdown_saved_x = abs_x;
        control->dropdown.dropdown_saved_y = list_y;
        int row_bytes = (control->dropdown.dropdown_saved_w + 1) / 2;
        control->dropdown.dropdown_saved_bg = kmalloc(row_bytes * control->dropdown.dropdown_saved_h);
        if (control->dropdown.dropdown_saved_bg) {
            gfx_read_screen_region_packed(control->dropdown.dropdown_saved_bg,
                                          control->dropdown.dropdown_saved_w,
                                          control->dropdown.dropdown_saved_h,
                                          control->dropdown.dropdown_saved_x,
                                          control->dropdown.dropdown_saved_y);
        }
    }

    gfx_fillrect(abs_x, list_y, control->w, list_h, COLOR_WHITE);
    gfx_rect(abs_x, list_y, control->w, list_h, theme->frame_dark);

    for (int vi = 0; vi < visible_count; vi++) {
        int i = control->dropdown.dropdown_scroll + vi;
        if (i >= item_count) break;

        char item_text[64];
        dropdown_get_item(control->text, i, item_text, sizeof(item_text));

        int item_y = list_y + vi * item_h;
        int content_w = control->w - sb_w;

        if (i == control->dropdown.cursor_pos) {
            gfx_fillrect(abs_x + 1, item_y, content_w - 2, item_h, COLOR_BLUE);
            if (font->data) bmf_printf(abs_x + 4, item_y + 3, font, size, COLOR_WHITE, "%s", item_text);
        } else if (i == control->dropdown.hovered_item) {
            gfx_fillrect(abs_x + 1, item_y, content_w - 2, item_h, 7);
            if (font->data) bmf_printf(abs_x + 4, item_y + 3, font, size, control->fg, "%s", item_text);
        } else {
            if (font->data) bmf_printf(abs_x + 4, item_y + 3, font, size, control->fg, "%s", item_text);
        }
    }

    if (need_scrollbar) {
        gui_control_t sb = {0};
        sb.type = CTRL_SCROLLBAR;
        sb.w = sb_w;
        sb.h = list_h;
        sb.scrollbar.checked = 0;
        sb.scrollbar.cursor_pos = control->dropdown.dropdown_scroll;
        sb.scrollbar.max_length = max_scroll > 0 ? max_scroll : 1;

        int mx = mouse_get_x();
        int my = mouse_get_y();
        int arrow_size = sb_w;
        int track_len = list_h - 2 * arrow_size;
        int thumb_size = 20;
        if (thumb_size > track_len) thumb_size = track_len;
        int thumb_pos = 0;
        if (sb.scrollbar.max_length > 0 && track_len > thumb_size) {
            thumb_pos = ((track_len - thumb_size) * sb.scrollbar.cursor_pos) / sb.scrollbar.max_length;
        }

        sb.scrollbar.hovered_item = -1;
        if (mx >= abs_x + control->w - sb_w && mx < abs_x + control->w && my >= list_y && my < list_y + list_h) {
            int rel = my - list_y;
            if (rel < arrow_size) sb.scrollbar.hovered_item = 0;
            else if (rel >= list_h - arrow_size) sb.scrollbar.hovered_item = 2;
            else {
                int thumb_y = arrow_size + thumb_pos;
                if (rel >= thumb_y && rel < thumb_y + thumb_size) sb.scrollbar.hovered_item = 1;
                else sb.scrollbar.hovered_item = -1;
            }
        }

        if (control->dropdown.pressed && control->dropdown.hovered_item == DROPDOWN_HOVER_SCROLLBAR) sb.scrollbar.pressed = 1;

        ctrl_draw_scrollbar(&sb, abs_x + control->w - sb_w, list_y);
    }
}


void ctrl_hide_dropdown_list(window_t *win, gui_control_t *control) {
    (void)win; /* unused: kept for API consistency */
    if (!control) return;

    if (control->dropdown.dropdown_saved_bg) {
        gfx_write_screen_region_packed(control->dropdown.dropdown_saved_bg,
                                       control->dropdown.dropdown_saved_w,
                                       control->dropdown.dropdown_saved_h,
                                       control->dropdown.dropdown_saved_x,
                                       control->dropdown.dropdown_saved_y);
        kfree(control->dropdown.dropdown_saved_bg);
        control->dropdown.dropdown_saved_bg = NULL;
        control->dropdown.dropdown_saved_w = 0;
        control->dropdown.dropdown_saved_h = 0;
        control->dropdown.dropdown_saved_x = 0;
        control->dropdown.dropdown_saved_y = 0;
    }

    control->dropdown.dropdown_open = 0;
    mouse_invalidate_buffer();
}
