#include "gui.h"

int gui_clamp_int(int value, int min_value, int max_value) {
    if (value < min_value) return min_value;
    if (value > max_value) return max_value;
    return value;
}

int gui_scrollbar_is_vertical(gui_control_t *ctrl) {
    if (!ctrl) return 1;
    if (ctrl->w > ctrl->h) return 0;
    if (ctrl->h > ctrl->w) return 1;
    return ctrl->scrollbar.checked ? 0 : 1;
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
