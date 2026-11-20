#include "gpriv.h"

void gfx_putpixel(int x, int y, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;
    putpixel_raw(x, y, color);
    mark_dirty(x, y, 1, 1);
}

uint8_t gfx_getpixel(int x, int y) {
    if (!backbuffer) return 0;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return 0;
    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
    if (x & 1) {
        return backbuffer[offset] & 0x0F;
    } else {
        return (backbuffer[offset] >> 4) & 0x0F;
    }
}

uint32_t gfx_get_color(int x, int y) {
    if (!backbuffer || x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT)
        return 0x80000000u;
    if (gfx_colorbuffer) return gfx_colorbuffer[(size_t)y * GFX_WIDTH + x];
    return 0x80000000u | getpixel_raw(x, y);
}

void gfx_put_color(int x, int y, uint32_t color) {
    if (!backbuffer || x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT ||
        (gfx_clip_enabled && (x < gfx_clip_x0 || x >= gfx_clip_x1 ||
                              y < gfx_clip_y0 || y >= gfx_clip_y1))) return;
    putcolor_raw(x, y, color);
    mark_dirty(x, y, 1, 1);
}
