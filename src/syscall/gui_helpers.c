#include "gui.h"

#define ICON_DIRTY_MARGIN 15

static void pump_set_icons_dirty_rect(window_manager_t *wm) {
    int min_x = WM_SCREEN_WIDTH, min_y = WM_SCREEN_HEIGHT, max_x = 0, max_y = 0;
    int found = 0;

    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f || !f->win.is_minimized) continue;

        if (f->win.minimized_icon_id != -1 && f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];
                if (ctrl->type == CTRL_ICON && ctrl->id == f->win.minimized_icon_id) {
                    int ix = ctrl->x - ICON_DIRTY_MARGIN;
                    int iy = ctrl->y - ICON_DIRTY_MARGIN;
                    int iw = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
                    int label_lines = icon_count_label_lines(ctrl->text, 49);
                    int ih = icon_calc_total_height(32, label_lines);
                    int ix2 = ctrl->x + iw + ICON_DIRTY_MARGIN;
                    int iy2 = ctrl->y + ih + ICON_DIRTY_MARGIN;
                    if (ix < min_x) min_x = ix;
                    if (iy < min_y) min_y = iy;
                    if (ix2 > max_x) max_x = ix2;
                    if (iy2 > max_y) max_y = iy2;
                    found = 1;
                    break;
                }
            }
        }
    }

    if (found) {
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        compositor_set_dirty_rect(wm, min_x, min_y, max_x - min_x, max_y - min_y);
    }
}

void pump_deselect_all_icons(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f || !f->win.is_minimized) continue;

        if (f->win.minimized_icon_id != -1 && f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];
                if (ctrl->type == CTRL_ICON && ctrl->id == f->win.minimized_icon_id) {
                    ctrl->icon.checked = 0;
                    break;
                }
            }
        }
    }
    pump_set_icons_dirty_rect(wm);
}

int pump_handle_icon_click(gui_form_t *form, int mx, int my) {
    uint32_t current_time = timer_get_ticks();

    if (win_is_icon_clicked(form, mx, my)) {
        if (wm_is_icon_doubleclick(&global_wm, current_time, form)) {
            int icon_x = 0, icon_y = 0;
            if (form->win.minimized_icon_id != -1 && form->controls) {
                gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                if (ctrl) {
                    icon_x = ctrl->x;
                    icon_y = ctrl->y;
                }
            }
            if (icon_x || icon_y) {
                wm_release_icon_slot(&global_wm, icon_x, icon_y);
            }
            win_restore(form);
            wm_bring_to_front(&global_wm, form);
            if (global_wm.count > 0) global_wm.focused_index = global_wm.count - 1;
            gui_request_full_redraw();
            return 2;
        } else {
            mouse_invalidate_buffer();
            pump_deselect_all_icons(&global_wm);

            if (form->win.minimized_icon_id != -1 && form->controls) {
                gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                if (ctrl) {
                    ctrl->icon.checked = 1;
                    ctrl->icon.click_start_x = mx;
                    ctrl->icon.click_start_y = my;
                    ctrl->icon.original_x = ctrl->x;
                    ctrl->icon.original_y = ctrl->y;
                }
            }
            wm_set_icon_click(&global_wm, current_time, form);
            return 1;
        }
    }
    return 0;
}

static void init_window_menu(gui_form_t *form) {
    menu_init(&form->window_menu);
    if (form->win.resizable) {
        if (form->win.is_maximized) {
            menu_add_item(&form->window_menu, "Restore", MENU_ACTION_RESTORE, MENU_ITEM_ENABLED);
        } else {
            menu_add_item(&form->window_menu, "Maximise", MENU_ACTION_MAXIMIZE, MENU_ITEM_ENABLED);
        }
    }
    menu_add_item(&form->window_menu, "Minimise", MENU_ACTION_MINIMIZE, MENU_ITEM_ENABLED);
    menu_add_item(&form->window_menu, "Close", MENU_ACTION_CLOSE, MENU_ITEM_ENABLED);
    form->window_menu_initialized = 1;
}

int pump_handle_minimize(gui_form_t *form, int mx, int my) {
    if (win_is_minimize_button(&form->win, mx, my)) {
        init_window_menu(form);

        int menu_x = form->win.x + form->win.w - 80;
        int menu_y = form->win.y + WM_TITLEBAR_HEIGHT + 2;

        menu_show(&form->window_menu, menu_x, menu_y);
        return 2;
    }
    return 0;
}

static int is_any_icon_selected(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f || !f->win.is_minimized) continue;

        if (f->win.minimized_icon_id != -1 && f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];
                if (ctrl->type == CTRL_ICON && ctrl->id == f->win.minimized_icon_id) {
                    if (ctrl->icon.checked) return 1;
                    break;
                }
            }
        }
    }
    return 0;
}

int pump_handle_titlebar_click(gui_form_t *form, int mx, int my) {
    if (form->win.is_maximized) return 0;
    if (win_is_titlebar(&form->win, mx, my)) {
        if (is_any_icon_selected(&global_wm))
            pump_deselect_all_icons(&global_wm);
        mouse_restore();
        mouse_invalidate_buffer();
        form->dragging = 1;
        form->drag_start_x = mx;
        form->drag_start_y = my;
        form->press_control_id = -1;
        return 1;
    }
    return 0;
}

int pump_handle_resize_corner_click(gui_form_t *form, int mx, int my) {
    if (form->win.is_maximized) return 0;
    if (win_is_resize_corner(&form->win, mx, my)) {
        if (is_any_icon_selected(&global_wm))
            pump_deselect_all_icons(&global_wm);
        mouse_restore();
        mouse_invalidate_buffer();
        form->resizing = 1;
        form->resize_start_w = form->win.w;
        form->resize_start_h = form->win.h;
        form->resize_start_mx = mx;
        form->resize_start_my = my;
        form->press_control_id = -1;
        return 1;
    }
    return 0;
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

gui_control_t *find_control_by_id(gui_form_t *form, int16_t id) {
    if (!form || !form->controls || id < 0) return NULL;
    for (int i = 0; i < form->ctrl_count; i++) {
        if (form->controls[i].id == id) {
            return &form->controls[i];
        }
    }
    return NULL;
}
