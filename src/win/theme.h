#pragma once
#include <stdint.h>

typedef struct {
    uint8_t bg_color;
    uint8_t titlebar_color;
    uint8_t frame_dark;
    uint8_t frame_light;
    uint8_t text_color;
    uint8_t button_color;
} window_theme_t;

void theme_init(void);
window_theme_t* theme_get_current(void);
void theme_set(window_theme_t *theme);
