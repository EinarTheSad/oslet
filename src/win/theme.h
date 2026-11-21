#pragma once
#include <stdint.h>

typedef struct {
    uint8_t bg_color;
    uint8_t titlebar_color;
    uint8_t titlebar_inactive;
    uint8_t frame_dark;
    uint8_t frame_light;
    uint8_t text_color;
    uint8_t icon_text_color;
    uint8_t desktop_icon_text_color;
    uint8_t button_color;
    uint8_t taskbar_color;
    uint8_t start_button_color;
    uint8_t desktop_color;
    uint8_t control_color;
    uint8_t frame_shine;
    uint8_t highlight_color;
    uint8_t hover_color;
} window_theme_t;

void theme_init(void);
window_theme_t* theme_get_current(void);
void theme_set(window_theme_t *theme);
uint8_t theme_contrast_text(uint8_t background);
