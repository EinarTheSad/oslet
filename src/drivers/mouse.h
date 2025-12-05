#pragma once
#include <stdint.h>

void mouse_init(void);
void mouse_handler(void);
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);
void mouse_draw_cursor(int x, int y, uint8_t color);
void mouse_save(int x, int y);
void mouse_restore(void);