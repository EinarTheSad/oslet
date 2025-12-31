#include "compositor.h"
#include "wm_config.h"
#include "../mem/heap.h"
#include "../drivers/graphics.h"

void compositor_init(compositor_t *comp, int width, int height) {
    comp->screen_width = width;
    comp->screen_height = height;
    comp->dirty_count = 0;

    // Allocate full screen buffer
    comp->full_screen_buffer = kmalloc(width * height);
    if (!comp->full_screen_buffer) {
        // Handle allocation failure - compositor will work in degraded mode
        comp->full_screen_buffer = NULL;
    }
}

void compositor_destroy(compositor_t *comp) {
    if (comp->full_screen_buffer) {
        kfree(comp->full_screen_buffer);
        comp->full_screen_buffer = NULL;
    }
}

void compositor_mark_dirty(compositor_t *comp, int x, int y, int w, int h) {
    if (comp->dirty_count >= MAX_DIRTY_REGIONS) {
        // Too many dirty regions, just mark entire screen dirty
        comp->dirty_regions[0].x = 0;
        comp->dirty_regions[0].y = 0;
        comp->dirty_regions[0].w = comp->screen_width;
        comp->dirty_regions[0].h = comp->screen_height;
        comp->dirty_count = 1;
        return;
    }

    // Add new dirty region
    comp->dirty_regions[comp->dirty_count].x = x;
    comp->dirty_regions[comp->dirty_count].y = y;
    comp->dirty_regions[comp->dirty_count].w = w;
    comp->dirty_regions[comp->dirty_count].h = h;
    comp->dirty_count++;
}

void compositor_save_region(compositor_t *comp, int x, int y, int w, int h, uint8_t *dest) {
    if (!dest) return;

    extern uint8_t gfx_getpixel(int x, int y);

    for (int py = 0; py < h; py++) {
        int sy = y + py;
        if (sy < 0 || sy >= comp->screen_height) continue;

        for (int px = 0; px < w; px++) {
            int sx = x + px;
            if (sx < 0 || sx >= comp->screen_width) continue;

            dest[py * w + px] = gfx_getpixel(sx, sy);
        }
    }
}

void compositor_restore_region(compositor_t *comp, const uint8_t *src, int x, int y, int w, int h) {
    if (!src) return;

    extern void gfx_putpixel(int x, int y, uint8_t color);

    for (int py = 0; py < h; py++) {
        int sy = y + py;
        if (sy < 0 || sy >= comp->screen_height) continue;

        for (int px = 0; px < w; px++) {
            int sx = x + px;
            if (sx < 0 || sx >= comp->screen_width) continue;

            gfx_putpixel(sx, sy, src[py * w + px]);
        }
    }
}

void compositor_clear_dirty(compositor_t *comp) {
    comp->dirty_count = 0;
}
