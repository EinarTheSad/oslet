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
