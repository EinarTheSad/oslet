#include "wm.h"
#include "window.h"
#include "menu.h"
#include "../drivers/mouse.h"

void wm_init(window_manager_t *wm) {
    wm->count = 0;
    wm->focused_index = -1;
    wm->next_icon_row = 0;
    wm->next_icon_column = 0;
    wm->last_icon_click_time = 0;
    wm->last_icon_click_form = NULL;
    wm->free_slot_count = 0;

    for (int i = 0; i < WM_MAX_WINDOWS; i++) {
        wm->windows[i] = NULL;
    }
}

int wm_register_window(window_manager_t *wm, gui_form_t *form) {
    if (!form || wm->count >= WM_MAX_WINDOWS) {
        return 0;  // Failure
    }

    // Add window to array
    wm->windows[wm->count] = form;
    wm->count++;

    // Make new window focused
    wm->focused_index = wm->count - 1;

    return 1;  // Success
}

void wm_unregister_window(window_manager_t *wm, gui_form_t *form) {
    if (!form) return;

    // Find window in array
    int found_index = -1;
    for (int i = 0; i < wm->count; i++) {
        if (wm->windows[i] == form) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) return;  // Not found

    // Shift remaining windows down
    for (int i = found_index; i < wm->count - 1; i++) {
        wm->windows[i] = wm->windows[i + 1];
    }

    wm->windows[wm->count - 1] = NULL;
    wm->count--;

    // Adjust focused index
    if (wm->focused_index == found_index) {
        // Focus was on removed window - focus last window
        wm->focused_index = wm->count > 0 ? wm->count - 1 : -1;
    } else if (wm->focused_index > found_index) {
        // Adjust focused index down
        wm->focused_index--;
    }
}

int wm_bring_to_front(window_manager_t *wm, gui_form_t *form) {
    if (!form || wm->count <= 1) return 0;

    // Find window
    int found_index = -1;
    for (int i = 0; i < wm->count; i++) {
        if (wm->windows[i] == form) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return 0;  // Not found
    }

    if (found_index == wm->count - 1) {
        wm->focused_index = wm->count - 1;
        return 1;
    }

    // Move window to end (front in Z-order)
    gui_form_t *temp = wm->windows[found_index];
    for (int i = found_index; i < wm->count - 1; i++) {
        wm->windows[i] = wm->windows[i + 1];
    }
    wm->windows[wm->count - 1] = temp;
    wm->focused_index = wm->count - 1;
    return 1;  // Z-order changed
}

gui_form_t* wm_get_window_at(window_manager_t *wm, int x, int y) {
    // Search from front to back (reverse order)
    for (int i = wm->count - 1; i >= 0; i--) {
        gui_form_t *form = wm->windows[i];
        if (!form || !form->win.is_visible) continue;

        // Check if minimized - check icon area
        if (form->win.is_minimized) {
            if (win_is_icon_clicked(form, x, y)) {
                return form;
            }
        } else {
            int click_w = form->win.w;
            int click_h = form->win.h;
            int click_y_offset = 0;  /* Offset from win.y to start of click area (negative = above window) */
            
            // Extend click area to include any open dropdown lists
            if (form->controls) {
                for (int j = 0; j < form->ctrl_count; j++) {
                    if (form->controls[j].type == CTRL_DROPDOWN && 
                        form->controls[j].dropdown.dropdown_open) {
                        int ctrl_abs_y = form->win.y + 20 + form->controls[j].y;
                        int list_h = form->controls[j].dropdown.item_count * 16;
                        int list_y = ctrl_abs_y + form->controls[j].h;
                        
                        /* Auto-flip: if list extends past screen bottom, it's above */
                        if (list_y + list_h > WM_SCREEN_HEIGHT) {
                            /* List is above control - extend hit area upward */
                            int flipped_y = ctrl_abs_y - list_h;
                            if (flipped_y < 0) {
                                /* List starts at screen top (0) */
                                /* Click area extends from 0 to window bottom */
                                click_y_offset = -form->win.y;
                                click_h = form->win.y + form->win.h;
                            } else if (flipped_y < form->win.y) {
                                /* Click area extends from flipped_y to window bottom */
                                click_y_offset = -(form->win.y - flipped_y);
                                click_h = form->win.h + (form->win.y - flipped_y);
                            }
                        } else {
                            /* List is below control normally */
                            int dropdown_bottom = form->win.y + 20 + form->controls[j].y + form->controls[j].h + list_h;
                            if (dropdown_bottom > form->win.y + click_h) {
                                click_h = dropdown_bottom - form->win.y;
                            }
                        }
                    }
                }
            }
            
            // Check window area (including extended dropdown area)
            int click_top = form->win.y + click_y_offset;
            if (x >= form->win.x && x < form->win.x + click_w &&
                y >= click_top && y < form->win.y + click_h) {
                return form;
            }
        }
    }

    return NULL;  // No window at this position
}

void wm_get_next_icon_pos(window_manager_t *wm, int *out_x, int *out_y) {
    // Pop from beginning of free list (top-leftmost slot)
    if (wm->free_slot_count > 0) {
        *out_x = wm->free_slots[0].x;
        *out_y = wm->free_slots[0].y;
        // Shift remaining slots down
        for (int i = 0; i < wm->free_slot_count - 1; i++) {
            wm->free_slots[i] = wm->free_slots[i + 1];
        }
        wm->free_slot_count--;
        return;
    }

    // No free slots - allocate a new position (top-left first, go down then right)
    *out_x = WM_ICON_MARGIN + (wm->next_icon_column * WM_ICON_SLOT_WIDTH);
    *out_y = WM_ICON_MARGIN + (wm->next_icon_row * WM_ICON_SLOT_HEIGHT);

    // Advance to next position
    wm->next_icon_row++;
    if (wm->next_icon_row * WM_ICON_SLOT_HEIGHT + WM_ICON_TOTAL_HEIGHT > WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT - WM_ICON_MARGIN) {
        wm->next_icon_row = 0;
        wm->next_icon_column++;
    }
}

void wm_release_icon_slot(window_manager_t *wm, int x, int y) {
    // Insert in sorted order (top-to-bottom, left-to-right)
    if (wm->free_slot_count >= WM_MAX_FREE_SLOTS) return;

    int insert_pos = wm->free_slot_count;
    for (int i = 0; i < wm->free_slot_count; i++) {
        // Sort by x first, then y (left to right, top to bottom - column-wise)
        if (x < wm->free_slots[i].x || (x == wm->free_slots[i].x && y < wm->free_slots[i].y)) {
            insert_pos = i;
            break;
        }
    }
    // Shift slots up to make room
    for (int i = wm->free_slot_count; i > insert_pos; i--) {
        wm->free_slots[i] = wm->free_slots[i - 1];
    }
    wm->free_slots[insert_pos].x = x;
    wm->free_slots[insert_pos].y = y;
    wm->free_slot_count++;
}

void wm_claim_icon_slot(window_manager_t *wm, int x, int y) {
    // Remove slot from free list (it's now occupied)
    for (int i = 0; i < wm->free_slot_count; i++) {
        if (wm->free_slots[i].x == x && wm->free_slots[i].y == y) {
            for (int j = i; j < wm->free_slot_count - 1; j++) {
                wm->free_slots[j] = wm->free_slots[j + 1];
            }
            wm->free_slot_count--;
            return;
        }
    }
}

void wm_set_icon_click(window_manager_t *wm, uint32_t time, gui_form_t *form) {
    wm->last_icon_click_time = time;
    wm->last_icon_click_form = form;
}

int wm_is_icon_doubleclick(window_manager_t *wm, uint32_t time, gui_form_t *form) {
    if ((time - wm->last_icon_click_time) < WM_DOUBLECLICK_TICKS &&
        wm->last_icon_click_form == form) {
        return 1;
    }
    return 0;
}

void wm_snap_to_slot(int x, int y, int *out_x, int *out_y) {
    int taskbar_height = WM_TASKBAR_HEIGHT;

    /* Calculate nearest column and row */
    int col = (x - WM_ICON_MARGIN + WM_ICON_SLOT_WIDTH / 2) / WM_ICON_SLOT_WIDTH;
    int row = (y - WM_ICON_MARGIN + WM_ICON_SLOT_HEIGHT / 2) / WM_ICON_SLOT_HEIGHT;

    /* Clamp to valid range */
    if (col < 0) col = 0;
    if (row < 0) row = 0;

    /* Calculate snapped position */
    *out_x = WM_ICON_MARGIN + col * WM_ICON_SLOT_WIDTH;
    *out_y = WM_ICON_MARGIN + row * WM_ICON_SLOT_HEIGHT;

    /* Ensure icon stays within desktop bounds (above taskbar) */
    int max_x = WM_SCREEN_WIDTH - WM_ICON_TOTAL_WIDTH - WM_ICON_MARGIN;
    int max_y = WM_SCREEN_HEIGHT - taskbar_height - WM_ICON_TOTAL_HEIGHT - WM_ICON_MARGIN;

    if (*out_x > max_x) *out_x = max_x;
    if (*out_y > max_y) *out_y = max_y;
    if (*out_x < WM_ICON_MARGIN) *out_x = WM_ICON_MARGIN;
    if (*out_y < WM_ICON_MARGIN) *out_y = WM_ICON_MARGIN;
}
