#pragma once
#include <stdint.h>

#define MAX_DIRTY_REGIONS 16

typedef struct {
    int x, y, w, h;
} dirty_region_t;

typedef struct {
    uint8_t *full_screen_buffer;
    dirty_region_t dirty_regions[MAX_DIRTY_REGIONS];
    int dirty_count;
    int screen_width;
    int screen_height;
} compositor_t;

void compositor_init(compositor_t *comp, int width, int height);
void compositor_destroy(compositor_t *comp);
void compositor_mark_dirty(compositor_t *comp, int x, int y, int w, int h);
void compositor_save_region(compositor_t *comp, int x, int y, int w, int h, uint8_t *dest);
void compositor_restore_region(compositor_t *comp, const uint8_t *src, int x, int y, int w, int h);
void compositor_clear_dirty(compositor_t *comp);
