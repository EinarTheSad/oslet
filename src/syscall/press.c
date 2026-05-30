#include "gui.h"

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
            gui_rect_t list_rect;
            gui_dropdown_list_rect(form, ctrl, ctrl_y_offset, &list_rect);

            if (mx >= list_rect.x && mx < list_rect.x + list_rect.w &&
                my >= list_rect.y && my < list_rect.y + list_rect.h) {
                int item_h = 16;
                int visible_count = list_rect.h / item_h;
                if (visible_count < 1) visible_count = 1;
                int max_scroll = ctrl->dropdown.item_count > visible_count ? (ctrl->dropdown.item_count - visible_count) : 0;
                int need_scrollbar = ctrl->dropdown.item_count > visible_count;
                int sb_w = need_scrollbar ? 18 : 0;
                int content_w = ctrl->w - sb_w;

                if (need_scrollbar && mx >= list_rect.x + content_w && mx < list_rect.x + list_rect.w) {
                    int arrow_size = sb_w;
                    int track_len = list_rect.h - 2 * arrow_size;
                    int thumb_size = 20;
                    if (thumb_size > track_len) thumb_size = track_len;
                    int thumb_pos = 0;
                    if (max_scroll > 0 && track_len > thumb_size)
                        thumb_pos = ((track_len - thumb_size) * ctrl->dropdown.dropdown_scroll) / max_scroll;

                    int rel_y = my - list_rect.y;

                    if (rel_y < arrow_size) {
                        if (ctrl->dropdown.dropdown_scroll > 0) {
                            ctrl->dropdown.dropdown_scroll--;
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        form->press_control_id = ctrl->id;
                        gui_request_full_redraw();
                        return 1;
                    }

                    if (rel_y >= list_rect.h - arrow_size) {
                        if (ctrl->dropdown.dropdown_scroll < max_scroll) {
                            ctrl->dropdown.dropdown_scroll++;
                            if (ctrl->dropdown.dropdown_scroll > max_scroll) ctrl->dropdown.dropdown_scroll = max_scroll;
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        form->press_control_id = ctrl->id;
                        gui_request_full_redraw();
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

                    if (rel_y >= arrow_size && rel_y < list_rect.h - arrow_size) {
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
                            gui_request_full_redraw();
                        }
                        ctrl->dropdown.pressed = 1;
                        ctrl->dropdown.hovered_item = DROPDOWN_HOVER_SCROLLBAR;
                        ctrl->dropdown.scroll_offset = thumb_size / 2;
                        form->press_control_id = ctrl->id;
                        return 1;
                    }
                }

                int rel_item = (my - list_rect.y) / item_h;
                int clicked_item = ctrl->dropdown.dropdown_scroll + rel_item;
                if (clicked_item >= 0 && clicked_item < ctrl->dropdown.item_count) {
                    ctrl->dropdown.cursor_pos = clicked_item;
                    form->clicked_id = ctrl->id;
                }
                ctrl_hide_dropdown_list(&form->win, ctrl);
                form->press_control_id = -1;
                gui_request_full_redraw();
                return 1;
            }
            else if (mx >= abs_x && mx < abs_x + ctrl->w &&
                     my >= abs_y && my < abs_y + ctrl->h) {
                ctrl_hide_dropdown_list(&form->win, ctrl);
                form->press_control_id = ctrl->id;
                gui_request_full_redraw();
                return 1;
            }
            else {
                ctrl_hide_dropdown_list(&form->win, ctrl);
                gui_request_full_redraw();
                return 1;
            }
        }

        for (int i = form->ctrl_count - 1; i >= 0; i--) {
            gui_control_t *ctrl = &form->controls[i];

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
                    return 1;
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
                    int max_val = ctrl->scrollbar.max_length > 0 ? ctrl->scrollbar.max_length : 100;
                    int length = vertical ? ctrl->h : ctrl->w;
                    scrollbar_geom_t sb;
                    gui_scrollbar_make(&sb, length, arrow_size,
                                        ctrl->scrollbar.cursor_pos, max_val);

                    if (vertical) {
                        int up_y = abs_y;
                        int down_y = abs_y + ctrl->h - arrow_size;
                        int track_y = abs_y + arrow_size;
                        int thumb_y = track_y + sb.thumb_pos;

                        if (my >= up_y && my < up_y + arrow_size) {
                            ctrl->scrollbar.hovered_item = 0;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos > 0) ctrl->scrollbar.cursor_pos--;
                            return 1;
                        } else if (my >= down_y && my < down_y + arrow_size) {
                            ctrl->scrollbar.hovered_item = 2;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos < max_val) ctrl->scrollbar.cursor_pos++;
                            return 1;
                        } else if (my >= thumb_y && my < thumb_y + sb.thumb_size) {
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = my - thumb_y;
                            return 1;
                        } else if (my >= track_y && my < track_y + sb.track_len) {
                            if (sb.travel <= 0)
                                return 1;
                            ctrl->scrollbar.cursor_pos =
                                gui_scrollbar_value_from_pos(&sb, my - track_y, sb.thumb_size / 2);
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = sb.thumb_size / 2;
                            return 1;
                        }
                    } else {
                        int left_x = abs_x;
                        int right_x = abs_x + ctrl->w - arrow_size;
                        int track_x = abs_x + arrow_size;
                        int thumb_x = track_x + sb.thumb_pos;

                        if (mx >= left_x && mx < left_x + arrow_size) {
                            ctrl->scrollbar.hovered_item = 0;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos > 0) ctrl->scrollbar.cursor_pos--;
                            return 1;
                        } else if (mx >= right_x && mx < right_x + arrow_size) {
                            ctrl->scrollbar.hovered_item = 2;
                            ctrl->scrollbar.pressed = 1;
                            if (ctrl->scrollbar.cursor_pos < max_val) ctrl->scrollbar.cursor_pos++;
                            return 1;
                        } else if (mx >= thumb_x && mx < thumb_x + sb.thumb_size) {
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = mx - thumb_x;
                            return 1;
                        } else if (mx >= track_x && mx < track_x + sb.track_len) {
                            if (sb.travel <= 0)
                                return 1;
                            ctrl->scrollbar.cursor_pos =
                                gui_scrollbar_value_from_pos(&sb, mx - track_x, sb.thumb_size / 2);
                            ctrl->scrollbar.hovered_item = 1;
                            ctrl->scrollbar.pressed = 1;
                            ctrl->scrollbar.scroll_offset = sb.thumb_size / 2;
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

    if (!clicked_on_focusable && old_focus != -1) {
        form->focused_control_id = -1;
        return 1;
    }

    if (form->press_control_id == -1 && form->controls) {
        int deselected = 0;
        for (int i = 0; i < form->ctrl_count; i++) {
            if (form->controls[i].type == CTRL_ICON && form->controls[i].icon.checked) {
                form->controls[i].icon.checked = 0;
                deselected = 1;
            }
        }
        if (deselected) {
            return 1;
        }
    }

    return 0;
}
