#ifndef CONTROLS_H
#define CONTROLS_H

#include "../syscall.h"
#include "window.h"

// Draw a button control
void ctrl_draw_button(gui_control_t *control, int abs_x, int abs_y);

// Draw a label control
void ctrl_draw_label(gui_control_t *control, int abs_x, int abs_y);

// Draw a picturebox control
void ctrl_draw_picturebox(gui_control_t *control, int abs_x, int abs_y);

// Generic control drawing dispatcher
void ctrl_draw(window_t *win, gui_control_t *control);

// Helper functions for text processing
int text_split_lines(const char *text, char lines[][256], int max_lines);
int text_measure_height(const char *text, void *font, int size);

#endif // CONTROLS_H
