#include "wm.h"
#include "window.h"
#include "icon.h"
#include "menu.h"

void wm_init(window_manager_t *wm) {
    wm->count = 0;
    wm->focused_index = -1;
    wm->next_icon_y = WM_ICON_MARGIN;
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

void wm_bring_to_front(window_manager_t *wm, gui_form_t *form) {
    if (!form || wm->count <= 1) return;

    // Find window
    int found_index = -1;
    for (int i = 0; i < wm->count; i++) {
        if (wm->windows[i] == form) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1 || found_index == wm->count - 1) {
        return;  // Not found or already at front
    }

    // Move window to end (front in Z-order)
    gui_form_t *temp = wm->windows[found_index];
    for (int i = found_index; i < wm->count - 1; i++) {
        wm->windows[i] = wm->windows[i + 1];
    }
    wm->windows[wm->count - 1] = temp;
    wm->focused_index = wm->count - 1;
}

gui_form_t* wm_get_window_at(window_manager_t *wm, int x, int y) {
    // Search from front to back (reverse order)
    for (int i = wm->count - 1; i >= 0; i--) {
        gui_form_t *form = wm->windows[i];
        if (!form || !form->win.is_visible) continue;

        // Check if minimized - check icon area
        if (form->win.is_minimized) {
            if (win_is_icon_clicked(&form->win, x, y)) {
                return form;
            }
        } else {
            // Check window area
            if (x >= form->win.x && x < form->win.x + form->win.w &&
                y >= form->win.y && y < form->win.y + form->win.h) {
                return form;
            }
        }
    }

    return NULL;  // No window at this position
}

void wm_draw_all(window_manager_t *wm) {
    // Draw windows from back to front
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *form = wm->windows[i];
        if (!form || !form->win.is_visible) continue;

        int is_focused = (i == wm->focused_index);
        win_draw_focused(&form->win, is_focused);

        // Draw controls if not minimized
        if (!form->win.is_minimized && form->controls) {
            for (int j = 0; j < form->ctrl_count; j++) {
                /* Set focus state before drawing (used by textbox for cursor) */
                form->controls[j].is_focused =
                    (form->controls[j].id == form->focused_control_id) ? 1 : 0;
                win_draw_control(&form->win, &form->controls[j]);
            }
        }

        // Draw window menu if visible (must be on top of controls)
        if (!form->win.is_minimized && form->window_menu.visible) {
            menu_draw(&form->window_menu);
        }
    }
}

void wm_get_next_icon_pos(window_manager_t *wm, int *out_x, int *out_y) {
    // First check if there are any free slots to reuse
    if (wm->free_slot_count > 0) {
        wm->free_slot_count--;
        *out_x = wm->free_slots[wm->free_slot_count].x;
        *out_y = wm->free_slots[wm->free_slot_count].y;
        return;
    }

    // No free slots - allocate a new position
    // Calculate X based on column (vertical stacking from left side)
    *out_x = WM_ICON_MARGIN + (wm->next_icon_column * WM_ICON_SLOT_WIDTH);
    *out_y = wm->next_icon_y;

    // Advance position for next icon (vertical layout)
    wm->next_icon_y += WM_ICON_SLOT_HEIGHT;

    // Check if we need to wrap to next column (accounting for taskbar at bottom)
    // Leave space for taskbar plus margin
    if (wm->next_icon_y + WM_ICON_TOTAL_HEIGHT > WM_SCREEN_HEIGHT - 27 - WM_ICON_MARGIN) {
        wm->next_icon_y = WM_ICON_MARGIN;  // Reset to top
        wm->next_icon_column++;  // Move to next column
    }
}

void wm_release_icon_slot(window_manager_t *wm, int x, int y) {
    // Add slot to free list if there's room
    if (wm->free_slot_count < WM_MAX_FREE_SLOTS) {
        wm->free_slots[wm->free_slot_count].x = x;
        wm->free_slots[wm->free_slot_count].y = y;
        wm->free_slot_count++;
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

void wm_invalidate_icon_backgrounds(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *form = wm->windows[i];
        if (form && form->win.is_minimized && form->win.minimized_icon) {
            icon_invalidate_bg(form->win.minimized_icon);
        }
    }
}

void wm_snap_to_slot(int x, int y, int *out_x, int *out_y) {
    int taskbar_height = 27;

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
