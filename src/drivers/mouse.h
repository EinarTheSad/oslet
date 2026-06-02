#pragma once
#include <stdint.h>

void mouse_init(void);
void mouse_handler(void);
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);
void mouse_set_cursor_mode(int mode);
int mouse_set_cursor_file(const char *path);
void mouse_draw_cursor(int x, int y);
void mouse_save(int x, int y);
void mouse_restore(void);
void mouse_invalidate_buffer(void);
