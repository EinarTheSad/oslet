#pragma once
#include "wm_config.h"
#include "../syscall.h"

// Free icon slot tracking
typedef struct {
    int x, y;
} icon_slot_t;

#define WM_MAX_FREE_SLOTS 32

// Window manager state
typedef struct {
    gui_form_t *windows[WM_MAX_WINDOWS];  // Array of window pointers
    int count;                             // Number of active windows
    int focused_index;                     // Index of focused window (-1 if none)
    int next_icon_y;                       // Y position for next minimized icon (vertical layout)
    int next_icon_column;                  // Current column for icon placement
    uint32_t last_icon_click_time;         // For double-click detection
    int last_icon_click_x;
    int last_icon_click_y;
    icon_slot_t free_slots[WM_MAX_FREE_SLOTS];  // Pool of free icon slots
    int free_slot_count;                         // Number of available free slots
    int needs_full_redraw;                       // Flag: desktop should do full redraw
    int dirty_x, dirty_y, dirty_w, dirty_h;      // Dirty rectangle for partial redraw
} window_manager_t;

void wm_init(window_manager_t *wm);
int wm_register_window(window_manager_t *wm, gui_form_t *form);
void wm_unregister_window(window_manager_t *wm, gui_form_t *form);
void wm_bring_to_front(window_manager_t *wm, gui_form_t *form);

// Find window at given screen coordinates
// Returns pointer to window or NULL if none found
gui_form_t* wm_get_window_at(window_manager_t *wm, int x, int y);

void wm_draw_all(window_manager_t *wm);
void wm_get_next_icon_pos(window_manager_t *wm, int *out_x, int *out_y);
void wm_release_icon_slot(window_manager_t *wm, int x, int y);

void wm_set_icon_click(window_manager_t *wm, uint32_t time, int x, int y);
int wm_is_icon_doubleclick(window_manager_t *wm, uint32_t time, int x, int y);
void wm_invalidate_icon_backgrounds(window_manager_t *wm);
