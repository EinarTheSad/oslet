#include "gui.h"

static int treeview_item_visible_sys(gui_control_t *ctrl, int idx) {
    if (!ctrl || !ctrl->treeview.items || idx < 0 || idx >= ctrl->treeview.item_count)
        return 0;

    int level = ctrl->treeview.items[idx].level;
    for (int i = idx - 1; i >= 0 && level > 0; i--) {
        sys_tree_item_t *item = &ctrl->treeview.items[i];
        if (item->level < level) {
            if (!(item->flags & TREE_ITEM_EXPANDED))
                return 0;
            level = item->level;
        }
    }
    return 1;
}

static int treeview_visible_count_sys(gui_control_t *ctrl) {
    int count = 0;
    if (!ctrl || !ctrl->treeview.items) return 0;
    for (int i = 0; i < ctrl->treeview.item_count; i++) {
        if (treeview_item_visible_sys(ctrl, i))
            count++;
    }
    return count;
}

int treeview_visible_to_item_sys(gui_control_t *ctrl, int visible_index) {
    int visible = 0;
    if (!ctrl || !ctrl->treeview.items || visible_index < 0) return -1;
    for (int i = 0; i < ctrl->treeview.item_count; i++) {
        if (!treeview_item_visible_sys(ctrl, i))
            continue;
        if (visible == visible_index)
            return i;
        visible++;
    }
    return -1;
}

static int treeview_max_item_width_sys(gui_control_t *ctrl) {
    extern bmf_font_t font_n;
    int max_w = 1;
    if (!ctrl || !ctrl->treeview.items) return max_w;

    for (int i = 0; i < ctrl->treeview.item_count; i++) {
        if (!treeview_item_visible_sys(ctrl, i))
            continue;
        int text_w = font_n.data ? bmf_measure_text(&font_n, ctrl->font_size > 0 ? ctrl->font_size : 12,
                                                    ctrl->treeview.items[i].text) : 64;
        int row_w = 36 + (ctrl->treeview.items[i].level * TREEVIEW_INDENT_W) + text_w;
        if (row_w > max_w)
            max_w = row_w;
    }
    return max_w;
}

void treeview_layout_sys(gui_control_t *ctrl, int *out_content_w, int *out_row_area_h,
                         int *out_visible_rows, int *out_need_v, int *out_need_h,
                         int *out_max_v, int *out_max_h) {
    int row_h = ctrl->treeview.row_height ? ctrl->treeview.row_height : 18;
    int inner_w = ctrl->w > 4 ? ctrl->w - 4 : ctrl->w;
    int inner_h = ctrl->h > 4 ? ctrl->h - 4 : ctrl->h;
    int total_visible = treeview_visible_count_sys(ctrl);
    int content_needed = treeview_max_item_width_sys(ctrl);
    int need_v = 0;
    int need_h = 0;
    int content_w = inner_w;
    int row_area_h = inner_h;
    int visible_rows = 1;
    int sb = 18;

    for (int pass = 0; pass < 3; pass++) {
        content_w = inner_w - (need_v ? sb : 0);
        row_area_h = inner_h - (need_h ? sb : 0);
        if (content_w < 1) content_w = 1;
        if (row_area_h < row_h) row_area_h = row_h;
        visible_rows = row_h > 0 ? row_area_h / row_h : 1;
        if (visible_rows < 1) visible_rows = 1;
        need_h = content_needed > content_w;
        need_v = total_visible > visible_rows;
    }

    int max_v = total_visible > visible_rows ? total_visible - visible_rows : 0;
    int max_h = content_needed > content_w ? content_needed - content_w : 0;

    if (out_content_w) *out_content_w = content_w;
    if (out_row_area_h) *out_row_area_h = row_area_h;
    if (out_visible_rows) *out_visible_rows = visible_rows;
    if (out_need_v) *out_need_v = need_v;
    if (out_need_h) *out_need_h = need_h;
    if (out_max_v) *out_max_v = max_v;
    if (out_max_h) *out_max_h = max_h;
    ctrl->treeview.content_width = content_needed;
}

int treeview_max_scroll_sys(gui_control_t *ctrl) {
    int max_v = 0;
    treeview_layout_sys(ctrl, NULL, NULL, NULL, NULL, NULL, &max_v, NULL);
    return max_v;
}

static int treeview_item_to_visible_index_sys(gui_control_t *ctrl, int item_idx) {
    int visible = 0;
    if (!ctrl || !ctrl->treeview.items || item_idx < 0 || item_idx >= ctrl->treeview.item_count)
        return -1;

    for (int i = 0; i < ctrl->treeview.item_count; i++) {
        if (!treeview_item_visible_sys(ctrl, i))
            continue;
        if (i == item_idx)
            return visible;
        visible++;
    }

    return -1;
}

void treeview_keep_selected_visible_sys(gui_control_t *ctrl) {
    int visible_rows = 1;
    int max_v = 0;
    int max_h = 0;

    if (!ctrl || ctrl->type != CTRL_TREEVIEW || !ctrl->treeview.items)
        return;

    treeview_layout_sys(ctrl, NULL, NULL, &visible_rows, NULL, NULL, &max_v, &max_h);

    if (ctrl->treeview.scroll_offset > max_v)
        ctrl->treeview.scroll_offset = max_v;
    if (ctrl->treeview.hscroll_offset > max_h)
        ctrl->treeview.hscroll_offset = max_h;

    if (ctrl->treeview.selected_index < 0 ||
        ctrl->treeview.selected_index >= ctrl->treeview.item_count)
        return;

    int visible_idx = treeview_item_to_visible_index_sys(ctrl, ctrl->treeview.selected_index);
    if (visible_idx >= 0) {
        if (visible_idx < ctrl->treeview.scroll_offset) {
            ctrl->treeview.scroll_offset = visible_idx;
        } else if (visible_idx >= ctrl->treeview.scroll_offset + visible_rows) {
            ctrl->treeview.scroll_offset = visible_idx - visible_rows + 1;
        }
    }

    int selected_left = 3 + ctrl->treeview.items[ctrl->treeview.selected_index].level * TREEVIEW_INDENT_W;
    if (ctrl->treeview.hscroll_offset > selected_left)
        ctrl->treeview.hscroll_offset = selected_left;
}

int listbox_visible_rows_sys(gui_control_t *ctrl) {
    int row_h = ctrl->listbox.row_height ? ctrl->listbox.row_height : 16;
    int inner_h = ctrl->h > 4 ? ctrl->h - 4 : ctrl->h;
    int visible_rows = row_h > 0 ? inner_h / row_h : 1;
    return visible_rows < 1 ? 1 : visible_rows;
}

int listbox_max_scroll_sys(gui_control_t *ctrl) {
    int visible_rows = listbox_visible_rows_sys(ctrl);
    return ctrl->listbox.item_count > visible_rows ? ctrl->listbox.item_count - visible_rows : 0;
}
