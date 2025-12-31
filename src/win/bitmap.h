#ifndef BITMAP_H
#define BITMAP_H

#include <stdint.h>

typedef struct bitmap_s {
    uint8_t *data;
    int width;
    int height;
    int bits_per_pixel;  // 4 for VGA mode 13h
} bitmap_t;

// Load bitmap from file
// Returns NULL on failure
bitmap_t* bitmap_load_from_file(const char *path);

// Draw bitmap to screen at specified position
void bitmap_draw(bitmap_t *bmp, int x, int y);

// Free bitmap memory
void bitmap_free(bitmap_t *bmp);

#endif // BITMAP_H
