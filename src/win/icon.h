#pragma once
#include <stdint.h>

struct gui_control_s;

typedef struct {
    int x;
    int y;
    int w;
    int h;
    int icon_x;
    int icon_y;
    int label_lines;
    int max_line_width;
    int bg_x;
    int bg_y;
    int bg_w;
    int bg_h;
} icon_geometry_t;

void icon_get_geometry(const struct gui_control_s *control, int abs_x,
                       int abs_y, icon_geometry_t *out);

int icon_count_label_lines(const char *label, int max_line_width);
int icon_count_label_lines_limited(const char *label, int max_line_width,
                                   int max_lines);
int icon_calc_total_height(int icon_size, int label_lines);
void icon_draw_label_wrapped(const char *label, int x, int y, int total_width,
                             int max_line_width, uint8_t color);
void icon_draw_label_wrapped_limit(const char *label, int x, int y,
                                   int total_width, int max_line_width,
                                   uint8_t color, int max_lines);
