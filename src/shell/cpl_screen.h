#pragma once
#include <stdint.h>

int screen_save_display(uint32_t width, uint32_t height, uint32_t depth);
void screen_restore_display(void);
