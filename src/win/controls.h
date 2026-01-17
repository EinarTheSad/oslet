#pragma once
#include "../syscall.h"
#include "window.h"
#include "../fonts/bmf.h"

void ctrl_draw_button(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_label(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_picturebox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_checkbox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_radiobutton(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_textbox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_frame(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw(window_t *win, gui_control_t *control);

int text_split_lines(const char *text, char lines[][256], int max_lines);
int text_measure_height(const char *text, void *font, int size);

/* Textbox helper - find char position from X coordinate */
int textbox_pos_from_x(bmf_font_t *font, int size, const char *text, int scroll_offset, int rel_x);
