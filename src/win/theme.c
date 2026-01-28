#include "theme.h"

static window_theme_t default_theme = {
    .bg_color = 15,
    .titlebar_color = 1,
    .titlebar_inactive = 8,
    .frame_dark = 8,
    .frame_light = 7,
    .text_color = 0,
    .icon_text_color = 0,
    .button_color = 7,
    .taskbar_color = 7,
    .start_button_color = 7,
    .desktop_color = 3
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
