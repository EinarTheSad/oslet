#include "gui.h"

int gui_clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

void gui_dropdown_list_rect(gui_form_t *form, gui_control_t *ctrl,
                            int ctrl_y_offset, gui_rect_t *out_rect) {
    int item_h = 16;
    int abs_x = form->win.x + ctrl->x;
    int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
    int list_h = ctrl->dropdown.item_count * item_h;
    int list_y = abs_y + ctrl->h;

    if (list_y + list_h > GFX_HEIGHT) {
        list_y = abs_y - list_h;
        if (list_y < 0) {
            list_y = 0;
            list_h = abs_y;
            if (list_h < item_h) list_h = item_h;
        }
    }

    out_rect->x = abs_x;
    out_rect->y = list_y;
    out_rect->w = ctrl->w;
    out_rect->h = list_h;
}

void gui_scrollbar_make(scrollbar_geom_t *geom, int length, int arrow_size,
                        int value, int max_value) {
    if (max_value < 0) max_value = 0;
    if (arrow_size < 1) arrow_size = 1;

    geom->arrow_size = arrow_size;
    geom->track_len = length - 2 * arrow_size;
    if (geom->track_len < 1) geom->track_len = 1;

    geom->thumb_size = 20;
    if (geom->thumb_size > geom->track_len) geom->thumb_size = geom->track_len;
    if (geom->thumb_size < 1) geom->thumb_size = 1;

    geom->travel = geom->track_len - geom->thumb_size;
    if (geom->travel < 0) geom->travel = 0;
    geom->max_value = max_value;

    value = gui_clamp_int(value, 0, max_value);
    geom->thumb_pos = 0;
    if (max_value > 0 && geom->travel > 0) {
        geom->thumb_pos = (geom->travel * value) / max_value;
    }
}

int gui_scrollbar_value_from_pos(scrollbar_geom_t *geom, int pos, int drag_offset) {
    int rel = pos - drag_offset;
    rel = gui_clamp_int(rel, 0, geom->travel);
    if (geom->max_value <= 0 || geom->travel <= 0) return 0;
    return (rel * geom->max_value) / geom->travel;
}
