#include "theme.h"
#include "../drivers/graphics.h"

static window_theme_t default_theme = {
    .bg_color = 15,
    .titlebar_color = 1,
    .titlebar_inactive = 8,
    .frame_dark = 8,
    .frame_light = 7,
    .text_color = 0,
    .icon_text_color = 0,
    .desktop_icon_text_color = 0,
    .button_color = 7,
    .taskbar_color = 7,
    .start_button_color = 7,
    .desktop_color = 3,
    .control_color = 15,
    .frame_shine = 15,
    .highlight_color = 1,
    .hover_color = 7
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

uint8_t theme_contrast_text(uint8_t background) {
    const uint8_t *bg = gfx_palette[background & 15];
    int lum = 299 * bg[0] + 587 * bg[1] + 114 * bg[2];
    const uint8_t *black = gfx_palette[COLOR_BLACK];
    const uint8_t *white = gfx_palette[COLOR_WHITE];
    int dark = 299 * black[0] + 587 * black[1] + 114 * black[2];
    int light = 299 * white[0] + 587 * white[1] + 114 * white[2];
    int dark_diff = lum > dark ? lum - dark : dark - lum;
    int light_diff = lum > light ? lum - light : light - lum;
    return light_diff > dark_diff ? COLOR_WHITE : COLOR_BLACK;
}
