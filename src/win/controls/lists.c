#include "internal.h"

static int treeview_item_visible(gui_control_t *control, int idx) {
    if (!control || !control->treeview.items || idx < 0 || idx >= control->treeview.item_count)
        return 0;

    int level = control->treeview.items[idx].level;
    for (int i = idx - 1; i >= 0 && level > 0; i--) {
        sys_tree_item_t *item = &control->treeview.items[i];
        if (item->level < level) {
            if (!(item->flags & TREE_ITEM_EXPANDED))
                return 0;
            level = item->level;
        }
    }
    return 1;
}

static int treeview_visible_count(gui_control_t *control) {
    int count = 0;
    if (!control || !control->treeview.items) return 0;

    for (int i = 0; i < control->treeview.item_count; i++) {
        if (treeview_item_visible(control, i))
            count++;
    }
    return count;
}

static int treeview_visible_to_item(gui_control_t *control, int visible_index) {
    int visible = 0;
    if (!control || !control->treeview.items || visible_index < 0) return -1;

    for (int i = 0; i < control->treeview.item_count; i++) {
        if (!treeview_item_visible(control, i))
            continue;
        if (visible == visible_index)
            return i;
        visible++;
    }
    return -1;
}

#define TREEVIEW_INDENT_W 10

static int treeview_max_item_width(gui_control_t *control, bmf_font_t *font, int size) {
    int max_w = 1;
    if (!control || !control->treeview.items) return max_w;

    for (int i = 0; i < control->treeview.item_count; i++) {
        if (!treeview_item_visible(control, i))
            continue;
        int text_w = font && font->data ? bmf_measure_text(font, size, control->treeview.items[i].text) : 64;
        int row_w = 36 + (control->treeview.items[i].level * TREEVIEW_INDENT_W) + text_w;
        if (row_w > max_w)
            max_w = row_w;
    }
    return max_w;
}

static void treeview_draw_fallback_folder(int x, int y, int open) {
    uint8_t fill = open ? COLOR_YELLOW : COLOR_BROWN;
    gfx_fillrect(x + 1, y + 5, 14, 10, fill);
    gfx_fillrect(x + 2, y + 3, 6, 3, fill);
    gfx_rect(x + 1, y + 5, 14, 10, COLOR_BLACK);
    gfx_hline(x + 2, y + 6, 12, COLOR_YELLOW);
}

void treeview_draw_text_clipped(int x, int y, bmf_font_t *font, int size,
                                const char *text, uint8_t color, int min_x, int max_x) {
    if (!font || !font->data || !text) return;

    int tx = x;
    for (int i = 0; text[i]; i++) {
        const bmf_glyph_t *g = bmf_get_glyph(font, size, (uint8_t)text[i]);
        int char_w = g ? g->width : 6;
        if (tx + char_w > max_x)
            break;
        if (g && tx + char_w > min_x && tx < max_x)
            bmf_draw_char(tx, y, font, size, (uint8_t)text[i], color);
        tx += char_w;
    }
}

static int treeview_valid_icon(bitmap_t *bmp) {
    return bmp && bmp->data && bmp->width > 0 && bmp->height > 0 &&
           bmp->width <= 32 && bmp->height <= 32;
}

void ctrl_draw_treeview(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;
    int row_h = control->treeview.row_height ? control->treeview.row_height : 18;
    int sb_w = 18;
    int hsb_h = 18;

    gfx_fillrect(abs_x, abs_y, control->w, control->h, COLOR_WHITE);
    gfx_hline(abs_x, abs_y, control->w, theme->frame_dark);
    gfx_vline(abs_x, abs_y, control->h, theme->frame_dark);
    gfx_hline(abs_x + 1, abs_y + control->h - 1, control->w - 1, theme->frame_light);
    gfx_vline(abs_x + control->w - 1, abs_y, control->h, theme->frame_light);
    gfx_hline(abs_x + 1, abs_y + 1, control->w - 2, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 2, COLOR_DARK_GRAY);

    if (!control->treeview.items || control->treeview.item_count == 0)
        return;

    int inner_x = abs_x + 2;
    int inner_y = abs_y + 2;
    int inner_w = control->w - 4;
    int inner_h = control->h - 4;
    int total_visible = treeview_visible_count(control);
    int content_needed = treeview_max_item_width(control, font, size);
    int needs_hscrollbar = 0;
    int needs_scrollbar = 0;
    int visible_rows = 1;
    int content_w = inner_w;
    int row_area_h = inner_h;

    for (int pass = 0; pass < 3; pass++) {
        content_w = inner_w - (needs_scrollbar ? sb_w : 0);
        row_area_h = inner_h - (needs_hscrollbar ? hsb_h : 0);
        if (content_w < 1) content_w = 1;
        if (row_area_h < row_h) row_area_h = row_h;
        visible_rows = row_area_h / row_h;
        if (visible_rows < 1) visible_rows = 1;
        needs_hscrollbar = content_needed > content_w;
        needs_scrollbar = total_visible > visible_rows;
    }

    int max_scroll = total_visible > visible_rows ? total_visible - visible_rows : 0;
    int max_hscroll = content_needed > content_w ? content_needed - content_w : 0;
    if (control->treeview.scroll_offset > max_scroll)
        control->treeview.scroll_offset = max_scroll;
    if (control->treeview.hscroll_offset > max_hscroll)
        control->treeview.hscroll_offset = max_hscroll;
    control->treeview.content_width = content_needed;

    if (!control->treeview.icon_closed && !control->treeview.icon_closed_failed) {
        const char *path = control->treeview.icon_closed_path[0] ? control->treeview.icon_closed_path : "C:/ICONS/FLD.ICO";
        control->treeview.icon_closed = bitmap_load_from_file(path);
        if (!treeview_valid_icon(control->treeview.icon_closed)) {
            if (control->treeview.icon_closed) {
                bitmap_free(control->treeview.icon_closed);
                control->treeview.icon_closed = NULL;
            }
            control->treeview.icon_closed_failed = 1;
        }
    }
    if (!control->treeview.icon_open && !control->treeview.icon_open_failed) {
        const char *path = control->treeview.icon_open_path[0] ? control->treeview.icon_open_path : "C:/ICONS/FLO.ICO";
        control->treeview.icon_open = bitmap_load_from_file(path);
        if (!treeview_valid_icon(control->treeview.icon_open)) {
            if (control->treeview.icon_open) {
                bitmap_free(control->treeview.icon_open);
                control->treeview.icon_open = NULL;
            }
            control->treeview.icon_open_failed = 1;
        }
    }

    for (int row = 0; row < visible_rows; row++) {
        int item_idx = treeview_visible_to_item(control, control->treeview.scroll_offset + row);
        if (item_idx < 0)
            break;

        sys_tree_item_t *item = &control->treeview.items[item_idx];
        int row_y = inner_y + row * row_h;
        int selected = (item_idx == control->treeview.selected_index);

        if (selected && content_w > 0)
            gfx_fillrect(inner_x, row_y, content_w + 1, row_h, COLOR_BLUE);

        int indent = item->level * TREEVIEW_INDENT_W;
        int box_x = inner_x + 3 + indent - control->treeview.hscroll_offset;
        int box_y = row_y + (row_h - 9) / 2;

        if ((item->flags & TREE_ITEM_HAS_CHILDREN) &&
            box_x >= inner_x && box_x + 9 <= inner_x + content_w) {
            gfx_fillrect(box_x, box_y, 9, 9, COLOR_WHITE);
            gfx_rect(box_x, box_y, 9, 9, COLOR_BLACK);
            gfx_hline(box_x + 2, box_y + 4, 5, COLOR_BLACK);
            if (!(item->flags & TREE_ITEM_EXPANDED))
                gfx_vline(box_x + 4, box_y + 2, 5, COLOR_BLACK);
        }

        int icon_x = box_x + 12;
        int icon_y = row_y + (row_h - 16) / 2;
        if (icon_y < row_y) icon_y = row_y;

        if (item->flags & TREE_ITEM_FOLDER) {
            int open_icon = selected;
            bitmap_t *icon = open_icon ? control->treeview.icon_open : control->treeview.icon_closed;
            if (icon && icon_x >= inner_x && icon_x + icon->width <= inner_x + content_w)
                bitmap_draw(icon, icon_x, icon_y);
            else if (icon_x >= inner_x && icon_x + 16 <= inner_x + content_w)
                treeview_draw_fallback_folder(icon_x, icon_y, open_icon);
        }

        int text_x = icon_x + 18;
        int text_y = row_y + (row_h - 12) / 2 + 3;
        uint8_t text_color = selected ? COLOR_WHITE : control->fg;
        treeview_draw_text_clipped(text_x, text_y, font, size, item->text,
                                   text_color, inner_x + 1, inner_x + content_w - 2);
    }

    if (needs_scrollbar && row_area_h > sb_w * 2) {
        gui_control_t sb = {0};
        sb.type = CTRL_SCROLLBAR;
        sb.w = sb_w;
        sb.h = row_area_h;
        sb.scrollbar.checked = 0;
        sb.scrollbar.cursor_pos = control->treeview.scroll_offset;
        sb.scrollbar.max_length = max_scroll;
        sb.scrollbar.hovered_item = control->treeview.scrollbar_hovered_item;
        sb.scrollbar.pressed = control->treeview.scrollbar_pressed;
        ctrl_draw_scrollbar(&sb, abs_x + control->w - sb_w - 1, inner_y);
    }

    if (needs_hscrollbar && content_w > hsb_h * 2) {
        gui_control_t sb = {0};
        sb.type = CTRL_SCROLLBAR;
        sb.w = content_w;
        sb.h = hsb_h;
        sb.scrollbar.checked = 1;
        sb.scrollbar.cursor_pos = control->treeview.hscroll_offset;
        sb.scrollbar.max_length = max_hscroll;
        sb.scrollbar.hovered_item = control->treeview.hscrollbar_hovered_item;
        sb.scrollbar.pressed = control->treeview.hscrollbar_pressed;
        ctrl_draw_scrollbar(&sb, inner_x, abs_y + control->h - hsb_h - 1);
        if (needs_scrollbar)
            gfx_fillrect(abs_x + control->w - sb_w - 1, abs_y + control->h - hsb_h - 1,
                         sb_w, hsb_h, theme->button_color);
    }
}

void ctrl_draw_listbox(gui_control_t *control, int abs_x, int abs_y) {
    window_theme_t *theme = theme_get_current();
    bmf_font_t *font = &font_n;
    int size = control->font_size > 0 ? control->font_size : 12;
    int row_h = control->listbox.row_height ? control->listbox.row_height : 16;
    int sb_w = 18;

    gfx_fillrect(abs_x, abs_y, control->w, control->h, COLOR_WHITE);
    gfx_hline(abs_x, abs_y, control->w, theme->frame_dark);
    gfx_vline(abs_x, abs_y, control->h, theme->frame_dark);
    gfx_hline(abs_x + 1, abs_y + control->h - 1, control->w - 1, theme->frame_light);
    gfx_vline(abs_x + control->w - 1, abs_y, control->h, theme->frame_light);
    gfx_hline(abs_x + 1, abs_y + 1, control->w - 2, COLOR_DARK_GRAY);
    gfx_vline(abs_x + 1, abs_y + 1, control->h - 2, COLOR_DARK_GRAY);

    if (!control->listbox.items || control->listbox.item_count == 0)
        return;

    int inner_x = abs_x + 2;
    int inner_y = abs_y + 2;
    int inner_w = control->w > 4 ? control->w - 4 : control->w;
    int inner_h = control->h > 4 ? control->h - 4 : control->h;
    int visible_rows = row_h > 0 ? inner_h / row_h : 1;
    if (visible_rows < 1) visible_rows = 1;
    int needs_scrollbar = control->listbox.item_count > visible_rows;
    int content_w = inner_w - (needs_scrollbar ? sb_w : 0);
    if (content_w < 1) content_w = 1;
    int max_scroll = control->listbox.item_count > visible_rows
                   ? control->listbox.item_count - visible_rows : 0;

    if (control->listbox.scroll_offset > max_scroll)
        control->listbox.scroll_offset = max_scroll;

    for (int row = 0; row < visible_rows; row++) {
        int item_idx = control->listbox.scroll_offset + row;
        if (item_idx < 0 || item_idx >= control->listbox.item_count)
            break;

        sys_list_item_t *item = &control->listbox.items[item_idx];
        int row_y = inner_y + row * row_h;
        int selected = (item_idx == control->listbox.selected_index);

        if (selected && content_w > 0)
            gfx_fillrect(inner_x, row_y, content_w + 1, row_h, COLOR_BLUE);

        int text_x = inner_x + 4;
        int text_y = row_y + (row_h - 12) / 2 + 3;
        uint8_t text_color = selected ? COLOR_WHITE : control->fg;
        treeview_draw_text_clipped(text_x, text_y, font, size, item->text,
                                   text_color, inner_x + 1, inner_x + content_w - 2);
    }

    if (needs_scrollbar && inner_h > sb_w * 2) {
        gui_control_t sb = {0};
        sb.type = CTRL_SCROLLBAR;
        sb.w = sb_w;
        sb.h = inner_h;
        sb.scrollbar.checked = 0;
        sb.scrollbar.cursor_pos = control->listbox.scroll_offset;
        sb.scrollbar.max_length = max_scroll;
        sb.scrollbar.hovered_item = control->listbox.scrollbar_hovered_item;
        sb.scrollbar.pressed = control->listbox.scrollbar_pressed;
        ctrl_draw_scrollbar(&sb, abs_x + control->w - sb_w - 1, inner_y);
    }
}
