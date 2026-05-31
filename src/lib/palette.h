#pragma once
#include <stdint.h>

int palette_parse_pal(const char *path, uint8_t palette[16][3]);
int palette_save_pal(const char *path, const uint8_t palette[16][3]);
int palette_load_default_file(const char *path, uint8_t palette[16][3]);
void palette_set_default(uint8_t palette[16][3]);
