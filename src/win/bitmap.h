#pragma once
#include <stdint.h>

typedef struct bitmap_s {
    uint8_t *data;
    int width;
    int height;
    int bits_per_pixel;
} bitmap_t;

bitmap_t* bitmap_load_from_file(const char *path);
bitmap_t* bitmap_scale_nearest(bitmap_t *src, int new_w, int new_h);
void bitmap_draw(bitmap_t *bmp, int x, int y);
void bitmap_draw_opaque(bitmap_t *bmp, int x, int y);
void bitmap_free(bitmap_t *bmp);