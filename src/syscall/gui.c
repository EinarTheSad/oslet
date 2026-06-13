#include "gui.h"

/* Global window manager */
window_manager_t global_wm;
int wm_initialized = 0;

static void gui_picturebox_free_data(gui_control_t *ctrl) {
    if (!ctrl || (ctrl->type & 0x7F) != CTRL_PICTUREBOX) return;

    if (ctrl->picturebox.cached_bitmap_orig) {
        bitmap_free(ctrl->picturebox.cached_bitmap_orig);
        ctrl->picturebox.cached_bitmap_orig = NULL;
    }
    if (ctrl->picturebox.cached_bitmap_scaled) {
        bitmap_free(ctrl->picturebox.cached_bitmap_scaled);
        ctrl->picturebox.cached_bitmap_scaled = NULL;
    }
    if (ctrl->picturebox.buffer) {
        kfree(ctrl->picturebox.buffer);
        ctrl->picturebox.buffer = NULL;
    }
    ctrl->picturebox.buffer_w = 0;
    ctrl->picturebox.buffer_h = 0;
    ctrl->picturebox.load_failed = 0;
}

void gui_invalidate_saved_backgrounds(window_manager_t *wm) {
    if (!wm) return;

    for (int i = 0; i < wm->count; i++) {
        gui_form_t *f = wm->windows[i];
        if (!f) continue;

        if (f->win.saved_bg) {
            kfree(f->win.saved_bg);
            f->win.saved_bg = NULL;
        }
        f->win.dirty = 1;

        if (f->controls) {
            for (int j = 0; j < f->ctrl_count; j++) {
                gui_control_t *ctrl = &f->controls[j];

                if ((ctrl->type & 0x7F) == CTRL_ICON && ctrl->icon.saved_bg) {
                    kfree(ctrl->icon.saved_bg);
                    ctrl->icon.saved_bg = NULL;
                }

                if ((ctrl->type & 0x7F) == CTRL_DROPDOWN &&
                    ctrl->dropdown.dropdown_saved_bg) {
                    kfree(ctrl->dropdown.dropdown_saved_bg);
                    ctrl->dropdown.dropdown_saved_bg = NULL;
                }
            }
        }

        if (f->window_menu.saved_bg) {
            kfree(f->window_menu.saved_bg);
            f->window_menu.saved_bg = NULL;
        }
    }
}

void gui_request_full_redraw(void) {
    global_wm.needs_full_redraw = 1;
    global_wm.backgrounds_invalid = 1;
}

int current_task_owns_focused_window(void) {
    if (!wm_initialized || global_wm.count <= 0)
        return 1;

    if (global_wm.focused_index < 0 || global_wm.focused_index >= global_wm.count)
        return 0;

    gui_form_t *focused = global_wm.windows[global_wm.focused_index];
    if (!focused || focused->win.is_minimized || !focused->win.is_visible)
        return 0;

    task_t *current = task_get_current();
    if (!current)
        return focused->owner_tid == 0;

    return focused->owner_tid == current->tid;
}

uint32_t handle_window(uint32_t al, uint32_t ebx,
                               uint32_t ecx, uint32_t edx) {
    switch (al) {
        case 0x00: { /* SYS_WIN_MSGBOX - Modal message box */
            char msg[256];
            char btn[64];
            char title[64];
            if (sys_copy_string(msg, ebx, sizeof(msg)) != 0) return (uint32_t)-1;
            if (sys_copy_string(btn, ecx, sizeof(btn)) != 0) return (uint32_t)-1;
            if (sys_copy_string(title, edx, sizeof(title)) != 0) return (uint32_t)-1;

            if (buffer_valid) {
                mouse_restore();
                mouse_invalidate_buffer();
                gfx_swap_buffers();
            }

            /* Create msgbox (allocate dynamically to support multiple concurrent msgboxes) */
            msgbox_t *box = (msgbox_t*)kmalloc(sizeof(msgbox_t));
            if (!box) return -1;

            win_msgbox_create(box, msg, btn, title);
            win_msgbox_draw(box);

            /* Get initial mouse position and draw cursor */
            int mx = mouse_get_x();
            int my = mouse_get_y();

            /* Initial cursor draw (save new background) */
            mouse_save(mx, my);
            mouse_draw_cursor(mx, my);
            gfx_swap_buffers();

            /* Modal event loop */
            int dragging = 0;
            int drag_start_x = 0, drag_start_y = 0;
            uint8_t last_mb = 0;

            while (1) {
                mx = mouse_get_x();
                my = mouse_get_y();
                uint8_t mb = mouse_get_buttons();

                /* Detect button click */
                uint8_t button_pressed = (mb & 1) && !(last_mb & 1);
                uint8_t button_released = !(mb & 1) && (last_mb & 1);

                /* Handle dragging */
                if (button_pressed) {
                    if (win_is_titlebar(&box->base, mx, my)) {
                        dragging = 1;
                        drag_start_x = mx;
                        drag_start_y = my;
                    }
                }

                if (button_released) {
                    /* Check if button was clicked */
                    if (!dragging) {
                        int clicked = win_msgbox_handle_click(box, mx, my);
                        if (clicked) {
                                     /* Button clicked - restore cursor and window background */
                                     mouse_invalidate_buffer();
                                     win_restore_background(&box->base);
                                     gfx_swap_buffers();
                                     kfree(box);
                                     return clicked;
                        }
                    }
                    dragging = 0;
                }

                /* Perform dragging */
                if (dragging && (mb & 1)) {
                    int dx = mx - drag_start_x;
                    int dy = my - drag_start_y;

                    if (dx != 0 || dy != 0) {
                        win_move(&box->base, dx, dy);
                        drag_start_x = mx;
                        drag_start_y = my;

                        /* Redraw window and cursor (save new position during drag) */
                        win_msgbox_draw(box);
                        mouse_save(mx, my);
                        mouse_draw_cursor(mx, my);
                        gfx_swap_buffers();
                        continue;
                    }
                }

                last_mb = mb;

                /* Smart cursor update: restore old, save new, draw new */
                if (buffer_valid) {
                    mouse_restore();
                }
                mouse_save(mx, my);
                mouse_draw_cursor(mx, my);
                gfx_swap_buffers();
            }

            return 0;
        }

        case 0x05: { /* SYS_WIN_CREATE_FORM */
            char title[64];
            if (sys_copy_string(title, ebx, sizeof(title)) != 0) return (uint32_t)NULL;

            /* Initialize window manager on first use */
            if (!wm_initialized) {
                wm_init(&global_wm);
                wm_initialized = 1;
            }

            gui_form_t *form = (gui_form_t*)kmalloc(sizeof(gui_form_t));
            if (!form) return (uint32_t)NULL;

            int x = (int16_t)(ecx >> 16);
            int y = (int16_t)(ecx & 0xFFFF);
            int w = (int16_t)(edx >> 16);
            int h = (int16_t)(edx & 0xFFFF);

            win_create(&form->win, x, y, w, h, title);
            form->controls = NULL;
            form->ctrl_count = 0;
            form->clicked_id = -1;
            form->last_mouse_buttons = 0;
            form->press_control_id = -1;
            form->dragging = 0;
            form->drag_start_x = 0;
            form->drag_start_y = 0;
            form->resizing = 0;
            form->resize_start_w = 0;
            form->resize_start_h = 0;
            form->resize_start_mx = 0;
            form->resize_start_my = 0;
            form->icon_path[0] = '\0';
            form->focused_control_id = -1;  /* No control focused initially */
            form->textbox_selecting = 0;
            form->last_icon_click_time = 0;
            form->last_icon_click_id = -1;
            form->window_menu_initialized = 0;
            /* Initialize menu structure to safe defaults */
            form->window_menu.visible = 0;
            form->window_menu.saved_bg = NULL;
            form->window_menu.item_count = 0;
            form->window_menu.hovered_item = -1;
            /* Initialize menubar */
            form->menubar_enabled = 0;

            /* Track owner task for cleanup on process exit */
            task_t *current = task_get_current();
            form->owner_tid = current ? current->tid : 0;

            /* If the owning task has a default icon, apply it to the form */
            if (current && current->icon_path[0]) {
                strcpy_s(form->icon_path, current->icon_path, 64);
            }

            /* Register window with window manager */
            if (!wm_register_window(&global_wm, form)) {
                /* Failed to register - cleanup */
                kfree(form);
                return (uint32_t)NULL;
            }

            /* Draw the newly-created window into the back buffer and
               request a desktop redraw so the compositor includes the
               new window immediately. Without this the window may be
               overwritten until another action (e.g. moving Start
               Manager) forces a redraw. */
            compositor_draw_single(&global_wm, form);
            gui_request_full_redraw();

            return (uint32_t)form;
        }

        case 0x07: /* SYS_WIN_PUMP_EVENTS */
            return sys_win_pump_events_kernel((gui_form_t*)ebx);

        case 0x08: { /* SYS_WIN_ADD_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            gui_control_t *ctrl = (gui_control_t*)ecx;

            if (!form || !ctrl) return 0;

            /* Allocate or expand controls array */
            if (!form->controls) {
                form->controls = (gui_control_t*)kmalloc(sizeof(gui_control_t) * WM_MAX_CONTROLS_PER_FORM);
                if (!form->controls) return 0;
            }

            /* Add control (simple append for now) */
            if (form->ctrl_count < WM_MAX_CONTROLS_PER_FORM) {
                /* Copy the control data */
                gui_control_t *dest = &form->controls[form->ctrl_count];
                dest->id = ctrl->id;
                dest->type = ctrl->type;
                dest->font_type = ctrl->font_type;
                dest->font_size = ctrl->font_size;
                dest->x = ctrl->x;
                dest->y = ctrl->y;
                dest->w = ctrl->w;
                dest->h = ctrl->h;
                dest->fg = ctrl->fg;
                dest->bg = ctrl->bg;
                dest->border = ctrl->border;
                dest->border_color = ctrl->border_color;
                strcpy_s(dest->text, ctrl->text, 256);

                /* Initialize union based on control type (mask off hidden flag 0x80) */
                switch (dest->type & 0x7F) {
                    case CTRL_BUTTON:
                        dest->button.cached_bitmap_orig = NULL;
                        dest->button.pressed = 0;
                        break;
                    case CTRL_PICTUREBOX:
                        dest->picturebox.cached_bitmap_orig = NULL;
                        dest->picturebox.cached_bitmap_scaled = NULL;
                        dest->picturebox.buffer = NULL;
                        dest->picturebox.buffer_w = 0;
                        dest->picturebox.buffer_h = 0;
                        dest->picturebox.image_mode = ctrl->picturebox.image_mode;
                        dest->picturebox.load_failed = 0;
                        break;
                    case CTRL_CHECKBOX:
                        dest->checkbox.checked = 0;
                        dest->checkbox.group_id = ctrl->checkbox.group_id;
                        break;
                    case CTRL_RADIOBUTTON:
                        dest->radiobutton.checked = 0;
                        dest->radiobutton.group_id = ctrl->radiobutton.group_id;
                        break;
                    case CTRL_TEXTBOX:
                        dest->textbox.cursor_pos = 0;
                        dest->textbox.scroll_offset = 0;
                        dest->textbox.is_focused = 0;
                        dest->textbox.sel_start = -1;
                        dest->textbox.sel_end = -1;
                        dest->textbox.current_line = 0;
                        if (ctrl->textbox.is_multiline) {
                            dest->textbox.is_multiline = 1;
                            dest->textbox.max_length = TEXTBOX_MULTILINE_SIZE;
                            dest->textbox.multiline_text = (char*)kmalloc(TEXTBOX_MULTILINE_SIZE);
                            if (dest->textbox.multiline_text) {
                                memset_s(dest->textbox.multiline_text, 0, TEXTBOX_MULTILINE_SIZE);
                                strcpy_s(dest->textbox.multiline_text, ctrl->text, TEXTBOX_MULTILINE_SIZE);
                            }
                        } else {
                            dest->textbox.is_multiline = 0;
                            dest->textbox.max_length = ctrl->textbox.max_length > 0 ? ctrl->textbox.max_length : 255;
                            dest->textbox.multiline_text = NULL;
                        }
                        break;
                    case CTRL_DROPDOWN:
                        dest->dropdown.dropdown_open = 0;
                        dest->dropdown.item_count = ctrl->dropdown.item_count;
                        dest->dropdown.hovered_item = -1;
                        dest->dropdown.dropdown_saved_bg = NULL;
                        dest->dropdown.dropdown_saved_w = 0;
                        dest->dropdown.dropdown_saved_h = 0;
                        dest->dropdown.dropdown_saved_x = 0;
                        dest->dropdown.dropdown_saved_y = 0;
                        dest->dropdown.dropdown_scroll = 0;
                        dest->dropdown.pressed = 0;
                        dest->dropdown.scroll_offset = 0;
                        dest->dropdown.cursor_pos = 0;
                        break;
                    case CTRL_SCROLLBAR:
                        dest->scrollbar.hovered_item = -1;
                        dest->scrollbar.pressed = 0;
                        dest->scrollbar.cursor_pos = ctrl->scrollbar.cursor_pos;
                        dest->scrollbar.max_length = ctrl->scrollbar.max_length > 0 ? ctrl->scrollbar.max_length : 100;
                        dest->scrollbar.checked = ctrl->scrollbar.checked;
                        dest->scrollbar.scroll_offset = 0;
                        break;
                    case CTRL_TREEVIEW:
                        dest->treeview.items = (sys_tree_item_t*)kmalloc(sizeof(sys_tree_item_t) * TREEVIEW_MAX_ITEMS);
                        dest->treeview.item_count = 0;
                        dest->treeview.max_items = dest->treeview.items ? TREEVIEW_MAX_ITEMS : 0;
                        dest->treeview.selected_index = -1;
                        dest->treeview.scroll_offset = 0;
                        dest->treeview.hscroll_offset = 0;
                        dest->treeview.content_width = 0;
                        dest->treeview.row_height = ctrl->treeview.row_height ? ctrl->treeview.row_height : 18;
                        dest->treeview.scrollbar_hovered_item = -1;
                        dest->treeview.scrollbar_pressed = 0;
                        dest->treeview.scrollbar_drag_offset = 0;
                        dest->treeview.hscrollbar_hovered_item = -1;
                        dest->treeview.hscrollbar_pressed = 0;
                        dest->treeview.hscrollbar_drag_offset = 0;
                        dest->treeview.last_action = TREE_ACTION_NONE;
                        dest->treeview.action_index = -1;
                        dest->treeview.icon_closed = NULL;
                        dest->treeview.icon_open = NULL;
                        strcpy_s(dest->treeview.icon_closed_path, "C:/ICONS/FLD.ICO", sizeof(dest->treeview.icon_closed_path));
                        strcpy_s(dest->treeview.icon_open_path, "C:/ICONS/FLO.ICO", sizeof(dest->treeview.icon_open_path));
                        dest->treeview.icon_closed_failed = 0;
                        dest->treeview.icon_open_failed = 0;
                        break;
                    case CTRL_LISTBOX:
                        dest->listbox.items = (sys_list_item_t*)kmalloc(sizeof(sys_list_item_t) * LISTBOX_MAX_ITEMS);
                        dest->listbox.item_count = 0;
                        dest->listbox.max_items = dest->listbox.items ? LISTBOX_MAX_ITEMS : 0;
                        dest->listbox.selected_index = -1;
                        dest->listbox.scroll_offset = 0;
                        dest->listbox.row_height = ctrl->listbox.row_height ? ctrl->listbox.row_height : 16;
                        dest->listbox.scrollbar_hovered_item = -1;
                        dest->listbox.scrollbar_pressed = 0;
                        dest->listbox.scrollbar_drag_offset = 0;
                        dest->listbox.last_action = LIST_ACTION_NONE;
                        dest->listbox.action_index = -1;
                        break;
                    case CTRL_ICON:
                        dest->icon.cached_bitmap_orig = NULL;
                        dest->icon.saved_bg = NULL;
                        dest->icon.checked = 0;
                        dest->icon.dragging = 0;
                        dest->icon.drag_offset_x = 0;
                        dest->icon.drag_offset_y = 0;
                        dest->icon.original_x = 0;
                        dest->icon.original_y = 0;
                        dest->icon.click_start_x = 0;
                        dest->icon.click_start_y = 0;
                        dest->icon.use_desktop_text_color = ctrl->icon.use_desktop_text_color;
                        break;
                    default:
                        /* No special initialization */
                        break;
                }

                /* For textbox, update cursor to end of text */
                if (dest->type == CTRL_TEXTBOX) {
                    char *text_src = dest->textbox.is_multiline && dest->textbox.multiline_text
                        ? dest->textbox.multiline_text : dest->text;
                    int len = 0;
                    while (text_src[len]) len++;
                    dest->textbox.cursor_pos = len;
                }

                form->ctrl_count++;
            }
            return 0;
        }

        case 0x09: { /* SYS_WIN_DRAW */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            /* Use compositor to draw with correct focus state */
            compositor_draw_single(&global_wm, form);
            return 0;
        }

        case 0x0A: { /* SYS_WIN_DESTROY_FORM */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;

            /* Release icon slot if window was minimized */
            if (form->win.is_minimized) {
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
            }

            /* Unregister from window manager */
            wm_unregister_window(&global_wm, form);

            /* Free cached bitmaps in controls and restore any open dropdown backgrounds */
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    gui_control_t *ctrl = &form->controls[i];

                    switch (ctrl->type & 0x7F) {
                        case CTRL_BUTTON:
                            if (ctrl->button.cached_bitmap_orig) {
                                bitmap_free(ctrl->button.cached_bitmap_orig);
                                ctrl->button.cached_bitmap_orig = NULL;
                            }
                            break;

                        case CTRL_PICTUREBOX:
                            gui_picturebox_free_data(ctrl);
                            break;

                        case CTRL_ICON:
                            if (ctrl->icon.cached_bitmap_orig) {
                                bitmap_free(ctrl->icon.cached_bitmap_orig);
                                ctrl->icon.cached_bitmap_orig = NULL;
                            }
                            if (ctrl->icon.saved_bg) {
                                kfree(ctrl->icon.saved_bg);
                                ctrl->icon.saved_bg = NULL;
                            }
                            break;

                        case CTRL_DROPDOWN:
                            if (ctrl->dropdown.dropdown_saved_bg) {
                                ctrl_hide_dropdown_list(&form->win, ctrl);
                            }
                            break;

                        case CTRL_TEXTBOX:
                            if (ctrl->textbox.multiline_text) {
                                kfree(ctrl->textbox.multiline_text);
                                ctrl->textbox.multiline_text = NULL;
                            }
                            break;

                        case CTRL_TREEVIEW:
                            if (ctrl->treeview.items) {
                                kfree(ctrl->treeview.items);
                                ctrl->treeview.items = NULL;
                            }
                            if (ctrl->treeview.icon_closed) {
                                bitmap_free(ctrl->treeview.icon_closed);
                                ctrl->treeview.icon_closed = NULL;
                            }
                            if (ctrl->treeview.icon_open) {
                                bitmap_free(ctrl->treeview.icon_open);
                                ctrl->treeview.icon_open = NULL;
                            }
                            break;

                        case CTRL_LISTBOX:
                            if (ctrl->listbox.items) {
                                kfree(ctrl->listbox.items);
                                ctrl->listbox.items = NULL;
                            }
                            break;

                        default:
                            break;
                    }
                }
                kfree(form->controls);
                form->controls = NULL;
            }

            if (form->window_menu_initialized) {
                menu_destroy(&form->window_menu);
            }

            win_destroy(&form->win);

            kfree(form);

            gui_request_full_redraw();

            return 0;
        }

        case 0x0B: { /* SYS_WIN_SET_ICON */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *icon_path = (const char*)ecx;

            if (!form || !icon_path) return 0;

            /* Store icon path for later use when minimizing */
            strcpy_s(form->icon_path, icon_path, 64);

            /* Create a CTRL_ICON control on the form if not already created */
            #define FORM_ICON_CONTROL_ID 5000
            int existing_icon_id = -1;
            if (form->controls) {
                for (int i = 0; i < form->ctrl_count; i++) {
                    /* Check for CTRL_ICON with or without hidden flag (0x80) */
                    if ((form->controls[i].type & 0x7F) == CTRL_ICON &&
                        form->controls[i].id == FORM_ICON_CONTROL_ID) {
                        existing_icon_id = i;
                        break;
                    }
                }
            }

            if (existing_icon_id == -1) {
                /* Create new icon control - hidden (0x80 flag makes it invisible) */
                gui_control_t icon_ctrl = {0};
                icon_ctrl.type = CTRL_ICON | 0x80;  /* Hidden flag */
                icon_ctrl.id = FORM_ICON_CONTROL_ID;
                icon_ctrl.x = -100;  /* Off-screen */
                icon_ctrl.y = -100;
                icon_ctrl.w = 0;
                icon_ctrl.h = 0;
                icon_ctrl.fg = 0;
                icon_ctrl.bg = 15;
                icon_ctrl.icon.use_desktop_text_color = 1;
                strcpy_s(icon_ctrl.text, form->win.title, 256);

                sys_win_add_control(form, &icon_ctrl);
                ctrl_set_image(form, FORM_ICON_CONTROL_ID, icon_path);
            } else {
                /* Update existing icon control */
                gui_control_t *ctrl = &form->controls[existing_icon_id];
                strcpy_s(ctrl->text, form->win.title, 256);
                ctrl->icon.use_desktop_text_color = 1;
                ctrl_set_image(form, FORM_ICON_CONTROL_ID, icon_path);
            }

            return 0;
        }

        case 0x0C: { /* SYS_WIN_REDRAW_ALL */
            if (global_wm.backgrounds_invalid) {
                gui_invalidate_saved_backgrounds(&global_wm);
                if (!global_wm.needs_full_redraw) {
                    global_wm.backgrounds_invalid = 0;
                }
            }
            compositor_draw_all(&global_wm);
            return 0;
        }

        case 0x0D: { /* SYS_WIN_GET_CONTROL */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t id = (int16_t)ecx;

            if (!form) return 0;

            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == id) {
                    return (uint32_t)&form->controls[i];
                }
            }
            return 0;
        }

        case 0x0E: { /* SYS_WIN_CTRL_SET_PROP */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t ctrl_id = (int16_t)(ecx >> 16);
            int16_t prop_id = (int16_t)(ecx & 0xFFFF);
            uint32_t value = edx;

            if (!form) return 0;

            /* Find control */
            gui_control_t *ctrl = NULL;
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == ctrl_id) {
                    ctrl = &form->controls[i];
                    break;
                }
            }
            if (!ctrl) return 0;

            switch (prop_id) {
                case 0: /* PROP_TEXT - universal */
                    if (value) {
                        if (ctrl->type == CTRL_TEXTBOX && ctrl->textbox.is_multiline && ctrl->textbox.multiline_text) {
                            strcpy_s(ctrl->textbox.multiline_text, (const char*)value, ctrl->textbox.max_length);
                            ctrl->textbox.cursor_pos = 0;
                            ctrl->textbox.sel_start = -1;
                            ctrl->textbox.sel_end = -1;
                        } else {
                            strcpy_s(ctrl->text, (const char*)value, 256);
                        }
                    }
                    break;
                case PROP_TEXTBOX_EDIT:
                    textbox_run_edit_command(ctrl, (int)value);
                    break;
                case PROP_TREE_ITEMS:
                    if (ctrl->type == CTRL_TREEVIEW && ctrl->treeview.items && value) {
                        if (!sys_range_mapped(value, sizeof(sys_tree_items_t)))
                            break;
                        sys_tree_items_t batch = *(sys_tree_items_t*)value;
                        uint16_t count = batch.count;
                        if (count > ctrl->treeview.max_items)
                            count = ctrl->treeview.max_items;
                        if (count > 0 && (!batch.items ||
                            !sys_range_mapped((uint32_t)batch.items, sizeof(sys_tree_item_t) * count))) {
                            break;
                        }
                        if (batch.items && count > 0) {
                            for (uint16_t i = 0; i < count; i++)
                                ctrl->treeview.items[i] = batch.items[i];
                        }
                        ctrl->treeview.item_count = count;
                        if (ctrl->treeview.selected_index >= count)
                            ctrl->treeview.selected_index = count ? 0 : -1;
                        treeview_keep_selected_visible_sys(ctrl);
                    }
                    break;
                case PROP_TREE_SELECTED:
                    if (ctrl->type == CTRL_TREEVIEW) {
                        int selected = (int)value;
                        if (selected < -1) selected = -1;
                        if (selected >= ctrl->treeview.item_count)
                            selected = ctrl->treeview.item_count ? ctrl->treeview.item_count - 1 : -1;
                        ctrl->treeview.selected_index = selected;
                        treeview_keep_selected_visible_sys(ctrl);
                    }
                    break;
                case PROP_TREE_SCROLL:
                    if (ctrl->type == CTRL_TREEVIEW) {
                        int scroll = (int)value;
                        int max_scroll = treeview_max_scroll_sys(ctrl);
                        if (scroll < 0) scroll = 0;
                        if (scroll > max_scroll) scroll = max_scroll;
                        ctrl->treeview.scroll_offset = scroll;
                    }
                    break;
                case PROP_TREE_HSCROLL:
                    if (ctrl->type == CTRL_TREEVIEW) {
                        int scroll = (int)value;
                        int max_scroll = 0;
                        treeview_layout_sys(ctrl, NULL, NULL, NULL, NULL, NULL, NULL, &max_scroll);
                        if (scroll < 0) scroll = 0;
                        if (scroll > max_scroll) scroll = max_scroll;
                        ctrl->treeview.hscroll_offset = scroll;
                    }
                    break;
                case PROP_TREE_ACTION:
                    if (ctrl->type == CTRL_TREEVIEW) {
                        ctrl->treeview.last_action = (uint8_t)value;
                        if (value == TREE_ACTION_NONE)
                            ctrl->treeview.action_index = -1;
                    }
                    break;
                case PROP_TREE_ACTION_INDEX:
                    if (ctrl->type == CTRL_TREEVIEW)
                        ctrl->treeview.action_index = (int16_t)value;
                    break;
                case PROP_TREE_ICON_CLOSED:
                    if (ctrl->type == CTRL_TREEVIEW && value) {
                        char path[64];
                        if (sys_copy_string(path, value, sizeof(path)) != 0)
                            break;
                        if (ctrl->treeview.icon_closed) {
                            bitmap_free(ctrl->treeview.icon_closed);
                            ctrl->treeview.icon_closed = NULL;
                        }
                        strcpy_s(ctrl->treeview.icon_closed_path, path, sizeof(ctrl->treeview.icon_closed_path));
                        ctrl->treeview.icon_closed_failed = 0;
                    }
                    break;
                case PROP_TREE_ICON_OPEN:
                    if (ctrl->type == CTRL_TREEVIEW && value) {
                        char path[64];
                        if (sys_copy_string(path, value, sizeof(path)) != 0)
                            break;
                        if (ctrl->treeview.icon_open) {
                            bitmap_free(ctrl->treeview.icon_open);
                            ctrl->treeview.icon_open = NULL;
                        }
                        strcpy_s(ctrl->treeview.icon_open_path, path, sizeof(ctrl->treeview.icon_open_path));
                        ctrl->treeview.icon_open_failed = 0;
                    }
                    break;
                case PROP_LIST_ITEMS:
                    if (ctrl->type == CTRL_LISTBOX && ctrl->listbox.items && value) {
                        if (!sys_range_mapped(value, sizeof(sys_list_items_t)))
                            break;
                        sys_list_items_t batch = *(sys_list_items_t*)value;
                        uint16_t count = batch.count;
                        if (count > ctrl->listbox.max_items)
                            count = ctrl->listbox.max_items;
                        if (count > 0 && (!batch.items ||
                            !sys_range_mapped((uint32_t)batch.items, sizeof(sys_list_item_t) * count))) {
                            break;
                        }
                        if (batch.items && count > 0) {
                            for (uint16_t i = 0; i < count; i++)
                                ctrl->listbox.items[i] = batch.items[i];
                        }
                        ctrl->listbox.item_count = count;
                        if (ctrl->listbox.selected_index >= count)
                            ctrl->listbox.selected_index = count ? 0 : -1;
                        int max_scroll = listbox_max_scroll_sys(ctrl);
                        if (ctrl->listbox.scroll_offset > max_scroll)
                            ctrl->listbox.scroll_offset = max_scroll;
                    }
                    break;
                case PROP_LIST_SELECTED:
                    if (ctrl->type == CTRL_LISTBOX) {
                        int selected = (int)value;
                        if (selected < -1) selected = -1;
                        if (selected >= ctrl->listbox.item_count)
                            selected = ctrl->listbox.item_count ? ctrl->listbox.item_count - 1 : -1;
                        ctrl->listbox.selected_index = selected;
                    }
                    break;
                case PROP_LIST_SCROLL:
                    if (ctrl->type == CTRL_LISTBOX) {
                        int scroll = (int)value;
                        int max_scroll = listbox_max_scroll_sys(ctrl);
                        if (scroll < 0) scroll = 0;
                        if (scroll > max_scroll) scroll = max_scroll;
                        ctrl->listbox.scroll_offset = scroll;
                    }
                    break;
                case PROP_LIST_ACTION:
                    if (ctrl->type == CTRL_LISTBOX) {
                        ctrl->listbox.last_action = (uint8_t)value;
                        if (value == LIST_ACTION_NONE)
                            ctrl->listbox.action_index = -1;
                    }
                    break;
                case PROP_LIST_ACTION_INDEX:
                    if (ctrl->type == CTRL_LISTBOX)
                        ctrl->listbox.action_index = (int16_t)value;
                    break;
                case 1: /* PROP_CHECKED */
                    switch (ctrl->type) {
                        case CTRL_CHECKBOX:
                            ctrl->checkbox.checked = (uint8_t)value;
                            break;
                        case CTRL_RADIOBUTTON:
                            ctrl->radiobutton.checked = (uint8_t)value;
                            break;
                        case CTRL_ICON:
                            ctrl->icon.checked = (uint8_t)value;
                            break;
                        default:
                            break;
                    }
                    break;
                case 2: /* PROP_X - universal */
                    ctrl->x = (uint16_t)value;
                    break;
                case 3: /* PROP_Y - universal */
                    ctrl->y = (uint16_t)value;
                    break;
                case 4: /* PROP_W - universal */
                    ctrl->w = (uint16_t)value;
                    break;
                case 5: /* PROP_H - universal */
                    ctrl->h = (uint16_t)value;
                    break;
                case 6: /* PROP_VISIBLE - universal */
                    if (value)
                        ctrl->type &= 0x7F;
                    else
                        ctrl->type |= 0x80;
                    break;
                case 7: /* PROP_FG - universal */
                    ctrl->fg = (uint8_t)value;
                    break;
                case 8: /* PROP_BG - universal */
                    ctrl->bg = (uint8_t)value;
                    break;
                case 9: /* PROP_IMAGE */
                    if (value) {
                        const char *path = (const char*)value;

                        if (ctrl->type == CTRL_BUTTON) {
                            if (ctrl->button.cached_bitmap_orig) {
                                bitmap_free(ctrl->button.cached_bitmap_orig);
                                ctrl->button.cached_bitmap_orig = NULL;
                            }
                            ctrl->button.cached_bitmap_orig = bitmap_load_from_file(path);
                        } else if (ctrl->type == CTRL_ICON) {
                            if (ctrl->icon.cached_bitmap_orig) {
                                bitmap_free(ctrl->icon.cached_bitmap_orig);
                                ctrl->icon.cached_bitmap_orig = NULL;
                            }
                            ctrl->icon.cached_bitmap_orig = bitmap_load_from_file(path);
                        } else if (ctrl->type == CTRL_PICTUREBOX) {
                            gui_picturebox_free_data(ctrl);
                            ctrl->text[0] = '\0';
                            strcpy_s(ctrl->text, path, sizeof(ctrl->text));
                        }
                    }
                    break;
                case PROP_BUFFER:
                    if (ctrl->type == CTRL_PICTUREBOX && value) {
                        if (!sys_range_mapped(value, sizeof(sys_picturebox_buffer_t)))
                            break;

                        sys_picturebox_buffer_t src;
                        memcpy_s(&src, (const void*)value, sizeof(src));
                        if (!src.pixels || src.w == 0 || src.h == 0)
                            break;
                        if (src.w > WM_SCREEN_WIDTH || src.h > WM_SCREEN_HEIGHT)
                            break;

                        uint32_t bytes = (uint32_t)src.w * (uint32_t)src.h;
                        if (bytes == 0 || bytes > (uint32_t)WM_SCREEN_WIDTH * (uint32_t)WM_SCREEN_HEIGHT)
                            break;
                        if (!sys_range_mapped((uint32_t)src.pixels, bytes))
                            break;

                        if (ctrl->picturebox.buffer &&
                            ctrl->picturebox.buffer_w == src.w &&
                            ctrl->picturebox.buffer_h == src.h) {
                            memcpy_s(ctrl->picturebox.buffer, src.pixels, bytes);
                            ctrl->text[0] = '\0';
                            break;
                        }

                        uint8_t *copy = (uint8_t*)kmalloc(bytes);
                        if (!copy)
                            break;

                        memcpy_s(copy, src.pixels, bytes);
                        gui_picturebox_free_data(ctrl);
                        ctrl->picturebox.buffer = copy;
                        ctrl->picturebox.buffer_w = src.w;
                        ctrl->picturebox.buffer_h = src.h;
                        ctrl->text[0] = '\0';
                    }
                    break;
                case 10: /* PROP_ENABLED - picturebox image_mode */
                    if (ctrl->type == CTRL_PICTUREBOX) {
                        ctrl->picturebox.image_mode = (uint8_t)value;
                    } else {
                        // For now - nothing. TODO: disabled controls
                    }
                    break;
                default:
                    return 0;
            }
            return 1;
        }

        case 0x0F: { /* SYS_WIN_CTRL_GET_PROP */
            gui_form_t *form = (gui_form_t*)ebx;
            int16_t ctrl_id = (int16_t)(ecx >> 16);
            int16_t prop_id = (int16_t)(ecx & 0xFFFF);

            if (!form) return 0;

            /* Find control */
            gui_control_t *ctrl = NULL;
            for (int i = 0; i < form->ctrl_count; i++) {
                if (form->controls[i].id == ctrl_id) {
                    ctrl = &form->controls[i];
                    break;
                }
            }
            if (!ctrl) return 0;

            switch (prop_id) {
                case 0: /* PROP_TEXT */
                    return (uint32_t)ctrl->text;
                case 1: /* PROP_CHECKED */
                    if (ctrl->type == CTRL_CHECKBOX) {
                        return ctrl->checkbox.checked;
                    } else if (ctrl->type == CTRL_RADIOBUTTON) {
                        return ctrl->radiobutton.checked;
                    } else if (ctrl->type == CTRL_ICON) {
                        return ctrl->icon.checked;
                    }
                    return 0;
                case 2: /* PROP_X */
                    return ctrl->x;
                case 3: /* PROP_Y */
                    return ctrl->y;
                case 4: /* PROP_W */
                    return ctrl->w;
                case 5: /* PROP_H */
                    return ctrl->h;
                case 6: /* PROP_VISIBLE */
                    return (ctrl->type & 0x80) ? 0 : 1;
                case 7: /* PROP_FG */
                    return ctrl->fg;
                case 8: /* PROP_BG */
                    return ctrl->bg;
                case 9: /* PROP_IMAGE */
                    if (ctrl->type == CTRL_BUTTON) {
                        return (uint32_t)ctrl->button.cached_bitmap_orig;
                    } else if (ctrl->type == CTRL_ICON) {
                        return (uint32_t)ctrl->icon.cached_bitmap_orig;
                    } else if (ctrl->type == CTRL_PICTUREBOX) {
                        return (uint32_t)ctrl->text;
                    }
                    return 0;
                case 10: /* PROP_ENABLED - picturebox image_mode */
                    if (ctrl->type == CTRL_PICTUREBOX) {
                        return ctrl->picturebox.image_mode;
                    }
                    return 0;
                case PROP_BUFFER:
                    if (ctrl->type == CTRL_PICTUREBOX) {
                        return (uint32_t)ctrl->picturebox.buffer;
                    }
                    return 0;
                case PROP_TREE_ITEMS:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return (uint32_t)ctrl->treeview.items;
                    return 0;
                case PROP_TREE_SELECTED:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return (uint32_t)ctrl->treeview.selected_index;
                    return 0;
                case PROP_TREE_SCROLL:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return ctrl->treeview.scroll_offset;
                    return 0;
                case PROP_TREE_HSCROLL:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return ctrl->treeview.hscroll_offset;
                    return 0;
                case PROP_TREE_ACTION:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return ctrl->treeview.last_action;
                    return 0;
                case PROP_TREE_ACTION_INDEX:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return (uint32_t)ctrl->treeview.action_index;
                    return 0;
                case PROP_TREE_ICON_CLOSED:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return (uint32_t)ctrl->treeview.icon_closed_path;
                    return 0;
                case PROP_TREE_ICON_OPEN:
                    if (ctrl->type == CTRL_TREEVIEW)
                        return (uint32_t)ctrl->treeview.icon_open_path;
                    return 0;
                case PROP_LIST_ITEMS:
                    if (ctrl->type == CTRL_LISTBOX)
                        return (uint32_t)ctrl->listbox.items;
                    return 0;
                case PROP_LIST_SELECTED:
                    if (ctrl->type == CTRL_LISTBOX)
                        return (uint32_t)ctrl->listbox.selected_index;
                    return 0;
                case PROP_LIST_SCROLL:
                    if (ctrl->type == CTRL_LISTBOX)
                        return ctrl->listbox.scroll_offset;
                    return 0;
                case PROP_LIST_ACTION:
                    if (ctrl->type == CTRL_LISTBOX)
                        return ctrl->listbox.last_action;
                    return 0;
                case PROP_LIST_ACTION_INDEX:
                    if (ctrl->type == CTRL_LISTBOX)
                        return (uint32_t)ctrl->listbox.action_index;
                    return 0;
                default:
                    return 0;
            }
        }

        case 0x10: { /* SYS_WIN_INVALIDATE_ICONS */
            compositor_invalidate_icon_backgrounds(&global_wm);
            return 0;
        }

        case 0x11: { /* SYS_WIN_CHECK_REDRAW - Check for full or partial redraw */
            /* Return codes: 0 = none, 1 = full redraw, 2 = partial redraw (dirty rect available) */
            if (global_wm.needs_full_redraw) {
                global_wm.needs_full_redraw = 0;
                global_wm.dirty_x = 0;
                global_wm.dirty_y = 0;
                global_wm.dirty_w = 0;
                global_wm.dirty_h = 0;
                return 1; /* full redraw */
            }

            /* If compositor has a pending dirty rectangle, signal a partial redraw */
            if (global_wm.dirty_w > 0 && global_wm.dirty_h > 0) {
                return 2; /* partial redraw */
            }

            return 0;
        }

        case 0x12: { /* SYS_WIN_GET_DIRTY_RECT - Get dirty rectangle */
            if (!sys_range_mapped(ebx, sizeof(int) * 4)) return (uint32_t)-1;
            int *out = (int*)ebx;
            out[0] = global_wm.dirty_x;
            out[1] = global_wm.dirty_y;
            out[2] = global_wm.dirty_w;
            out[3] = global_wm.dirty_h;
            return 0;
        }

        case 0x13: { /* SYS_WIN_GET_THEME - Get current theme pointer */
            return (uint32_t)theme_get_current();
        }

        case 0x14: { /* SYS_WIN_CYCLE_PREVIEW - Cycle focus to next window (preview only) */
            if (global_wm.count <= 1) return 0;
            int start = global_wm.focused_index;
            int i = start;
            for (int k = 0; k < global_wm.count; k++) {
                i = (i + 1) % global_wm.count;
                gui_form_t *f = global_wm.windows[i];
                if (f && !f->win.is_minimized && f->win.is_visible) {
                    /* Set focused index but DO NOT change z-order - preview behavior */
                    global_wm.focused_index = i;
                    gui_request_full_redraw();
                    return 1;
                }
            }
            return 0;
        }

        case 0x15: { /* SYS_WIN_CYCLE_COMMIT - Bring the currently focused window to front */
            if (global_wm.focused_index < 0 || global_wm.focused_index >= global_wm.count) return 0;
            gui_form_t *f = global_wm.windows[global_wm.focused_index];
            if (!f || f->win.is_minimized || !f->win.is_visible) return 0;
            wm_bring_to_front(&global_wm, f);
            gui_request_full_redraw();
            return 1;
        }

        case 0x16: { /* SYS_WIN_RESTORE_FORM - Restore given form (if minimized) and bring to front */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;

            if (form->win.is_minimized) {
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
            }

            wm_bring_to_front(&global_wm, form);
            /* Safety: ensure focused_index points to restored window */
            if (global_wm.count > 0) global_wm.focused_index = global_wm.count - 1;
            gui_request_full_redraw();
            return 2; /* Indicate full redraw */
        }

        case 0x17: { /* SYS_WIN_FORCE_FULL_REDRAW - request a desktop full redraw */
            gui_request_full_redraw();
            return 1;
        }

        case 0x18: { /* SYS_WIN_IS_FOCUSED - return 1 if given form is focused/topmost */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            if (global_wm.focused_index >= 0 && global_wm.focused_index < global_wm.count &&
                global_wm.windows[global_wm.focused_index] == form) {
                return 1;
            }
            return 0;
        }

        case 0x19: { /* SYS_WIN_DRAW_BUFFER - draw a user buffer into a form but clipped to regions where the form is topmost */
            struct {
                gui_form_t *form;
                const uint8_t *buffer;
                int buf_w;
                int buf_h;
                int src_x;
                int src_y;
                int src_w;
                int src_h;
                int dest_x;
                int dest_y;
                int transparent;
            } *p = (void*)ebx;

            if (!p || !p->form || !p->buffer) return -1;

            /* Clip source rectangle to buffer bounds */
            int sx = p->src_x < 0 ? 0 : p->src_x;
            int sy = p->src_y < 0 ? 0 : p->src_y;
            int sw = p->src_w;
            int sh = p->src_h;
            if (sx + sw > p->buf_w) sw = p->buf_w - sx;
            if (sy + sh > p->buf_h) sh = p->buf_h - sy;
            if (sw <= 0 || sh <= 0) return -1;

            /* Destination is relative to the window client area (titlebar offset = 20) */
            int dest_base_x = p->form->win.x + p->dest_x;
            int dest_base_y = p->form->win.y + 20 + p->dest_y;

            for (int y = 0; y < sh; y++) {
                int screen_y = dest_base_y + y;
                if (screen_y < 0 || screen_y >= WM_SCREEN_HEIGHT) continue;
                for (int x = 0; x < sw; x++) {
                    int screen_x = dest_base_x + x;
                    if (screen_x < 0 || screen_x >= WM_SCREEN_WIDTH) continue;

                    /* Only draw if this form is topmost at this pixel */
                    gui_form_t *top = wm_get_window_at(&global_wm, screen_x, screen_y);
                    if (top != p->form) continue;

                    int px = p->buffer[(sy + y) * p->buf_w + (sx + x)];
                    if (p->transparent && px == 5) continue; /* color 5 as transparent for compatibility */
                    gfx_putpixel(screen_x, screen_y, (uint8_t)px);
                }
            }

            return 0;
        }

        case 0x1A: { /* SYS_WIN_MARK_DIRTY - mark window region as dirty and trigger compositor redraw with z-order */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form || !form->win.is_visible || form->win.is_minimized) return 0;

            int wx = form->win.x - WM_BG_MARGIN;
            int wy = form->win.y - WM_BG_MARGIN;
            int ww = form->win.w + (WM_BG_MARGIN * 2);
            int wh = form->win.h + (WM_BG_MARGIN * 2);

            /* Set dirty rect and trigger compositor redraw */
            compositor_set_dirty_rect(&global_wm, wx, wy, ww, wh);
            compositor_draw_all(&global_wm);

            return 0;
        }

        case 0x1B: { /* SYS_WIN_MARK_DIRTY_RECT - mark arbitrary screen rect as dirty and redraw overlapping windows */
            struct { int x; int y; int w; int h; } *p = (void*)ebx;
            if (!p) return 0;
            /* Sanitize/clamp rect to screen */
            int rx = p->x < 0 ? 0 : p->x;
            int ry = p->y < 0 ? 0 : p->y;
            int rw = p->w;
            int rh = p->h;
            if (rw <= 0 || rh <= 0) return 0;
            if (rx + rw > WM_SCREEN_WIDTH) rw = WM_SCREEN_WIDTH - rx;
            if (ry + rh > WM_SCREEN_HEIGHT) rh = WM_SCREEN_HEIGHT - ry;
            compositor_set_dirty_rect(&global_wm, rx, ry, rw, rh);
            compositor_draw_all(&global_wm);
            return 0;
        }

        case 0x1C: { /* SYS_WIN_MENUBAR_ENABLE */
            gui_form_t *form = (gui_form_t*)ebx;
            if (!form) return 0;
            menubar_init(&form->menubar);
            form->menubar_enabled = 1;
            return 0;
        }

        case 0x1D: { /* SYS_WIN_MENUBAR_ADD_MENU */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *title = (const char*)ecx;
            if (!form || !title) return (uint32_t)-1;
            if (!form->menubar_enabled) return (uint32_t)-1;
            return (uint32_t)menubar_add_menu(&form->menubar, title);
        }

        case 0x1E: { /* SYS_WIN_MENUBAR_ADD_ITEM */
            gui_form_t *form = (gui_form_t*)ebx;
            const char *text = (const char*)ecx;
            uint32_t packed = edx;
            int menu_index = (int)(packed >> 16);
            int action_id = (int)(packed & 0xFFFF);
            if (!form || !text) return (uint32_t)-1;
            if (!form->menubar_enabled) return (uint32_t)-1;
            menu_t *menu = menubar_get_menu(&form->menubar, menu_index);
            if (!menu) return (uint32_t)-1;
            return (uint32_t)menu_add_item(menu, text, action_id, MENU_ITEM_ENABLED);
        }

        case 0x1F: { /* SYS_WIN_SET_RESIZABLE */
            gui_form_t *form = (gui_form_t*)ebx;
            int resizable = (int)ecx;
            if (!form) return 0;
            form->win.resizable = resizable ? 1 : 0;
            return 0;
        }

        case 0x20: { /* SYS_WIN_DRAW_TASKBAR_BUTTON */
            struct { int x; int y; int w; int h; const char *icon; const char *label; int pressed; uint8_t color; } *params;
            params = (void*)ebx;
            if (!params) return 0;

            /* Create a temporary control to render with unified button logic */
            gui_control_t temp_ctrl = {0};
            temp_ctrl.type = CTRL_BUTTON;
            temp_ctrl.w = params->w;
            temp_ctrl.h = params->h;
            temp_ctrl.button.pressed = params->pressed ? 1 : 0;
            temp_ctrl.bg = params->color;
            temp_ctrl.fg = COLOR_BLACK;

            /* Set label if provided */
            if (params->label && params->label[0]) {
                int len = 0;
                while (params->label[len] && len < 255) {
                    temp_ctrl.text[len] = params->label[len];
                    len++;
                }
                temp_ctrl.text[len] = '\0';
            }

            /* Load icon if provided */
            if (params->icon && params->icon[0]) {
                temp_ctrl.button.cached_bitmap_orig = bitmap_load_from_file(params->icon);
            }

            /* Draw using unified control rendering */
            ctrl_draw_button(&temp_ctrl, params->x, params->y);

            /* Clean up loaded bitmap */
            if (temp_ctrl.button.cached_bitmap_orig) {
                bitmap_free(temp_ctrl.button.cached_bitmap_orig);
            }

            return 0;
        }

        case 0x21: { /* SYS_WIN_GET_TOPMOST_AT - return the topmost window at position (x, y) */
            int x = ebx;
            int y = ecx;
            gui_form_t *topmost = wm_get_window_at(&global_wm, x, y);
            return (uint32_t)topmost;
        }

        case 0x22: { /* SYS_WIN_MENUBAR_ADD_SEPARATOR */
            gui_form_t *form = (gui_form_t*)ebx;
            int menu_index = (int)ecx;
            if (!form || !form->menubar_enabled) return (uint32_t)-1;
            menu_t *menu = menubar_get_menu(&form->menubar, menu_index);
            if (!menu) return (uint32_t)-1;
            return (uint32_t)menu_add_separator(menu);
        }

        default:
            return (uint32_t)-1;
    }
}

/* Cleanup all windows owned by a task (called on task exit) */
void wm_cleanup_task(uint32_t tid) {
    if (!wm_initialized) return;

    /* Iterate through all windows and destroy those owned by this task */
    int cleaned = 0;
    for (int i = global_wm.count - 1; i >= 0; i--) {
        gui_form_t *form = global_wm.windows[i];
        if (!form || form->owner_tid != tid) continue;

        /* Release icon slot if window was minimized */
        if (form->win.is_minimized) {
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
        }

        /* Unregister from window manager */
        wm_unregister_window(&global_wm, form);

        /* Free cached bitmaps in controls */
        if (form->controls) {
            for (int j = 0; j < form->ctrl_count; j++) {
                gui_control_t *ctrl = &form->controls[j];
                /* Free bitmaps based on control type */
                if (ctrl->type == CTRL_BUTTON && ctrl->button.cached_bitmap_orig) {
                    bitmap_free(ctrl->button.cached_bitmap_orig);
                    ctrl->button.cached_bitmap_orig = NULL;
                }
                if (ctrl->type == CTRL_PICTUREBOX) {
                    gui_picturebox_free_data(ctrl);
                }
                if (ctrl->type == CTRL_ICON && ctrl->icon.cached_bitmap_orig) {
                    bitmap_free(ctrl->icon.cached_bitmap_orig);
                    ctrl->icon.cached_bitmap_orig = NULL;
                }
            }
            kfree(form->controls);
        }

        /* Destroy window menu */
        if (form->window_menu_initialized) {
            menu_destroy(&form->window_menu);
        }

        /* Destroy window (restores background) */
        win_destroy(&form->win);

        /* Free form */
        kfree(form);
        cleaned = 1;
    }
    /* If we removed any windows, request a full desktop redraw so wallpaper and
       desktop elements behind the removed windows are repainted. */
    if (cleaned) {
        gui_request_full_redraw();
    }
}
