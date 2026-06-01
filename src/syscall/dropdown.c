#include "gui.h"

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

int pump_update_dropdown_hover(gui_form_t *form, int mx, int my, int ctrl_y_offset) {
    int needs_redraw = 0;

    if (!form->controls) return 0;

    for (int i = 0; i < form->ctrl_count; i++) {
        gui_control_t *ctrl = &form->controls[i];
        if (ctrl->type != CTRL_DROPDOWN || !ctrl->dropdown.dropdown_open) continue;

        if (ctrl->dropdown.pressed && ctrl->dropdown.hovered_item == DROPDOWN_HOVER_SCROLLBAR) continue;

        int item_h = 16;
        gui_rect_t list_rect;
        gui_dropdown_list_rect(form, ctrl, ctrl_y_offset, &list_rect);

        int visible_count = list_rect.h / item_h;
        if (visible_count < 1) visible_count = 1;
        int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
        int need_scrollbar = ctrl->dropdown.item_count > visible_count;
        int sb_w = need_scrollbar ? 18 : 0;
        int content_w = ctrl->w - sb_w;

        if (ctrl->dropdown.dropdown_scroll > (uint16_t)max_scroll) ctrl->dropdown.dropdown_scroll = max_scroll;

        int old_hover = ctrl->dropdown.hovered_item;

        if (mx >= list_rect.x && mx < list_rect.x + list_rect.w &&
            my >= list_rect.y && my < list_rect.y + list_rect.h) {
            if (need_scrollbar && mx >= list_rect.x + content_w && mx < list_rect.x + list_rect.w) {
                if (old_hover != DROPDOWN_HOVER_SCROLLBAR) {
                    ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                    needs_redraw = 1;
                }
            } else {
                int rel = (my - list_rect.y) / item_h;
                int hovered = ctrl->dropdown.dropdown_scroll + rel;
                if (hovered < 0) hovered = 0;
                if (hovered >= ctrl->dropdown.item_count) hovered = ctrl->dropdown.item_count - 1;
                if (hovered != old_hover) {
                    ctrl->dropdown.hovered_item = hovered;
                    needs_redraw = 1;
                }
            }
        } else {
            if (old_hover != -1) {
                ctrl->dropdown.hovered_item = -1;
                needs_redraw = 1;
            }
        }
    }

    return needs_redraw;
}
