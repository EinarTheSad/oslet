#include "gui.h"

uint32_t sys_win_pump_events_kernel(gui_form_t *form) {
    if (!form) return 0;

    int ctrl_y_offset = 20;
    if (form->menubar_enabled) {
        ctrl_y_offset += menubar_get_height(&form->menubar);
    }

    form->clicked_id = -1;

    int mx = mouse_get_x();
    int my = mouse_get_y();
    uint8_t mb = mouse_get_buttons();

    gui_form_t *topmost = wm_get_window_at(&global_wm, mx, my);

    uint8_t button_pressed = (mb & 1) && !(form->last_mouse_buttons & 1);
    uint8_t button_released = !(mb & 1) && (form->last_mouse_buttons & 1);

    form->last_mouse_buttons = mb;

    int event_count = 0;
    int needs_redraw = 0;
    int z_order_changed = 0;
    int16_t changed_controls[32];
    int changed_count = 0;

    if (form->window_menu.visible) {
        int action = menu_handle_mouse(&form->window_menu, mx, my,
                                       button_pressed, button_released);
        if (action > 0) {
            if (action == MENU_ACTION_MAXIMIZE) {
                form->dragging = 0;
                form->resizing = 0;
                win_maximize(form);
                gui_request_full_redraw();
                return SYS_WIN_EVENT_RESIZE;
            } else if (action == MENU_ACTION_RESTORE) {
                form->dragging = 0;
                form->resizing = 0;
                win_restore_from_maximize(form);
                gui_request_full_redraw();
                return SYS_WIN_EVENT_RESIZE;
            } else if (action == MENU_ACTION_MINIMIZE) {
                int icon_x, icon_y;
                wm_get_next_icon_pos(&global_wm, &icon_x, &icon_y);
                const char *icon_path = form->icon_path[0] ? form->icon_path : NULL;
                win_minimize(form, icon_x, icon_y, icon_path);
                wm_claim_icon_slot(&global_wm, icon_x, icon_y);
                /* Let the desktop redraw run first to avoid compositing on stale wallpaper. */
                gui_request_full_redraw();
                return SYS_WIN_EVENT_WINDOW_CHANGED;
            } else if (action == MENU_ACTION_CLOSE) {
                return SYS_WIN_EVENT_CLOSE;
            }
        } else if (action == -1) {
            needs_redraw = 1;
        } else if (button_pressed && !menu_contains_point(&form->window_menu, mx, my)) {
            menu_hide(&form->window_menu);
            needs_redraw = 1;
        }
        if (form->window_menu.visible || action != 0) {
            if (needs_redraw) {
                return (uint32_t)SYS_WIN_EVENT_REDRAW;
            }
            return 0;
        }
    }

    /* Handle menubar if enabled */
    if (form->menubar_enabled && form->menubar.visible) {
        int bar_y = form->win.y + WM_TITLEBAR_HEIGHT + 2;
        int in_menubar = (mx >= form->win.x + 2 && mx < form->win.x + form->win.w - 2 &&
                          my >= bar_y && my < bar_y + MENUBAR_HEIGHT);
        int in_open_menu = 0;
        if (form->menubar.active_menu >= 0 &&
            form->menubar.active_menu < form->menubar.menu_count) {
            menu_t *active = &form->menubar.menus[form->menubar.active_menu].menu;
            in_open_menu = menu_contains_point(active, mx, my);
        }

        int action = menubar_handle_mouse(&form->menubar, form->win.x, form->win.y,
                                          mx, my, button_pressed, button_released);
        if (action > 0) {
            /* Menu item selected - return action ID as event */
            menubar_close_all(&form->menubar);
            form->clicked_id = action;
            event_count = 1;
            needs_redraw = 1;
        } else if (action == -1) {
            /* Menu closed without selection */
            needs_redraw = 1;
        } else if ((in_menubar || in_open_menu) && (button_pressed || button_released)) {
            needs_redraw = 1;
        }
        /* Redraw if needed */
        if (needs_redraw) {
            mouse_restore();
            mouse_invalidate_buffer();
            compositor_draw_single(&global_wm, form);
            if (action > 0) {
                return form->clicked_id;
            }
            return 0;
        }
    }

    /* Handle mouse button press */
    if (button_pressed) {
        /* If click landed on desktop (no window/icon), deselect any selected icons */
        if (topmost == NULL) {
            pump_deselect_all_icons(&global_wm);
            needs_redraw = 1;
        } else {
            /* Only process window clicks if topmost is not NULL */

            /* Ignore presses for windows that are not the topmost at the mouse position. This
               prevents clicks on controls/titlebars of windows that are overlapped by another
               (focused) window. Exception: allow clicks on open dropdown lists even if they
               extend outside the window bounds. */
            int click_on_dropdown = 0;
            if (topmost != form && form->controls) {
                /* Check if click is on an open dropdown list */
                for (int i = 0; i < form->ctrl_count; i++) {
                    gui_control_t *ctrl = &form->controls[i];
                    if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                        gui_rect_t list_rect;
                        gui_dropdown_list_rect(form, ctrl, ctrl_y_offset, &list_rect);

                        if (mx >= list_rect.x && mx < list_rect.x + list_rect.w &&
                            my >= list_rect.y && my < list_rect.y + list_rect.h) {
                            click_on_dropdown = 1;
                            break;
                        }
                    }
                }
            }

            if (topmost != form && !click_on_dropdown) {
                /* If click landed on another window, do not process this press for this form. */
            } else if (form->win.is_minimized) {
            /* Check for click on icon (single or double) */
            int icon_result = pump_handle_icon_click(form, mx, my);
            if (icon_result == 2) {
                /* Double-click - window restored */
                return SYS_WIN_EVENT_WINDOW_CHANGED;
            } else if (icon_result == 1) {
                /* Single click - selection changed */
                needs_redraw = 1;
            } else if (icon_result == 0) {
                /* Click outside this icon - check if we need to deselect */
                int icon_selected = 0;
                if (form->win.minimized_icon_id != -1 && form->controls) {
                    gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                    if (ctrl && ctrl->icon.checked) icon_selected = 1;
                }
                if (icon_selected) {
                    /* Check if click was on ANY other icon */
                    int clicked_other_icon = 0;
                    for (int i = 0; i < global_wm.count; i++) {
                        if (global_wm.windows[i] &&
                            global_wm.windows[i] != form &&
                            global_wm.windows[i]->win.is_minimized &&
                            win_is_icon_clicked(global_wm.windows[i], mx, my)) {
                            clicked_other_icon = 1;
                            break;
                        }
                    }
                    /* If no icon was clicked, deselect all */
                    if (!clicked_other_icon) {
                        pump_deselect_all_icons(&global_wm);
                        needs_redraw = 1;
                    }
                }
            }
        } else {
            /* Click landed on topmost window - process normally */
            /* Check if click is within window bounds (including open dropdown lists) */
            int check_x = form->win.x;
            int check_y = form->win.y;
            int check_w = form->win.w;
            int check_h = form->win.h;

            /* Expand bounds to include any open dropdown list */
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    gui_control_t *ctrl = &form->controls[i];
                    if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                        gui_rect_t list_rect;
                        gui_dropdown_list_rect(form, ctrl, ctrl_y_offset, &list_rect);

                        if (list_rect.y < check_y) {
                            check_h += (check_y - list_rect.y);
                            check_y = list_rect.y;
                        }
                        int list_bottom = list_rect.y + list_rect.h;
                        int window_bottom = form->win.y + form->win.h;
                        if (list_bottom > window_bottom) {
                            int extra_h = list_bottom - window_bottom;
                            if (check_h < form->win.h + extra_h) {
                                check_h = form->win.h + extra_h;
                            }
                        }
                    }
                }
            }

            if (mx >= check_x && mx < check_x + check_w &&
                my >= check_y && my < check_y + check_h) {
                z_order_changed = wm_bring_to_front(&global_wm, form);
                if (z_order_changed)
                    gui_request_full_redraw();
            }

            /* Check if click is on an open dropdown list FIRST (highest priority,
               as dropdowns can extend above the window and overlap the titlebar) */
            int dropdown_handled = 0;
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    gui_control_t *ctrl = &form->controls[i];
                    if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                        gui_rect_t list_rect;
                        gui_dropdown_list_rect(form, ctrl, ctrl_y_offset, &list_rect);

                        if (mx >= list_rect.x && mx < list_rect.x + list_rect.w &&
                            my >= list_rect.y && my < list_rect.y + list_rect.h) {
                            dropdown_handled = 1;
                            if (pump_handle_control_press(form, mx, my, ctrl_y_offset)) {
                                needs_redraw = 1;
                            }
                            break;
                        }
                    }
                }
            }

            if (!dropdown_handled) {
                /* Check minimize button */
                int min_result = pump_handle_minimize(form, mx, my);
                if (min_result == 2) {
                    /* Menu shown */
                    needs_redraw = 1;
                } else if (min_result == 1) {
                    return SYS_WIN_EVENT_REDRAW;
                }
                /* Check if resize corner was clicked */
                else if (pump_handle_resize_corner_click(form, mx, my)) {
                    /* Resizing started, continue */
                }
                /* Check if titlebar was clicked */
                else if (pump_handle_titlebar_click(form, mx, my)) {
                    /* Dragging started, continue */
                } else {
                    /* Find which control (if any) was pressed */
                    if (pump_handle_control_press(form, mx, my, ctrl_y_offset)) {
                        needs_redraw = 1;
                    }
                }
            }
        }
        }  /* Close the else block for topmost != NULL */
    }

    /* Check if an event was generated during button press (e.g., dropdown item selected) */
    if (form->clicked_id >= 0 && !button_released) {
        event_count = 1;
    }

    /* Handle mouse button release */
    if (button_released) {
        /* End window dragging - signal full redraw needed */
        int was_dragging = form->dragging;
        int was_resizing = form->resizing;
        if (form->dragging) {
            form->dragging = 0;
        }
        if (form->resizing) {
            form->resizing = 0;
        }

        /* End icon dragging - snap to slot */
        int was_icon_dragging = 0;
        if (form->win.is_minimized) {
            if (form->win.minimized_icon_id != -1 && form->controls) {
                gui_control_t *ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
                if (ctrl && ctrl->icon.dragging) {
                    was_icon_dragging = 1;
                    /* Restore old position's background */
                    if (ctrl->icon.saved_bg) {
                        int old_bg_x = ctrl->x - 1;
                        int old_bg_y = ctrl->y - 1;
                        int old_bg_w = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
                        int label_lines = icon_count_label_lines(ctrl->text, 49);
                        int old_bg_h = icon_calc_total_height(32, label_lines);
                        if (old_bg_x >= 0 && old_bg_y >= 0 &&
                            old_bg_x + old_bg_w + 2 <= WM_SCREEN_WIDTH &&
                            old_bg_y + old_bg_h + 2 <= WM_SCREEN_HEIGHT && (old_bg_x & 1) == 0) {
                            gfx_write_screen_region_packed(ctrl->icon.saved_bg, old_bg_w + 2, old_bg_h + 2, old_bg_x, old_bg_y);
                        }
                        kfree(ctrl->icon.saved_bg);
                        ctrl->icon.saved_bg = NULL;
                    }
                    /* Snap to nearest slot (slot already claimed during drag) */
                    int snap_x, snap_y;
                    wm_snap_to_slot(ctrl->x, ctrl->y, &snap_x, &snap_y);
                    /* If actually snapped to different position, update slot */
                    if (snap_x != ctrl->x || snap_y != ctrl->y) {
                        wm_release_icon_slot(&global_wm, ctrl->icon.original_x, ctrl->icon.original_y);
                        wm_claim_icon_slot(&global_wm, snap_x, snap_y);
                        ctrl->icon.original_x = snap_x;
                        ctrl->icon.original_y = snap_y;
                    }
                    ctrl->icon.dragging = 0;
                    ctrl_set_pos(form, ctrl->id, snap_x, snap_y);
                    /* Save snapped position's background */
                    int snap_bg_x = snap_x - 1;
                    int snap_bg_y = snap_y - 1;
                    int snap_bg_w = ctrl->w > 0 ? ctrl->w : WM_ICON_TOTAL_WIDTH;
                    int snap_label_lines = icon_count_label_lines(ctrl->text, 49);
                    int snap_bg_h = icon_calc_total_height(32, snap_label_lines);
                    int snap_row_bytes = (snap_bg_w + 3) / 2;
                    ctrl->icon.saved_bg = (uint8_t*)kmalloc(snap_row_bytes * (snap_bg_h + 2));
                    if (ctrl->icon.saved_bg) {
                        if (snap_bg_x >= 0 && snap_bg_y >= 0 &&
                            snap_bg_x + snap_bg_w + 2 <= WM_SCREEN_WIDTH &&
                            snap_bg_y + snap_bg_h + 2 <= WM_SCREEN_HEIGHT && (snap_bg_x & 1) == 0) {
                            gfx_read_screen_region_packed(ctrl->icon.saved_bg, snap_bg_w + 2, snap_bg_h + 2, snap_bg_x, snap_bg_y);
                        }
                    }
                }
            }
        }

        /* Check if release is on the same control that was pressed */
        if (form->press_control_id != -1 && form->controls) {
            for (int i = 0; i < form->ctrl_count; i++) {
                gui_control_t *ctrl = &form->controls[i];

                if (ctrl->id == form->press_control_id) {
                    /* Clear pressed state for buttons */
                    if (ctrl->type == CTRL_BUTTON) {
                        ctrl->button.pressed = 0;
                        needs_redraw = 1;  /* Button visual changed */
                        if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                    }

                    int abs_x = form->win.x + ctrl->x;
                    int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

                    int hit_w = ctrl->w;
                    int hit_h = ctrl->h;

                    if (ctrl->type == CTRL_CHECKBOX && ctrl->w == 13) {
                        hit_w = 100;
                    } else if (ctrl->type == CTRL_RADIOBUTTON && ctrl->w == 12) {
                        hit_w = 100;
                    } else if (ctrl->type == CTRL_ICON) {
                        /* Icon hit area: 48x58 (32px icon + 24px label + 2px spacing) */
                        hit_w = ctrl->w > 0 ? ctrl->w : 48;
                        hit_h = ctrl->h > 0 ? ctrl->h : 58;
                    } else if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                        /* Extended hit area when dropdown is open */
                        hit_h = ctrl->h + (ctrl->dropdown.item_count * 16);
                    }

                    /* Check if release is within same control */
                    if (mx >= abs_x && mx < abs_x + hit_w &&
                        my >= abs_y && my < abs_y + hit_h) {

                        /* Handle checkbox toggle */
                        if (ctrl->type == CTRL_CHECKBOX) {
                            ctrl->checkbox.checked = !ctrl->checkbox.checked;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                            /* Don't set clicked_id for checkbox - just redraw */
                        }

                        /* Handle radio button selection */
                        else if (ctrl->type == CTRL_RADIOBUTTON) {
                            /* Uncheck all radio buttons in the same group */
                            for (int j = 0; j < form->ctrl_count; j++) {
                                gui_control_t *other = &form->controls[j];
                                if (other->type == CTRL_RADIOBUTTON &&
                                    other->checkbox.group_id == ctrl->checkbox.group_id) {
                                    other->icon.checked = 0;
                                    if (changed_count < 32) changed_controls[changed_count++] = other->id;
                                }
                            }
                            /* Check this radio button */
                            ctrl->checkbox.checked = 1;
                            needs_redraw = 1;
                            /* Don't set clicked_id for radio - just redraw */
                        }

                        /* Handle icon click/double-click */
                        else if (ctrl->type == CTRL_ICON) {
                            uint32_t now = timer_get_ticks();

                            /* Deselect all other icons first */
                            for (int j = 0; j < form->ctrl_count; j++) {
                                gui_control_t *other = &form->controls[j];
                                if (other->type == CTRL_ICON && other->id != ctrl->id) {
                                    other->icon.checked = 0;
                                    if (changed_count < 32) changed_controls[changed_count++] = other->id;
                                }
                            }

                            /* Check for double-click */
                            if (form->last_icon_click_id == ctrl->id &&
                                (now - form->last_icon_click_time) < WM_DOUBLECLICK_TICKS) {
                                /* Double-click - activate the icon and deselect it */
                                ctrl->icon.checked = 0;
                                form->clicked_id = ctrl->id;
                                event_count = 1;
                                form->last_icon_click_id = -1;
                            } else {
                                /* Single click - just select */
                                ctrl->icon.checked = 1;
                                form->last_icon_click_time = now;
                                form->last_icon_click_id = ctrl->id;
                            }
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                            needs_redraw = 1;
                        }

                        /* Handle dropdown - selection generates event */
                        else if (ctrl->type == CTRL_DROPDOWN) {
                            /* Dropdown click was already handled in press, just generate event */
                            ctrl->dropdown.pressed = 0;
                            if (ctrl->dropdown.hovered_item == DROPDOWN_HOVER_SCROLLBAR) ctrl->dropdown.hovered_item = -1;
                            form->clicked_id = ctrl->id;
                            event_count = 1;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }

                        /* Handle scrollbar - clear pressed state and generate event */
                        else if (ctrl->type == CTRL_SCROLLBAR) {
                            ctrl->scrollbar.pressed = 0;
                            form->clicked_id = ctrl->id;
                            event_count = 1;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }

                        else if (ctrl->type == CTRL_TREEVIEW) {
                            ctrl->treeview.scrollbar_pressed = 0;
                            ctrl->treeview.scrollbar_hovered_item = -1;
                            ctrl->treeview.hscrollbar_pressed = 0;
                            ctrl->treeview.hscrollbar_hovered_item = -1;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }

                        else if (ctrl->type == CTRL_LISTBOX) {
                            ctrl->listbox.scrollbar_pressed = 0;
                            ctrl->listbox.scrollbar_hovered_item = -1;
                            needs_redraw = 1;
                            if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                        }

                        else if (ctrl->type == CTRL_TEXTBOX) {
                            if (ctrl->textbox.scrollbar_pressed) {
                                ctrl->textbox.scrollbar_pressed = 0;
                                ctrl->textbox.scrollbar_hovered_item = -1;
                                needs_redraw = 1;
                                if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                            }
                        }

                        /* Valid click detected for buttons and other controls */
                        else {
                            form->clicked_id = ctrl->id;
                            event_count = 1;
                        }
                    }
                    break;
                }
            }
        }

        /* Clear all pressed states on any button that might still be pressed */
        if (form->controls) {
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].type == CTRL_BUTTON && form->controls[i].button.pressed) {
                    form->controls[i].button.pressed = 0;
                    needs_redraw = 1;  /* Button visual changed */
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
                if (form->controls[i].type == CTRL_SCROLLBAR && form->controls[i].scrollbar.pressed) {
                    form->controls[i].scrollbar.pressed = 0;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
                if (form->controls[i].type == CTRL_TEXTBOX && form->controls[i].textbox.scrollbar_pressed) {
                    form->controls[i].textbox.scrollbar_pressed = 0;
                    form->controls[i].textbox.scrollbar_hovered_item = -1;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
                if (form->controls[i].type == CTRL_TREEVIEW && form->controls[i].treeview.scrollbar_pressed) {
                    form->controls[i].treeview.scrollbar_pressed = 0;
                    form->controls[i].treeview.scrollbar_hovered_item = -1;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
                if (form->controls[i].type == CTRL_TREEVIEW && form->controls[i].treeview.hscrollbar_pressed) {
                    form->controls[i].treeview.hscrollbar_pressed = 0;
                    form->controls[i].treeview.hscrollbar_hovered_item = -1;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
                if (form->controls[i].type == CTRL_LISTBOX && form->controls[i].listbox.scrollbar_pressed) {
                    form->controls[i].listbox.scrollbar_pressed = 0;
                    form->controls[i].listbox.scrollbar_hovered_item = -1;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
                if (form->controls[i].type == CTRL_DROPDOWN && form->controls[i].dropdown.pressed) {
                    form->controls[i].dropdown.pressed = 0;
                    if (form->controls[i].dropdown.hovered_item == DROPDOWN_HOVER_SCROLLBAR) form->controls[i].dropdown.hovered_item = -1;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                }
            }
        }

        /* Clear press state after release */
        form->press_control_id = -1;

        /* If we just finished dragging or resizing, signal full redraw needed */
        if (was_dragging || was_icon_dragging || was_resizing) {
            gui_request_full_redraw();  /* Desktop will do full redraw */
            if (was_resizing) {
                return (uint32_t)SYS_WIN_EVENT_RESIZE;
            }
            return (uint32_t)SYS_WIN_EVENT_WINDOW_CHANGED;
        }
    }

    /* Handle icon dragging (start drag after threshold, update during drag) */
    if (form->win.is_minimized && (mb & 1)) {
        if (form->win.minimized_icon_id != -1 && form->controls) {
            gui_control_t *icon_ctrl = sys_win_get_control(form, form->win.minimized_icon_id);
            if (icon_ctrl && icon_ctrl->icon.checked) {
                if (!icon_ctrl->icon.dragging) {
                    if (win_is_icon_clicked(form, icon_ctrl->icon.click_start_x, icon_ctrl->icon.click_start_y) ||
                        win_is_icon_clicked(form, mx, my)) {
                        int dx = mx - icon_ctrl->icon.click_start_x;
                        int dy = my - icon_ctrl->icon.click_start_y;
                        int threshold = WM_ICON_SLOT_WIDTH / 4;
                        if (dx * dx + dy * dy > threshold * threshold) {
                            icon_ctrl->icon.dragging = 1;
                            icon_ctrl->icon.drag_offset_x = mx - icon_ctrl->x;
                            icon_ctrl->icon.drag_offset_y = my - icon_ctrl->y;
                        }
                    }
                } else {
                    int new_x = mx - icon_ctrl->icon.drag_offset_x;
                    int new_y = my - icon_ctrl->icon.drag_offset_y;
                    if (new_x < 0) new_x = 0;
                    if (new_y < 0) new_y = 0;
                    if (new_x > WM_SCREEN_WIDTH - WM_ICON_TOTAL_WIDTH) new_x = WM_SCREEN_WIDTH - WM_ICON_TOTAL_WIDTH;
                    if (new_y > WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT - 48) new_y = WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT - 48;
                    if (new_x != icon_ctrl->x || new_y != icon_ctrl->y) {
                        int old_x = icon_ctrl->x;
                        int old_y = icon_ctrl->y;
                        int bg_w = icon_ctrl->w > 0 ? icon_ctrl->w : WM_ICON_TOTAL_WIDTH;
                        int label_lines = icon_count_label_lines(icon_ctrl->text, 49);
                        int bg_h = icon_calc_total_height(32, label_lines);
                        int save_w = bg_w + 2;
                        int save_h = bg_h + 2;
                        int row_bytes = (save_w + 1) / 2;
                        uint8_t *old_saved_bg = icon_ctrl->icon.saved_bg;
                        /* Restore OLD position using existing saved_bg */
                        if (old_saved_bg) {
                            int old_bg_x = old_x - 1;
                            int old_bg_y = old_y - 1;
                            for (int py = 0; py < save_h; py++) {
                                uint8_t *row = old_saved_bg + py * row_bytes;
                                for (int px = 0; px < save_w; px++) {
                                    int sx = old_bg_x + px;
                                    int sy = old_bg_y + py;
                                    if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                                        int byte_idx = px / 2;
                                        uint8_t packed = row[byte_idx];
                                        uint8_t pix = (px & 1) ? (packed & 0x0F) : (packed >> 4);
                                        gfx_putpixel(sx, sy, pix);
                                    }
                                }
                            }
                        }
                        /* Move icon to new position */
                        ctrl_set_pos(form, icon_ctrl->id, new_x, new_y);
                        /* Don't use compositor_draw_all - it would save bg WITH icon.
                           Instead, save bg at new position and draw icon directly. */
                        uint8_t *new_saved_bg = (uint8_t*)kmalloc(row_bytes * save_h);
                        if (new_saved_bg) {
                            int new_bg_x = new_x - 1;
                            int new_bg_y = new_y - 1;
                            for (int py = 0; py < save_h; py++) {
                                uint8_t *row = new_saved_bg + py * row_bytes;
                                for (int b = 0; b < row_bytes; b++) row[b] = 0;
                                for (int px = 0; px < save_w; px++) {
                                    int sx = new_bg_x + px;
                                    int sy = new_bg_y + py;
                                    uint8_t pix = 0;
                                    if (sx >= 0 && sx < WM_SCREEN_WIDTH && sy >= 0 && sy < WM_SCREEN_HEIGHT) {
                                        pix = gfx_getpixel(sx, sy);
                                    }
                                    int byte_idx = px / 2;
                                    if (px & 1) row[byte_idx] = (row[byte_idx] & 0xF0) | (pix & 0x0F);
                                    else row[byte_idx] = (row[byte_idx] & 0x0F) | (pix << 4);
                                }
                            }
                        }
                        /* Free old saved_bg and use new one */
                        if (old_saved_bg) kfree(old_saved_bg);
                        icon_ctrl->icon.saved_bg = new_saved_bg;
                        /* Draw icon at new position directly */
                        ctrl_draw_icon(icon_ctrl, new_x, new_y, 0);
                        mouse_restore();
                        needs_redraw = 0;
                    }
                }
            }
        }
    }

    /* Handle active window dragging */
    if (form->dragging && (mb & 1)) {
        int dx = mx - form->drag_start_x;
        int dy = my - form->drag_start_y;

        if (dx != 0 || dy != 0) {
            int had_saved_bg = form->win.saved_bg != NULL;
            win_move(&form->win, dx, dy);
            form->drag_start_x = mx;
            form->drag_start_y = my;
            if (global_wm.backgrounds_invalid || !had_saved_bg || !form->win.saved_bg) {
                gui_request_full_redraw();
            }
            mouse_restore();
            compositor_draw_all(&global_wm);
            needs_redraw = 0;  /* Already drawn */
        }
    }

    /* Handle active window resizing */
    if (form->resizing && (mb & 1)) {
        int dx = mx - form->resize_start_mx;
        int dy = my - form->resize_start_my;

        if (dx != 0 || dy != 0) {
            int new_w = form->resize_start_w + dx;
            int new_h = form->resize_start_h + dy;
            int had_saved_bg = form->win.saved_bg != NULL;
            win_resize(&form->win, new_w, new_h);
            if (global_wm.backgrounds_invalid || !had_saved_bg || !form->win.saved_bg) {
                gui_request_full_redraw();
            }
            mouse_restore();
            compositor_draw_all(&global_wm);
            needs_redraw = 0;  /* Already drawn */
        }
    }

    /* Handle textbox mouse selection (dragging to select text) */
    if (form->textbox_selecting && (mb & 1) && form->focused_control_id >= 0) {
        gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
        if (ctrl && ctrl->type == CTRL_TEXTBOX) {
            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

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
                if (needs_scrollbar) {
                    text_area_w -= 18;
                    if (text_area_w < 10) text_area_w = 10;
                }

                new_pos = textbox_pos_from_xy(&font_n, size, text, text_area_x, text_area_w,
                                              ctrl->textbox.current_line, rel_x, rel_y);
            } else {
                new_pos = textbox_pos_from_x(&font_n, size, text,
                                             ctrl->textbox.scroll_offset, rel_x);
            }

            /* Update selection end and cursor */
            if (new_pos != ctrl->textbox.sel_end) {
                ctrl->textbox.sel_end = new_pos;
                ctrl->textbox.cursor_pos = new_pos;
                needs_redraw = 1;
                if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
            }
        }
    }

    /* End textbox selection on mouse release */
    if (button_released && form->textbox_selecting) {
        form->textbox_selecting = 0;
        /* If sel_start == sel_end, clear selection */
        gui_control_t *ctrl = find_control_by_id(form, form->focused_control_id);
        if (ctrl && ctrl->type == CTRL_TEXTBOX) {
            if (ctrl->textbox.sel_start == ctrl->textbox.sel_end) {
                ctrl->textbox.sel_start = -1;
                ctrl->textbox.sel_end = -1;
            }
        }
    }

    /* Handle scrollbar thumb dragging (including dropdown inline scrollbar) */
    if ((mb & 1) && form->press_control_id >= 0 && form->controls) {
        gui_control_t *ctrl = find_control_by_id(form, form->press_control_id);

        /* Regular scrollbar control dragging */
        if (ctrl && ctrl->type == CTRL_SCROLLBAR && ctrl->scrollbar.hovered_item == 1 && ctrl->scrollbar.pressed) {
            int vertical = !ctrl->scrollbar.checked;
            int arrow_size = vertical ? ctrl->w : ctrl->h;
            int max_val = ctrl->scrollbar.max_length > 0 ? ctrl->scrollbar.max_length : 100;
            int length = vertical ? ctrl->h : ctrl->w;
            scrollbar_geom_t sb;
            gui_scrollbar_make(&sb, length, arrow_size,
                                ctrl->scrollbar.cursor_pos, max_val);

            int abs_x = form->win.x + ctrl->x;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;

            if (vertical) {
                int track_y = abs_y + arrow_size;
                int new_val = gui_scrollbar_value_from_pos(&sb, my - track_y,
                                                            ctrl->scrollbar.scroll_offset);
                if (new_val != ctrl->scrollbar.cursor_pos) {
                    ctrl->scrollbar.cursor_pos = new_val;
                    form->clicked_id = ctrl->id;
                    event_count = 1; /* Generate event during drag */
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            } else {
                int track_x = abs_x + arrow_size;
                int new_val = gui_scrollbar_value_from_pos(&sb, mx - track_x,
                                                            ctrl->scrollbar.scroll_offset);
                if (new_val != ctrl->scrollbar.cursor_pos) {
                    ctrl->scrollbar.cursor_pos = new_val;
                    form->clicked_id = ctrl->id;
                    event_count = 1; /* Generate event during drag */
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            }
        }

        if (ctrl && ctrl->type == CTRL_TREEVIEW && ctrl->treeview.scrollbar_pressed &&
            ctrl->treeview.scrollbar_hovered_item == 1) {
            int row_area_h = 1, max_val = 0;
            treeview_layout_sys(ctrl, NULL, &row_area_h, NULL, NULL, NULL, &max_val, NULL);
            int arrow_size = 18;
            int track_len = row_area_h - 2 * arrow_size;
            int thumb_size = 20;
            if (thumb_size > track_len) thumb_size = track_len;
            if (thumb_size < 1) thumb_size = 1;

            if (max_val > 0 && track_len > thumb_size) {
                int track_y = form->win.y + ctrl->y + ctrl_y_offset + 2 + arrow_size;
                int rel_y = my - track_y - ctrl->treeview.scrollbar_drag_offset;
                if (rel_y < 0) rel_y = 0;
                if (rel_y > track_len - thumb_size) rel_y = track_len - thumb_size;
                int new_val = (rel_y * max_val) / (track_len - thumb_size);
                if (new_val > max_val) new_val = max_val;
                if (new_val != ctrl->treeview.scroll_offset) {
                    ctrl->treeview.scroll_offset = new_val;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            }
        }

        if (ctrl && ctrl->type == CTRL_TREEVIEW && ctrl->treeview.hscrollbar_pressed &&
            ctrl->treeview.hscrollbar_hovered_item == 1) {
            int content_w = 1, row_area_h = 1, max_val = 0;
            treeview_layout_sys(ctrl, &content_w, &row_area_h, NULL, NULL, NULL, NULL, &max_val);
            int arrow_size = 18;
            int track_len = content_w - 2 * arrow_size;
            int thumb_size = 20;
            if (thumb_size > track_len) thumb_size = track_len;
            if (thumb_size < 1) thumb_size = 1;

            if (max_val > 0 && track_len > thumb_size) {
                int track_x = form->win.x + ctrl->x + 2 + arrow_size;
                int rel_x = mx - track_x - ctrl->treeview.hscrollbar_drag_offset;
                if (rel_x < 0) rel_x = 0;
                if (rel_x > track_len - thumb_size) rel_x = track_len - thumb_size;
                int new_val = (rel_x * max_val) / (track_len - thumb_size);
                if (new_val > max_val) new_val = max_val;
                if (new_val != ctrl->treeview.hscroll_offset) {
                    ctrl->treeview.hscroll_offset = new_val;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            }
        }

        if (ctrl && ctrl->type == CTRL_LISTBOX && ctrl->listbox.scrollbar_pressed &&
            ctrl->listbox.scrollbar_hovered_item == 1) {
            int inner_h = ctrl->h > 4 ? ctrl->h - 4 : ctrl->h;
            int max_val = listbox_max_scroll_sys(ctrl);
            int arrow_size = 18;
            int track_len = inner_h - 2 * arrow_size;
            int thumb_size = 20;
            if (thumb_size > track_len) thumb_size = track_len;
            if (thumb_size < 1) thumb_size = 1;

            if (max_val > 0 && track_len > thumb_size) {
                int track_y = form->win.y + ctrl->y + ctrl_y_offset + 2 + arrow_size;
                int rel_y = my - track_y - ctrl->listbox.scrollbar_drag_offset;
                if (rel_y < 0) rel_y = 0;
                if (rel_y > track_len - thumb_size) rel_y = track_len - thumb_size;
                int new_val = (rel_y * max_val) / (track_len - thumb_size);
                if (new_val > max_val) new_val = max_val;
                if (new_val != ctrl->listbox.scroll_offset) {
                    ctrl->listbox.scroll_offset = new_val;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            }
        }

        /* Textbox inline scrollbar dragging */
        if (ctrl && ctrl->type == CTRL_TEXTBOX && ctrl->textbox.scrollbar_pressed && ctrl->textbox.scrollbar_hovered_item == 1) {
            extern bmf_font_t font_n;
            int abs_y = form->win.y + ctrl->y + ctrl_y_offset;
            int text_area_w = ctrl->w - 6;
            int text_area_h = ctrl->h - 9;
            int seq_idx = -1;
            for (int j = 0; j < font_n.size_count; j++) {
                if (font_n.sequences[j].point_size == (ctrl->font_size > 0 ? ctrl->font_size : 12)) {
                    seq_idx = j;
                    break;
                }
            }
            int font_height = seq_idx >= 0 ? font_n.sequences[seq_idx].height : (ctrl->font_size > 0 ? ctrl->font_size : 12);
            int line_height = font_height + 2;
            int visible_lines = text_area_h / line_height;
            if (visible_lines < 1) visible_lines = 1;
            int total_lines = textbox_wrap_line_count(&font_n, (ctrl->font_size > 0 ? ctrl->font_size : 12), textbox_get_text(ctrl), text_area_w);
            int max_scroll = total_lines > visible_lines ? total_lines - visible_lines : 0;

            if (total_lines > visible_lines && max_scroll > 0) {
                int arrow_size = 18;
                int track_len = text_area_h - arrow_size * 2;
                int thumb_size = 20;
                if (thumb_size > track_len) thumb_size = track_len;
                int track_y = abs_y + 6 + arrow_size;

                int rel_y = my - track_y - ctrl->textbox.scroll_offset;
                if (rel_y < 0) rel_y = 0;
                if (rel_y > track_len - thumb_size) rel_y = track_len - thumb_size;
                int new_line = 0;
                if (track_len - thumb_size > 0) {
                    new_line = (rel_y * max_scroll) / (track_len - thumb_size);
                }
                if (new_line > max_scroll) new_line = max_scroll;
                if (new_line < 0) new_line = 0;
                if (new_line != ctrl->textbox.current_line) {
                    ctrl->textbox.current_line = new_line;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            }
        }

        if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open &&
            ctrl->dropdown.pressed && ctrl->dropdown.hovered_item == DROPDOWN_HOVER_SCROLLBAR) {
            int item_h = 16;
            gui_rect_t list_rect;
            gui_dropdown_list_rect(form, ctrl, ctrl_y_offset, &list_rect);

            int visible_count = list_rect.h / item_h;
            if (visible_count < 1) visible_count = 1;
            int max_val = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
            if (max_val > 0) {
                scrollbar_geom_t sb;
                gui_scrollbar_make(&sb, list_rect.h, 18, ctrl->dropdown.dropdown_scroll, max_val);
                int track_y = list_rect.y + sb.arrow_size;
                int new_val = gui_scrollbar_value_from_pos(&sb, my - track_y,
                                                            ctrl->dropdown.scroll_offset);
                if (new_val != ctrl->dropdown.dropdown_scroll) {
                    ctrl->dropdown.dropdown_scroll = new_val;
                    form->clicked_id = ctrl->id;
                    event_count = 1;
                    needs_redraw = 1;
                    if (changed_count < 32) changed_controls[changed_count++] = ctrl->id;
                }
            }
        }
    }

    if (pump_update_dropdown_hover(form, mx, my, ctrl_y_offset)) {
        needs_redraw = 1;
        for (int i = 0; i < form->ctrl_count; i++) {
            if (form->controls[i].type == CTRL_DROPDOWN && form->controls[i].dropdown.dropdown_open) {
                if (changed_count < 32) changed_controls[changed_count++] = form->controls[i].id;
                break;
            }
        }
    }

    /* Handle keyboard input for focused textbox. Only the globally focused window
       should receive keyboard events (prevents background windows from consuming keys) */
    if (!form->win.is_minimized && form->focused_control_id >= 0 &&
        global_wm.focused_index >= 0 && global_wm.windows[global_wm.focused_index] == form) {
        int keyboard_result = pump_handle_keyboard(form);
        if (keyboard_result == 2) {
            event_count = 1;
        } else if (keyboard_result) {
            needs_redraw = 1;
            if (changed_count < 32) changed_controls[changed_count++] = form->focused_control_id;
        }
    }

    /* Return clicked control ID directly (0 if none clicked) */
    if (event_count > 0) {
        /* If button was pressed, we need to redraw to show unpressed state */
        if (needs_redraw) {
            mouse_restore();
            mouse_invalidate_buffer();
            if (changed_count > 0) {
                for (int i = 0; i < changed_count; i++) {
                    gui_control_t *ctrl = find_control_by_id(form, changed_controls[i]);
                    if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                        compositor_draw_dropdown_list_only(&global_wm, form, changed_controls[i]);
                    } else {
                        compositor_draw_control_by_id(&global_wm, form, changed_controls[i]);
                    }
                }
            } else if (z_order_changed) {
                /* Z-order changed: full composite so no lower window paints over the new top */
                compositor_draw_all(&global_wm);
            } else {
                compositor_draw_single(&global_wm, form);
            }
        }
        return form->clicked_id;
    }

    /* Return -1 if visual state changed and needs redraw */
    if (needs_redraw) {
        mouse_restore();
        mouse_invalidate_buffer();
        if (changed_count > 0) {
            for (int i = 0; i < changed_count; i++) {
                gui_control_t *ctrl = find_control_by_id(form, changed_controls[i]);
                if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown.dropdown_open) {
                    compositor_draw_dropdown_list_only(&global_wm, form, changed_controls[i]);
                } else {
                    compositor_draw_control_by_id(&global_wm, form, changed_controls[i]);
                }
            }
        } else if (z_order_changed) {
            /* Z-order changed: full composite so no lower window paints over the new top */
            compositor_draw_all(&global_wm);
        } else {
            compositor_draw_single(&global_wm, form);
        }
        /* Return clicked_id if it was set (e.g., scrollbar value changed), otherwise -1 */
        return (form->clicked_id >= 0) ? (uint32_t)form->clicked_id : (uint32_t)SYS_WIN_EVENT_REDRAW;
    }

    return 0;
}
