#pragma once

#include "../controls.h"
#include "../theme.h"
#include "../bitmap.h"
#include "../window.h"
#include "../icon.h"
#include "../../drivers/graphics.h"
#include "../../drivers/mouse.h"
#include "../../fonts/bmf.h"
#include "../../mem/heap.h"
#include "../../rtc.h"

extern bmf_font_t font_b, font_n, font_i, font_bi;

static inline uint8_t ctrl_text_color(const gui_control_t *control) {
    return control->fg < 0 ? theme_get_current()->text_color : (uint8_t)control->fg;
}

static inline uint8_t ctrl_inside_color(const gui_control_t *control) {
    (void)control;
    return theme_get_current()->control_color;
}

#define DROPDOWN_HOVER_SCROLLBAR (-2)

void treeview_draw_text_clipped(int x, int y, bmf_font_t *font, int size,
                                const char *text, uint8_t color,
                                int min_x, int max_x);
