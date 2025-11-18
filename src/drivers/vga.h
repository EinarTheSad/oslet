#pragma once
#include <stdint.h>

void vga_save_state(void);
void vga_restore_state(void);
void vga_clear(void);
void vga_use_as_console(void);
void vga_set_color(uint8_t background, uint8_t foreground);
void vga_reset_palette(void);