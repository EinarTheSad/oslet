#ifndef WM_H
#define WM_H

#include "wm_config.h"
#include "../syscall.h"

// Window manager state
typedef struct {
    gui_form_t *windows[WM_MAX_WINDOWS];  // Array of window pointers
    int count;                             // Number of active windows
    int focused_index;                     // Index of focused window (-1 if none)
    int next_icon_x;                       // X position for next minimized icon
    uint32_t last_icon_click_time;         // For double-click detection
    int last_icon_click_x;
    int last_icon_click_y;
} window_manager_t;

// Initialize window manager
void wm_init(window_manager_t *wm);

// Register a new window with the manager
// Returns 1 on success, 0 on failure
int wm_register_window(window_manager_t *wm, gui_form_t *form);

// Unregister a window from the manager
void wm_unregister_window(window_manager_t *wm, gui_form_t *form);

// Bring window to front (change Z-order)
void wm_bring_to_front(window_manager_t *wm, gui_form_t *form);

// Find window at given screen coordinates
// Returns pointer to window or NULL if none found
gui_form_t* wm_get_window_at(window_manager_t *wm, int x, int y);

// Draw all windows in Z-order (back to front)
void wm_draw_all(window_manager_t *wm);

// Get next icon position for minimized window
void wm_get_next_icon_pos(window_manager_t *wm, int *out_x, int *out_y);

// Icon double-click state management
void wm_set_icon_click(window_manager_t *wm, uint32_t time, int x, int y);
int wm_is_icon_doubleclick(window_manager_t *wm, uint32_t time, int x, int y);

#endif // WM_H
