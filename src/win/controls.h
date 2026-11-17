#pragma once
#include "../syscall.h"
#include "window.h"
#include "../fonts/bmf.h"

void ctrl_draw_button(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_label(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_label_3d(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_sunken_border(int x, int y, int w, int h);
void ctrl_draw_picturebox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_checkbox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_radiobutton(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_textbox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_frame(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_dropdown(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_dropdown_list(window_t *win, gui_control_t *control, int y_offset);
void ctrl_draw_clock(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_scrollbar(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_treeview(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_listbox(gui_control_t *control, int abs_x, int abs_y);
void ctrl_draw_icon(gui_control_t *control, int abs_x, int abs_y, uint8_t win_bg);
void ctrl_hide_dropdown_list(window_t *win, gui_control_t *control);
void ctrl_draw(window_t *win, gui_control_t *control);
void ctrl_draw_with_offset(window_t *win, gui_control_t *control, int y_offset);

int text_split_lines(const char *text, char lines[][256], int max_lines);
int text_measure_height(const char *text, void *font, int size);

int textbox_pos_from_x(bmf_font_t *font, int size, gui_control_t *ctrl, int scroll_offset, int rel_x);
uint32_t textbox_pos_from_xy(bmf_font_t *font, int size, gui_control_t *ctrl, int text_area_x, int text_area_w, int start_line, int rel_x, int rel_y);
int textbox_wrap_line_count(bmf_font_t *font, int size, gui_control_t *ctrl, int text_area_w);
int textbox_multiline_layout(bmf_font_t *font, int size, gui_control_t *ctrl,
                             int *content_width, int *visible_lines, int *total_lines);
void textbox_wrapped_cursor_position(bmf_font_t *font, int size, gui_control_t *ctrl, uint32_t pos, int text_area_w, int *out_line, int *out_x);
uint32_t textbox_text_len(gui_control_t *ctrl);
char textbox_char_at(gui_control_t *ctrl, uint32_t offset);
int textbox_copy_range(gui_control_t *ctrl, uint32_t offset, char *destination, uint32_t length);
int textbox_set_text(gui_control_t *ctrl, const char *text, uint32_t length);
