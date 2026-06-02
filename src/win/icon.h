#pragma once
#include <stdint.h>

int icon_count_label_lines(const char *label, int max_line_width);
int icon_count_label_lines_limited(const char *label, int max_line_width,
                                   int max_lines);
int icon_calc_total_height(int icon_size, int label_lines);
void icon_draw_label_wrapped(const char *label, int x, int y, int total_width,
                             int max_line_width, uint8_t color);
void icon_draw_label_wrapped_limit(const char *label, int x, int y,
                                   int total_width, int max_line_width,
                                   uint8_t color, int max_lines);
