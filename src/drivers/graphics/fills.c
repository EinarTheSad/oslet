#include "gpriv.h"

static uint8_t calculate_gradient_color(int pos, int denom,
                                        uint8_t c_start, uint8_t c_end,
                                        int bx, int by) {
    int mix = (16 * pos) / denom;
    int threshold = bayer8[by][bx] >> 2;
    return mix > threshold ? c_end : c_start;
}

void gfx_fillrect_gradient(int x, int y, int w, int h,
                           uint8_t c_start, uint8_t c_end,
                           int orientation) {
    if (!backbuffer || w <= 0 || h <= 0) return;

    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 >= GFX_WIDTH || y0 >= GFX_HEIGHT) return;
    if (x1 < 0 || y1 < 0) return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_WIDTH) x1 = GFX_WIDTH - 1;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;

    int eff_w = x1 - x0 + 1;
    int eff_h = y1 - y0 + 1;
    if (eff_w <= 0 || eff_h <= 0) return;

    int denom = (orientation == GRADIENT_V)
        ? (eff_h > 1 ? eff_h - 1 : 1)
        : (eff_w > 1 ? eff_w - 1 : 1);

    for (int py = 0; py < eff_h; py++) {
        int sy = y0 + py;
        for (int px = 0; px < eff_w; px++) {
            int sx = x0 + px;
            int pos = (orientation == GRADIENT_V) ? py : px;
            uint8_t color = calculate_gradient_color(
                pos, denom, c_start, c_end, sx & 7, sy & 7);
            putpixel_raw(sx, sy, color);
        }
    }

    mark_dirty(x0, y0, eff_w, eff_h);
}
