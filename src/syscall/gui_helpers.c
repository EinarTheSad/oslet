#include "gui.h"

static char textbox_clipboard[TEXTBOX_MULTILINE_SIZE];

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
            /* Double-click - restore window */
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
            global_wm.needs_full_redraw = 1;
            compositor_draw_all(&global_wm);
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
            return 1;  /* Selection changed, needs redraw */
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
        return 2;  /* Menu shown, needs redraw but don't minimize */
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
        return 1;  /* Dragging started */
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
        return 1;  /* Resizing started */
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

        int abs_x = form->win.x + ctrl->x;
        int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
        int item_h = 16;
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

        int visible_count = list_h / item_h;
        if (visible_count < 1) visible_count = 1;
        int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
        int need_scrollbar = ctrl->dropdown.item_count > visible_count;
        int sb_w = need_scrollbar ? 18 : 0;
        int content_w = ctrl->w - sb_w;

        if (ctrl->dropdown.dropdown_scroll > (uint16_t)max_scroll) ctrl->dropdown.dropdown_scroll = max_scroll;

        int old_hover = ctrl->dropdown.hovered_item;

        if (mx >= abs_x && mx < abs_x + ctrl->w && my >= list_y && my < list_y + list_h) {
            if (need_scrollbar && mx >= abs_x + content_w && mx < abs_x + ctrl->w) {
                if (old_hover != DROPDOWN_HOVER_SCROLLBAR) {
                    ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                    needs_redraw = 1;
                }
            } else {
                int rel = (my - list_y) / item_h;
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

char *textbox_get_text(gui_control_t *ctrl) {
    if (ctrl->type == CTRL_TEXTBOX && ctrl->textbox.is_multiline && ctrl->textbox.multiline_text) {
        return ctrl->textbox.multiline_text;
    }
    return ctrl->text;
}

static int textbox_text_len(gui_control_t *ctrl) {
    char *text = textbox_get_text(ctrl);
    int len = 0;
    while (text && text[len]) len++;
    return len;
}

static int textbox_selection_bounds(gui_control_t *ctrl, int text_len, int *sel_min, int *sel_max) {
    if (ctrl->textbox.sel_start < 0 || ctrl->textbox.sel_start == ctrl->textbox.sel_end) return 0;

    int start = ctrl->textbox.sel_start;
    int end = ctrl->textbox.sel_end;
    if (start > end) {
        int tmp = start;
        start = end;
        end = tmp;
    }

    if (start < 0) start = 0;
    if (end < 0) end = 0;
    if (start > text_len) start = text_len;
    if (end > text_len) end = text_len;
    if (start == end) return 0;

    *sel_min = start;
    *sel_max = end;
    return 1;
}

static int textbox_copy_selection(gui_control_t *ctrl) {
    int text_len = textbox_text_len(ctrl);
    int sel_min, sel_max;
    if (!textbox_selection_bounds(ctrl, text_len, &sel_min, &sel_max)) return 0;

    char *text = textbox_get_text(ctrl);
    int copy_len = sel_max - sel_min;
    if (copy_len >= TEXTBOX_MULTILINE_SIZE) copy_len = TEXTBOX_MULTILINE_SIZE - 1;

    for (int i = 0; i < copy_len; i++) {
        textbox_clipboard[i] = text[sel_min + i];
    }
    textbox_clipboard[copy_len] = '\0';
    return copy_len > 0;
}

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

static int treeview_visible_to_item_sys(gui_control_t *ctrl, int visible_index) {
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

#define TREEVIEW_INDENT_W 10

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

static int listbox_visible_rows_sys(gui_control_t *ctrl) {
    int row_h = ctrl->listbox.row_height ? ctrl->listbox.row_height : 16;
    int inner_h = ctrl->h > 4 ? ctrl->h - 4 : ctrl->h;
    int visible_rows = row_h > 0 ? inner_h / row_h : 1;
    return visible_rows < 1 ? 1 : visible_rows;
}

int listbox_max_scroll_sys(gui_control_t *ctrl) {
    int visible_rows = listbox_visible_rows_sys(ctrl);
    return ctrl->listbox.item_count > visible_rows ? ctrl->listbox.item_count - visible_rows : 0;
}

int pump_handle_control_press(gui_form_t *form, int mx, int my, int ctrl_y_offset) {
    form->press_control_id = -1;
    int old_focus = form->focused_control_id;
    int clicked_on_focusable = 0;
    int focus_cleared = 0;

    if (form->controls) {
        for (int i = 0; i < form->ctrl_count; i++) {
            gui_control_t *ctrl = &form->controls[i];
            if (ctrl->type != CTRL_DROPDOWN || !ctrl->dropdown.dropdown_open) continue;

            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
            int list_h = ctrl->dropdown.item_count * 16;
            int list_y = abs_y + ctrl->h;

            if (list_y + list_h > GFX_HEIGHT) {
                list_y = abs_y - list_h;
                if (list_y < 0) {
                    list_y = 0;
                    list_h = abs_y;
                    if (list_h < 16) list_h = 16;
                }
            }

            if (mx >= abs_x && mx < abs_x + ctrl->w && my >= list_y && my < list_y + list_h) {
                int item_h = 16;
                int visible_count = list_h / item_h;
                if (visible_count < 1) visible_count = 1;
                int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
                int need_scrollbar = ctrl->dropdown.item_count > visible_count;
                int sb_w = need_scrollbar ? 18 : 0;
                int content_w = ctrl->w - sb_w;

                if (need_scrollbar && mx >= abs_x + content_w && mx < abs_x + ctrl->w) {
                    int arrow_size = sb_w;
                    int track_len = list_h - 2 * arrow_size;
                    int thumb_size = 20;
                    if (thumb_size > track_len) thumb_size = track_len;
                    int thumb_pos = 0;
                    if (max_scroll > 0 && track_len > thumb_size)
                        thumb_pos = ((track_len - thumb_size) * ctrl->dropdown.dropdown_scroll) / max_scroll;

                    int rel_y = my - list_y;

                    if (rel_y < arrow_size) {
                        if (ctrl->dropdown.dropdown_scroll > 0) {
                            ctrl->dropdown.dropdown_scroll--;
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        form->press_control_id = ctrl->id;
                        global_wm.needs_full_redraw = 1;
                        return 1;
                    }

                    if (rel_y >= list_h - arrow_size) {
                        if (ctrl->dropdown.dropdown_scroll < max_scroll) {
                            ctrl->dropdown.dropdown_scroll++;
                            if (ctrl->dropdown.dropdown_scroll > max_scroll) ctrl->dropdown.dropdown_scroll = max_scroll;
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        form->press_control_id = ctrl->id;
                        global_wm.needs_full_redraw = 1;
                        return 1;
                    }

                    int thumb_y = arrow_size + thumb_pos;
                    if (rel_y >= thumb_y && rel_y < thumb_y + thumb_size) {
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        ctrl->dropdown.scroll_offset = rel_y - thumb_y;
                        form->press_control_id = ctrl->id;
                        return 1;
                    }

                    if (rel_y >= arrow_size && rel_y < list_h - arrow_size) {
                        int track_y = arrow_size;
                        int rel_track = rel_y - track_y - thumb_size / 2;
                        if (rel_track < 0) rel_track = 0;
                        if (rel_track > track_len - thumb_size) rel_track = track_len - thumb_size;
                        int new_scroll = 0;
                        if (track_len - thumb_size > 0)
                            new_scroll = (rel_track * max_scroll) / (track_len - thumb_size);
                        if (new_scroll < 0) new_scroll = 0;
                        if (new_scroll > max_scroll) new_scroll = max_scroll;
                        if (new_scroll != ctrl->dropdown.dropdown_scroll) {
                            ctrl->dropdown.dropdown_scroll = new_scroll;
                            global_wm.needs_full_redraw = 1;
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        ctrl->dropdown.scroll_offset = thumb_size / 2;
                        form->press_control_id = ctrl->id;
                        return 1;
                    }
                }

                int rel_item = (my - list_y) / item_h;
                int clicked_item = ctrl->dropdown.dropdown_scroll + rel_item;
                if (clicked_item >= 0 && clicked_item < ctrl->dropdown.item_count) {
                    ctrl->dropdown.cursor_pos = clicked_item;
                    form->clicked_id = ctrl->id;
                }
                ctrl_hide_dropdown_list(&form->win, ctrl);
                form->press_control_id = -1;
                global_wm.needs_full_redraw = 1;
                return 1;
            }
            else if (mx >= abs_x && mx < abs_x + ctrl->w &&
                     my >= abs_y && my < abs_y + ctrl->h) {
                ctrl_hide_dropdown_list(&form->win, ctrl);
                form->press_control_id = ctrl->id;
                global_wm.needs_full_redraw = 1;
                return 1;
            }
            else {
                ctrl_hide_dropdown_list(&form->win, ctrl);
                global_wm.needs_full_redraw = 1;
                return 1;
            }
        }

        /* Iterate backwards to check top-most (last drawn) controls first */
        for (int i = form->ctrl_count - 1; i >= 0; i--) {
            gui_control_t *ctrl = &form->controls[i];

            /* Skip non-interactive controls (treat picturebox as interactive so clicks are detected) */
            if (ctrl->type != CTRL_BUTTON &&
                ctrl->type != CTRL_LABEL &&
                ctrl->type != CTRL_CHECKBOX &&
                ctrl->type != CTRL_RADIOBUTTON &&
                ctrl->type != CTRL_TEXTBOX &&
                ctrl->type != CTRL_ICON &&
                ctrl->type != CTRL_DROPDOWN &&
                ctrl->type != CTRL_PICTUREBOX &&
                ctrl->type != CTRL_SCROLLBAR &&
                ctrl->type != CTRL_TREEVIEW &&
                ctrl->type != CTRL_LISTBOX) {
                continue;
            }

            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

            int hit_w = ctrl->w;
            int hit_h = ctrl->h;
            int hit_y_offset = 0;

            if (ctrl->type == CTRL_CHECKBOX && ctrl->w == 13) {
                hit_w = 100;
            } else if (ctrl->type == CTRL_RADIOBUTTON && ctrl->w == 12) {
                hit_w = 100;
            } else if (ctrl->type == CTRL_ICON) {
                hit_w = ctrl->w > 0 ? ctrl->w : 48;
                hit_h = ctrl->h > 0 ? ctrl->h : 58;
            } else if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                int list_h = ctrl->dropdown.item_count * 16;
                int list_y = abs_y + ctrl->h;

                if (list_y + list_h > GFX_HEIGHT) {
                    hit_y_offset = -(list_h);
                    hit_h = ctrl->h + list_h;
                } else {
                    hit_h = ctrl->h + list_h;
                }
            }

            int hit_test_y = abs_y + (hit_y_offset != 0 ? hit_y_offset : 0);

            if (mx >= abs_x && mx < abs_x + hit_w &&
                my >= hit_test_y && my < hit_test_y + hit_h) {
                form->press_control_id = ctrl->id;
                if (ctrl->type != CTRL_TEXTBOX && old_focus != -1) {
                    form->focused_control_id = -1;
                    focus_cleared = 1;
                }

                if (ctrl->type == CTRL_BUTTON) {
                    ctrl->button.pressed = 1;
                    return 1;  /* needs_redraw */
                }
                else if (ctrl->type == CTRL_LABEL) {
                    return focus_cleared;
                }
                else if (ctrl->type == CTRL_CHECKBOX || ctrl->type == CTRL_RADIOBUTTON) {
                    return focus_cleared;
                }
                else if (ctrl->type == CTRL_TEXTBOX) {
                    form->focused_control_id = ctrl->id;
                    clicked_on_focusable = 1;

                    extern bmf_font_t font_n;
                    int size = ctrl->font_size > 0 ? ctrl->font_size : 12;
                    int text_area_x = abs_x + 3;
                    int text_area_w = ctrl->w - 6;
                    int rel_x = mx - text_area_x;
                    int rel_y = my - (abs_y + 6);

                    char *text = textbox_get_text(ctrl);
                    int new_pos;

                    if (ctrl->textbox.is_multiline) {
                        int text_area_h = ctrl->h - 9;
                        int font_height = 0;
                        int seq_idx = -1;
                        for (int j = 0; j < font_n.size_count; j++) {
                            if (font_n.sequences[j].point_size == size) {
                                seq_idx = j;
                                break;
                            }
                        }
                        font_height = seq_idx >= 0 ? font_n.sequences[seq_idx].height : size;
                        int line_height = font_height + 2;
                        int visible_lines = text_area_h / line_height;
                        if (visible_lines < 1) visible_lines = 1;

                        int total_lines = textbox_wrap_line_count(&font_n, size, text, text_area_w);
                        int needs_scrollbar = total_lines > visible_lines;
                        int max_scroll = total_lines > visible_lines ? total_lines - visible_lines : 0;
                        int content_width = text_area_w;
                        if (needs_scrollbar) {
                            content_width -= 18;
                            if (content_width < 10) content_width = 10;
                        }

                        if (needs_scrollbar && mx >= abs_x + ctrl->w - 18 && mx < abs_x + ctrl->w &&
                            my >= abs_y + 6 && my < abs_y + 6 + text_area_h) {
                            int arrow_size = 18;
                            int track_len = text_area_h - arrow_size * 2;
                            int thumb_size = 20;
                            if (thumb_size > track_len) thumb_size = track_len;
                            int thumb_pos = 0;
                            if (max_scroll > 0 && track_len > thumb_size) {
                                thumb_pos = ((track_len - thumb_size) * ctrl->textbox.current_line) / max_scroll;
                            }
                            int rel_sb_y = my - (abs_y + 6);

                            if (rel_sb_y < arrow_size) {
                                if (ctrl->textbox.current_line > 0) ctrl->textbox.current_line--;
                                ctrl->textbox.scrollbar_hovered_item = 0;
                                ctrl->textbox.scrollbar_pressed = 1;
                                form->press_control_id = ctrl->id;
                                return 1;
                            }
                            if (rel_sb_y >= text_area_h - arrow_size) {
                                if (ctrl->textbox.current_line < max_scroll) ctrl->textbox.current_line++;
                                ctrl->textbox.scrollbar_hovered_item = 2;
                                ctrl->textbox.scrollbar_pressed = 1;
                                form->press_control_id = ctrl->id;
                                return 1;
                            }

                            int thumb_y = arrow_size + thumb_pos;
                            if (rel_sb_y >= thumb_y && rel_sb_y < thumb_y + thumb_size) {
                                ctrl->textbox.scrollbar_hovered_item = 1;
                                ctrl->textbox.scrollbar_pressed = 1;
                                ctrl->textbox.scroll_offset = rel_sb_y - thumb_y;
                                form->press_control_id = ctrl->id;
                                return 1;
                            }

                            if (rel_sb_y >= arrow_size && rel_sb_y < text_area_h - arrow_size) {
                                int rel_track = rel_sb_y - arrow_size - thumb_size / 2;
                                if (rel_track < 0) rel_track = 0;
                                if (rel_track > track_len - thumb_size) rel_track = track_len - thumb_size;
                                int new_scroll = 0;
                                if (track_len - thumb_size > 0) {
                                    new_scroll = (rel_track * max_scroll) / (track_len - thumb_size);
                                }
                                if (new_scroll < 0) new_scroll = 0;
                                if (new_scroll > max_scroll) new_scroll = max_scroll;
                                ctrl->textbox.current_line = new_scroll;
                                ctrl->textbox.scrollbar_hovered_item = 1;
                                ctrl->textbox.scrollbar_pressed = 1;
                                ctrl->textbox.scroll_offset = thumb_size / 2;
                                form->press_control_id = ctrl->id;
                                return 1;
                            }
                        }

                        new_pos = textbox_pos_from_xy(&font_n, size, text, text_area_x, content_width,
                                                      ctrl->textbox.current_line, rel_x, rel_y);
                    } else {
                        new_pos = textbox_pos_from_x(&font_n, size, text,
                                                     ctrl->textbox.scroll_offset, rel_x);
                    }
                    ctrl->textbox.cursor_pos = new_pos;

                    ctrl->textbox.sel_start = new_pos;
                    ctrl->textbox.sel_end = new_pos;
                    form->textbox_selecting = 1;

                    return 1;
                }
                else if (ctrl->type == CTRL_ICON) {
                    if (old_focus != -1) {
                        form->focused_control_id = -1;
                        return 1;
                    }
                    return 0;
                }
                else if (ctrl->type == CTRL_DROPDOWN) {
                    if (!ctrl->dropdown.dropdown_open) {
                        int count = 1;
                        const char *p = ctrl->text;
                        while (*p) {
                            if (*p == '|') count++;
                            p++;
                        }
                        ctrl->dropdown.item_count = count;

                        int item_h = 16;
                        int list_h = ctrl->dropdown.item_count * item_h;
                        int list_y = abs_y + ctrl->h;
                        if (list_y + list_h > GFX_HEIGHT) {
                            list_h = abs_y;
                            if (list_h < item_h) list_h = item_h;
                        }
                        int visible_count = list_h / item_h;
                        if (visible_count < 1) visible_count = 1;
                        int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
                        int sel = ctrl->dropdown.cursor_pos;
                        if (sel < 0) sel = 0;
                        if (sel >= ctrl->dropdown.item_count) sel = ctrl->dropdown.item_count - 1;
                        if (sel > visible_count - 1) {
                            int s = sel - (visible_count - 1);
                            if (s > max_scroll) s = max_scroll;
                            ctrl->dropdown.dropdown_scroll = s;
                        } else {
                            ctrl->dropdown.dropdown_scroll = 0;
                        }

                        ctrl->dropdown.dropdown_open = 1;
                        return 1;
                    }
                }
                else if (ctrl->type == CTRL_SCROLLBAR) {
                    int vertical = !ctrl->scrollbar.checked;
                    int arrow_size = vertical ? ctrl->w : ctrl->h;
                    int track_len = vertical ? (ctrl->h - 2 * arrow_size) : (ctrl->w - 2 * arrow_size);
                    int thumb_size = 20;
                    if (thumb_size > track_len) thumb_size = track_len;
                    int max_val = ctrl->scrollbar.max_length > 0 ? ctrl->scrollbar.max_length : 100;
                    int travel = track_len - thumb_size;

                    int thumb_pos = 0;
                    if (max_val > 0 && travel > 0) {
                        thumb_pos = (travel * ctrl->scrollbar.cursor_pos) / max_val;
                    }

                    if (vertical) {
                        int up_y = abs_y;
                        int down_y = abs_y + ctrl->h - arrow_size;
                        int track_y = abs_y + arrow_size;
                        int thumb_y = track_y + thumb_pos;

                        if (my >= up_y && my < up_y + arrow_size) {
                            /* Up arrow clicked */
                            ctrl->scrollbar.hovered_item = 0;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos > 0) ctrl->scrollbar.cursor_pos--;
                            return 1;
                        } else if (my >= down_y && my < down_y + arrow_size) {
                            /* Down arrow clicked */
                            ctrl->scrollbar.hovered_item = 2;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos < max_val) ctrl->scrollbar.cursor_pos++;
                            return 1;
                        } else if (my >= thumb_y && my < thumb_y + thumb_size) {
                            /* Thumb clicked - start dragging, store offset */
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = my - thumb_y; /* Store drag offset */
                            return 1;
                        } else if (my >= track_y && my < track_y + track_len) {
                            if (travel <= 0)
                                return 1;
                            /* Track clicked (not on thumb) - jump to position */
                            int rel_y = my - track_y - thumb_size / 2; /* Center thumb on click */
                            if (rel_y < 0) rel_y = 0;
                            if (rel_y > travel) rel_y = travel;
                            ctrl->scrollbar.cursor_pos = (rel_y * max_val) / travel;
                            if (ctrl->scrollbar.cursor_pos > max_val) ctrl->scrollbar.cursor_pos = max_val;
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = thumb_size / 2; /* Store drag offset at center */
                            return 1;
                        }
                    } else {
                        /* Horizontal scrollbar */
                        int left_x = abs_x;
                        int right_x = abs_x + ctrl->w - arrow_size;
                        int track_x = abs_x + arrow_size;
                        int thumb_x = track_x + thumb_pos;

                        if (mx >= left_x && mx < left_x + arrow_size) {
                            /* Left arrow clicked */
                            ctrl->scrollbar.hovered_item = 0;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos > 0) ctrl->scrollbar.cursor_pos--;
                            return 1;
                        } else if (mx >= right_x && mx < right_x + arrow_size) {
                            /* Right arrow clicked */
                            ctrl->scrollbar.hovered_item = 2;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos < max_val) ctrl->scrollbar.cursor_pos++;
                            return 1;
                        } else if (mx >= thumb_x && mx < thumb_x + thumb_size) {
                            /* Thumb clicked - start dragging */
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = mx - thumb_x; /* Store drag offset */
                            return 1;
                        } else if (mx >= track_x && mx < track_x + track_len) {
                            if (travel <= 0)
                                return 1;
                            /* Track clicked (not on thumb) - jump to position */
                            int rel_x = mx - track_x - thumb_size / 2;
                            if (rel_x < 0) rel_x = 0;
                            if (rel_x > travel) rel_x = travel;
                            ctrl->scrollbar.cursor_pos = (rel_x * max_val) / travel;
                            if (ctrl->scrollbar.cursor_pos > max_val) ctrl->scrollbar.cursor_pos = max_val;
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = thumb_size / 2;
                            return 1;
                        }
                    }
                }
                else if (ctrl->type == CTRL_TREEVIEW) {
                    ctrl->treeview.last_action = TREE_ACTION_NONE;
                    ctrl->treeview.action_index = -1;

                    int row_h = ctrl->treeview.row_height ? ctrl->treeview.row_height : 18;
                    int inner_x = abs_x + 2;
                    int inner_y = abs_y + 2;
                    int content_w = 1;
                    int row_area_h = 1;
                    int visible_rows = 1;
                    int max_scroll = 0;
                    int max_hscroll = 0;
                    int needs_scrollbar = 0;
                    int needs_hscrollbar = 0;
                    int sb_w = 18;
                    treeview_layout_sys(ctrl, &content_w, &row_area_h, &visible_rows,
                                        &needs_scrollbar, &needs_hscrollbar,
                                        &max_scroll, &max_hscroll);

                    if (ctrl->treeview.scroll_offset > max_scroll)
                        ctrl->treeview.scroll_offset = max_scroll;
                    if (ctrl->treeview.hscroll_offset > max_hscroll)
                        ctrl->treeview.hscroll_offset = max_hscroll;

                    if (needs_scrollbar && row_area_h > sb_w * 2 &&
                        mx >= abs_x + ctrl->w - sb_w - 1 && mx < abs_x + ctrl->w - 1 &&
                        my >= inner_y && my < inner_y + row_area_h) {
                        int arrow_size = sb_w;
                        int track_len = row_area_h - 2 * arrow_size;
                        int thumb_size = 20;
                        if (thumb_size > track_len) thumb_size = track_len;
                        if (thumb_size < 1) thumb_size = 1;
                        int thumb_pos = 0;
                        if (max_scroll > 0 && track_len > thumb_size)
                            thumb_pos = ((track_len - thumb_size) * ctrl->treeview.scroll_offset) / max_scroll;

                        int rel_y = my - inner_y;
                        if (rel_y < arrow_size) {
                            ctrl->treeview.scrollbar_hovered_item = 0;
                            ctrl->treeview.scrollbar_pressed = 1;
                            if (ctrl->treeview.scroll_offset > 0)
                                ctrl->treeview.scroll_offset--;
                            return 1;
                        } else if (rel_y >= row_area_h - arrow_size) {
                            ctrl->treeview.scrollbar_hovered_item = 2;
                            ctrl->treeview.scrollbar_pressed = 1;
                            if (ctrl->treeview.scroll_offset < max_scroll)
                                ctrl->treeview.scroll_offset++;
                            return 1;
                        } else {
                            int thumb_y = arrow_size + thumb_pos;
                            if (rel_y >= thumb_y && rel_y < thumb_y + thumb_size) {
                                ctrl->treeview.scrollbar_hovered_item = 1;
                                ctrl->treeview.scrollbar_pressed = 1;
                                ctrl->treeview.scrollbar_drag_offset = rel_y - thumb_y;
                                return 1;
                            }

                            int rel_track = rel_y - arrow_size - thumb_size / 2;
                            if (rel_track < 0) rel_track = 0;
                            if (rel_track > track_len - thumb_size) rel_track = track_len - thumb_size;
                            if (track_len > thumb_size && max_scroll > 0)
                                ctrl->treeview.scroll_offset = (rel_track * max_scroll) / (track_len - thumb_size);
                            ctrl->treeview.scrollbar_hovered_item = 1;
                            ctrl->treeview.scrollbar_pressed = 1;
                            ctrl->treeview.scrollbar_drag_offset = thumb_size / 2;
                            return 1;
                        }
                    }

                    if (needs_hscrollbar && content_w > sb_w * 2 &&
                        mx >= inner_x && mx < inner_x + content_w &&
                        my >= abs_y + ctrl->h - sb_w - 1 && my < abs_y + ctrl->h - 1) {
                        int arrow_size = sb_w;
                        int track_len = content_w - 2 * arrow_size;
                        int thumb_size = 20;
                        if (thumb_size > track_len) thumb_size = track_len;
                        if (thumb_size < 1) thumb_size = 1;
                        int thumb_pos = 0;
                        if (max_hscroll > 0 && track_len > thumb_size)
                            thumb_pos = ((track_len - thumb_size) * ctrl->treeview.hscroll_offset) / max_hscroll;

                        int rel_x = mx - inner_x;
                        if (rel_x < arrow_size) {
                            ctrl->treeview.hscrollbar_hovered_item = 0;
                            ctrl->treeview.hscrollbar_pressed = 1;
                            if (ctrl->treeview.hscroll_offset > 0)
                                ctrl->treeview.hscroll_offset--;
                            return 1;
                        } else if (rel_x >= content_w - arrow_size) {
                            ctrl->treeview.hscrollbar_hovered_item = 2;
                            ctrl->treeview.hscrollbar_pressed = 1;
                            if (ctrl->treeview.hscroll_offset < max_hscroll)
                                ctrl->treeview.hscroll_offset++;
                            return 1;
                        } else {
                            int thumb_x = arrow_size + thumb_pos;
                            if (rel_x >= thumb_x && rel_x < thumb_x + thumb_size) {
                                ctrl->treeview.hscrollbar_hovered_item = 1;
                                ctrl->treeview.hscrollbar_pressed = 1;
                                ctrl->treeview.hscrollbar_drag_offset = rel_x - thumb_x;
                                return 1;
                            }

                            int rel_track = rel_x - arrow_size - thumb_size / 2;
                            if (rel_track < 0) rel_track = 0;
                            if (rel_track > track_len - thumb_size) rel_track = track_len - thumb_size;
                            if (track_len > thumb_size && max_hscroll > 0)
                                ctrl->treeview.hscroll_offset = (rel_track * max_hscroll) / (track_len - thumb_size);
                            ctrl->treeview.hscrollbar_hovered_item = 1;
                            ctrl->treeview.hscrollbar_pressed = 1;
                            ctrl->treeview.hscrollbar_drag_offset = thumb_size / 2;
                            return 1;
                        }
                    }

                    if (mx >= inner_x && mx < inner_x + content_w &&
                        my >= inner_y && my < inner_y + visible_rows * row_h) {
                        int visible_idx = ctrl->treeview.scroll_offset + ((my - inner_y) / row_h);
                        int item_idx = treeview_visible_to_item_sys(ctrl, visible_idx);
                        if (item_idx >= 0) {
                            sys_tree_item_t *item = &ctrl->treeview.items[item_idx];
                            int box_x = inner_x + 3 + item->level * TREEVIEW_INDENT_W - ctrl->treeview.hscroll_offset;
                            int box_y = inner_y + ((my - inner_y) / row_h) * row_h + (row_h - 9) / 2;
                            if ((item->flags & TREE_ITEM_HAS_CHILDREN) &&
                                mx >= box_x && mx < box_x + 9 &&
                                my >= box_y && my < box_y + 9) {
                                ctrl->treeview.last_action = TREE_ACTION_TOGGLE;
                            } else {
                                ctrl->treeview.selected_index = item_idx;
                                ctrl->treeview.last_action = TREE_ACTION_SELECT;
                            }
                            ctrl->treeview.action_index = item_idx;
                            form->clicked_id = ctrl->id;
                            form->press_control_id = -1;
                            return 1;
                        }
                    }
                }
                else if (ctrl->type == CTRL_LISTBOX) {
                    ctrl->listbox.last_action = LIST_ACTION_NONE;
                    ctrl->listbox.action_index = -1;

                    int row_h = ctrl->listbox.row_height ? ctrl->listbox.row_height : 16;
                    int inner_x = abs_x + 2;
                    int inner_y = abs_y + 2;
                    int inner_w = ctrl->w > 4 ? ctrl->w - 4 : ctrl->w;
                    int inner_h = ctrl->h > 4 ? ctrl->h - 4 : ctrl->h;
                    int visible_rows = listbox_visible_rows_sys(ctrl);
                    int max_scroll = listbox_max_scroll_sys(ctrl);
                    int sb_w = 18;
                    int needs_scrollbar = ctrl->listbox.item_count > visible_rows;
                    int content_w = inner_w - (needs_scrollbar ? sb_w : 0);
                    if (content_w < 1) content_w = 1;

                    if (ctrl->listbox.scroll_offset > max_scroll)
                        ctrl->listbox.scroll_offset = max_scroll;

                    if (needs_scrollbar && inner_h > sb_w * 2 &&
                        mx >= abs_x + ctrl->w - sb_w - 1 && mx < abs_x + ctrl->w - 1 &&
                        my >= inner_y && my < inner_y + inner_h) {
                        int arrow_size = sb_w;
                        int track_len = inner_h - 2 * arrow_size;
                        int thumb_size = 20;
                        if (thumb_size > track_len) thumb_size = track_len;
                        if (thumb_size < 1) thumb_size = 1;
                        int thumb_pos = 0;
                        if (max_scroll > 0 && track_len > thumb_size)
                            thumb_pos = ((track_len - thumb_size) * ctrl->listbox.scroll_offset) / max_scroll;

                        int rel_y = my - inner_y;
                        if (rel_y < arrow_size) {
                            ctrl->listbox.scrollbar_hovered_item = 0;
                            ctrl->listbox.scrollbar_pressed = 1;
                            if (ctrl->listbox.scroll_offset > 0)
                                ctrl->listbox.scroll_offset--;
                            return 1;
                        } else if (rel_y >= inner_h - arrow_size) {
                            ctrl->listbox.scrollbar_hovered_item = 2;
                            ctrl->listbox.scrollbar_pressed = 1;
                            if (ctrl->listbox.scroll_offset < max_scroll)
                                ctrl->listbox.scroll_offset++;
                            return 1;
                        } else {
                            int thumb_y = arrow_size + thumb_pos;
                            if (rel_y >= thumb_y && rel_y < thumb_y + thumb_size) {
                                ctrl->listbox.scrollbar_hovered_item = 1;
                                ctrl->listbox.scrollbar_pressed = 1;
                                ctrl->listbox.scrollbar_drag_offset = rel_y - thumb_y;
                                return 1;
                            }

                            int rel_track = rel_y - arrow_size - thumb_size / 2;
                            if (rel_track < 0) rel_track = 0;
                            if (rel_track > track_len - thumb_size) rel_track = track_len - thumb_size;
                            if (track_len > thumb_size && max_scroll > 0)
                                ctrl->listbox.scroll_offset = (rel_track * max_scroll) / (track_len - thumb_size);
                            ctrl->listbox.scrollbar_hovered_item = 1;
                            ctrl->listbox.scrollbar_pressed = 1;
                            ctrl->listbox.scrollbar_drag_offset = thumb_size / 2;
                            return 1;
                        }
                    }

                    if (mx >= inner_x && mx < inner_x + content_w &&
                        my >= inner_y && my < inner_y + visible_rows * row_h) {
                        int item_idx = ctrl->listbox.scroll_offset + ((my - inner_y) / row_h);
                        if (item_idx >= 0 && item_idx < ctrl->listbox.item_count) {
                            ctrl->listbox.selected_index = item_idx;
                            ctrl->listbox.last_action = LIST_ACTION_SELECT;
                            ctrl->listbox.action_index = item_idx;
                            form->clicked_id = ctrl->id;
                            form->press_control_id = -1;
                            return 1;
                        }
                    }
                }
                break;
            }
        }
    }

    /* If clicked outside any focusable control, clear focus */
    if (!clicked_on_focusable && old_focus != -1) {
        form->focused_control_id = -1;
        return 1;  /* Focus changed, needs redraw */
    }

    /* If no control was pressed, deselect all icons in the form */
    if (form->press_control_id == -1 && form->controls) {
        int deselected = 0;
        for (int i = 0; i < form->ctrl_count; i++) {
            if (form->controls[i].type == CTRL_ICON && form->controls[i].icon.checked) {
                form->controls[i].icon.checked = 0;
                deselected = 1;
            }
        }
        if (deselected) {
            return 1;  /* Icons deselected, needs redraw */
        }
    }

    return 0;
}

/* Helper function to find control by ID */
gui_control_t *find_control_by_id(gui_form_t *form, int16_t id) {
    if (!form || !form->controls || id < 0) return NULL;
    for (int i = 0; i < form->ctrl_count; i++) {
        if (form->controls[i].id == id) {
            return &form->controls[i];
        }
    }
    return NULL;
}

static int textbox_delete_selection(gui_control_t *ctrl) {
    int text_len = textbox_text_len(ctrl);
    int sel_min, sel_max;
    if (!textbox_selection_bounds(ctrl, text_len, &sel_min, &sel_max)) return 0;

    /* Shift text left to remove selected portion */
    char *text = textbox_get_text(ctrl);
    int del_count = sel_max - sel_min;
    for (int i = sel_min; i < text_len - del_count; i++) {
        text[i] = text[i + del_count];
    }
    text[text_len - del_count] = '\0';

    ctrl->textbox.cursor_pos = sel_min;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

static int textbox_insert_text(gui_control_t *ctrl, const char *insert_text) {
    if (!insert_text || !insert_text[0]) return 0;

    int max_len = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
    if (max_len < 2) return 0;

    if (textbox_delete_selection(ctrl)) {
        /* cursor and text length are recalculated below */
    }

    char *text = textbox_get_text(ctrl);
    int text_len = textbox_text_len(ctrl);
    int cursor = ctrl->textbox.cursor_pos;
    if (cursor < 0) cursor = 0;
    if (cursor > text_len) cursor = text_len;

    int room = (max_len - 1) - text_len;
    if (room <= 0) return 0;

    int insert_len = 0;
    while (insert_text[insert_len] && insert_len < room) insert_len++;
    if (insert_len <= 0) return 0;

    for (int i = text_len; i >= cursor; i--) {
        text[i + insert_len] = text[i];
    }

    for (int i = 0; i < insert_len; i++) {
        text[cursor + i] = insert_text[i];
    }

    ctrl->textbox.cursor_pos = cursor + insert_len;
    ctrl->textbox.sel_start = -1;
    ctrl->textbox.sel_end = -1;
    return 1;
}

static int textbox_select_all(gui_control_t *ctrl) {
    int text_len = textbox_text_len(ctrl);
    if (text_len <= 0) {
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        ctrl->textbox.cursor_pos = 0;
        return 1;
    }

    ctrl->textbox.sel_start = 0;
    ctrl->textbox.sel_end = text_len;
    ctrl->textbox.cursor_pos = text_len;
    return 1;
}

int textbox_run_edit_command(gui_control_t *ctrl, int command) {
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    switch (command) {
        case TEXTBOX_EDIT_COPY:
            textbox_copy_selection(ctrl);
            return 0;
        case TEXTBOX_EDIT_CUT:
            if (!textbox_copy_selection(ctrl)) return 0;
            return textbox_delete_selection(ctrl);
        case TEXTBOX_EDIT_PASTE:
            return textbox_insert_text(ctrl, textbox_clipboard);
        case TEXTBOX_EDIT_SELECT_ALL:
            return textbox_select_all(ctrl);
        default:
            return 0;
    }
}

/* Handle keyboard input for focused textbox */
int pump_handle_keyboard(gui_form_t *form) {
    if (form->focused_control_id < 0) return 0;

    gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
    if (!ctrl || ctrl->type != CTRL_TEXTBOX) return 0;

    int key = kbd_getchar_nonblock();
    if (key == 0) return 0;  /* No key pressed */

    char *text = textbox_get_text(ctrl);
    int text_len = 0;
    while (text[text_len]) text_len++;

    int max_len = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
    int needs_redraw = 0;
    int has_selection = (ctrl->textbox.sel_start >= 0 && ctrl->textbox.sel_start != ctrl->textbox.sel_end);

    if (key == 14 || key == 19) {
        form->clicked_id = SYS_EVENT_CTRL_KEY(key);
        return 2;
    }

    if (key == 1 || key == 3 || key == 22 || key == 24) {
        if (key == 1) {              /* Ctrl+A */
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_SELECT_ALL);
        } else if (key == 3) {       /* Ctrl+C */
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_COPY);
        } else if (key == 22) {      /* Ctrl+V */
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_PASTE);
        } else if (key == 24) {      /* Ctrl+X */
            needs_redraw = textbox_run_edit_command(ctrl, TEXTBOX_EDIT_CUT);
        }
        return needs_redraw;
    }

    /* Handle special keys */
    if (key == KEY_LEFT) {
        if (has_selection) {
            int sel_min = ctrl->textbox.sel_start < ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = sel_min;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            ctrl->textbox.cursor_pos--;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_RIGHT) {
        if (has_selection) {
            int sel_max = ctrl->textbox.sel_start > ctrl->textbox.sel_end ? ctrl->textbox.sel_start : ctrl->textbox.sel_end;
            ctrl->textbox.cursor_pos = sel_max;
            ctrl->textbox.sel_start = -1;
            ctrl->textbox.sel_end = -1;
        } else if (ctrl->textbox.cursor_pos < text_len) {
            ctrl->textbox.cursor_pos++;
        }
        needs_redraw = 1;
    }
    else if (key == KEY_HOME) {
        ctrl->textbox.cursor_pos = 0;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == KEY_END) {
        ctrl->textbox.cursor_pos = text_len;
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        needs_redraw = 1;
    }
    else if (key == '\b') {  /* Backspace */
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->textbox.cursor_pos > 0) {
            text = textbox_get_text(ctrl);
            for (int i = ctrl->textbox.cursor_pos - 1; i < text_len; i++) {
                text[i] = text[i + 1];
            }
            ctrl->textbox.cursor_pos--;
            needs_redraw = 1;
        }
    }
    else if (key == KEY_DELETE) {
        if (has_selection) {
            textbox_delete_selection(ctrl);
            needs_redraw = 1;
        } else if (ctrl->textbox.cursor_pos < text_len) {
            text = textbox_get_text(ctrl);
            for (int i = ctrl->textbox.cursor_pos; i < text_len; i++) {
                text[i] = text[i + 1];
            }
            needs_redraw = 1;
        }
    }
    else if (key == '\t') {
        /* Tab could be used to move focus to next control - for now skip */
    }
    else if (key == '\n' || key == '\r') {
        if (ctrl->textbox.is_multiline && text_len < max_len - 1) {
            text = textbox_get_text(ctrl);
            for (int i = text_len; i >= ctrl->textbox.cursor_pos; i--) {
                text[i + 1] = text[i];
            }
            text[ctrl->textbox.cursor_pos] = '\n';
            ctrl->textbox.cursor_pos++;
            needs_redraw = 1;
        } else {
            form->clicked_id = ctrl->id;
            needs_redraw = 1;
        }
    }
    else if (key == KEY_ESC) {
        ctrl->textbox.sel_start = -1;
        ctrl->textbox.sel_end = -1;
        form->focused_control_id = -1;
        needs_redraw = 1;
    }
    else if (key >= 0x20 && key < 0x80) {
        /* Printable ASCII character */
        if (has_selection) {
            textbox_delete_selection(ctrl);
            text = textbox_get_text(ctrl);
            text_len = 0;
            while (text[text_len]) text_len++;
        }

        if (text_len < max_len - 1) {
            text = textbox_get_text(ctrl);
            for (int i = text_len; i >= ctrl->textbox.cursor_pos; i--) {
                text[i + 1] = text[i];
            }
            text[ctrl->textbox.cursor_pos] = (char)key;
            ctrl->textbox.cursor_pos++;
            needs_redraw = 1;
        }
    }

    return needs_redraw;
}
