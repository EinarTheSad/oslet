#ifndef THEME_H
#define THEME_H

#include <stdint.h>

typedef struct {
    uint8_t bg_color;           // Window background color
    uint8_t titlebar_color;     // Title bar background color
    uint8_t frame_dark;         // Dark frame edge color (3D effect)
    uint8_t frame_light;        // Light frame edge color (3D effect)
    uint8_t text_color;         // Default text color
    uint8_t button_color;       // Button background color
} window_theme_t;

// Initialize theming system with default theme
void theme_init(void);

// Get pointer to current active theme
window_theme_t* theme_get_current(void);

// Set a new theme (for future use)
void theme_set(window_theme_t *theme);

#endif // THEME_H
