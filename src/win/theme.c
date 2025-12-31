#include "theme.h"

// Default theme (Windows 95-style)
static window_theme_t default_theme = {
    .bg_color = 15,         // White background
    .titlebar_color = 1,    // Blue title bar
    .frame_dark = 8,        // Dark gray frame
    .frame_light = 7,       // Light gray frame
    .text_color = 0,        // Black text
    .button_color = 7       // Gray button
};

static window_theme_t *current_theme = &default_theme;

void theme_init(void) {
    current_theme = &default_theme;
}

window_theme_t* theme_get_current(void) {
    return current_theme;
}

void theme_set(window_theme_t *theme) {
    if (theme) {
        current_theme = theme;
    }
}
