#include "gpriv.h"

void gfx_floodfill(int x, int y, uint8_t new_color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    uint8_t target = getpixel_raw(x, y);
    if (target == new_color) {
        return;
    }

    size_t max_pixels = (size_t)GFX_WIDTH * (size_t)GFX_HEIGHT;
    size_t alloc_size = max_pixels * sizeof(uint32_t);
    if (alloc_size / sizeof(uint32_t) != max_pixels) {
        return;
    }
    uint32_t* stack = (uint32_t*)kmalloc(alloc_size);
    if (!stack) {
        return;
    }

    int min_x = x, max_x = x;
    int min_y = y, max_y = y;

    size_t sp = 0;
    stack[sp++] = ((uint32_t)y << 16) | (uint16_t)(x & 0xFFFF);

    while (sp > 0) {
        uint32_t v = stack[--sp];
        int cy = (int)(v >> 16);
        int cx = (int)(int16_t)(v & 0xFFFF);

        if (cx < 0 || cx >= GFX_WIDTH || cy < 0 || cy >= GFX_HEIGHT)
            continue;

        if (getpixel_raw(cx, cy) != target)
            continue;

        int lx = cx;
        int rx = cx;

        while (lx - 1 >= 0 && getpixel_raw(lx - 1, cy) == target) {
            lx--;
        }
        while (rx + 1 < GFX_WIDTH && getpixel_raw(rx + 1, cy) == target) {
            rx++;
        }

        for (int px = lx; px <= rx; px++) {
            putpixel_raw(px, cy, new_color);
        }
        
        if (lx < min_x) min_x = lx;
        if (rx > max_x) max_x = rx;
        if (cy < min_y) min_y = cy;
        if (cy > max_y) max_y = cy;

        for (int ny = cy - 1; ny <= cy + 1; ny += 2) {
            if (ny < 0 || ny >= GFX_HEIGHT)
                continue;

            int nx = lx;
            while (nx <= rx) {
                int start = -1;
                while (nx <= rx && getpixel_raw(nx, ny) == target) {
                    if (start < 0) start = nx;
                    nx++;
                }

                if (start >= 0) {
                    int seed_x = (start + nx - 1) / 2;
                    if (sp < max_pixels) {
                        stack[sp++] = ((uint32_t)ny << 16) | (uint16_t)(seed_x & 0xFFFF);
                    }
                }

                nx++;
            }
        }
    }

    kfree(stack);
    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}
static inline int clamp4(int v) {
    if (v < 0) return 0;
    if (v > 15) return 15;
    return v;
}

static uint8_t calculate_gradient_color(int pos, int denom, 
                                       uint8_t c_start, uint8_t c_end,
                                       int bx, int by) {
    int mix = (16 * pos) / denom;  // 0-16
    
    int threshold = bayer8[by][bx] >> 2;
    
    if (mix > threshold) {
        return c_end;
    } else {
        return c_start;
    }
}

void gfx_fillrect_gradient(int x, int y, int w, int h,
                           uint8_t c_start, uint8_t c_end,
                           int orientation) {
    if (!backbuffer) return;
    if (w <= 0 || h <= 0) return;

    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 >= GFX_WIDTH || y0 >= GFX_HEIGHT) return;
    if (x1 < 0 || y1 < 0) return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_WIDTH)  x1 = GFX_WIDTH - 1;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;

    int eff_w = x1 - x0 + 1;
    int eff_h = y1 - y0 + 1;

    if (eff_w <= 0 || eff_h <= 0) return;

    int denom = (orientation == GRADIENT_V)
                ? ((eff_h > 1) ? (eff_h - 1) : 1)
                : ((eff_w > 1) ? (eff_w - 1) : 1);

    for (int py = 0; py < eff_h; py++) {
        int sy = y0 + py;
        for (int px = 0; px < eff_w; px++) {
            int sx = x0 + px;

            int pos = (orientation == GRADIENT_V) ? py : px;
            int bx = sx & 7;
            int by = sy & 7;

            uint8_t final_col = calculate_gradient_color(pos, denom, c_start, c_end, bx, by);
            putpixel_raw(sx, sy, final_col);
        }
    }

    mark_dirty(x0, y0, eff_w, eff_h);
}

void gfx_floodfill_gradient(int x, int y,
                            uint8_t c_start, uint8_t c_end,
                            int orientation) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    uint8_t target = getpixel_raw(x, y);

    if (target == c_start || target == c_end) {
        return;
    }

    uint8_t placeholder = 0xFF;
    for (int i = 0; i < 16; i++) {
        if ((uint8_t)i != target && (uint8_t)i != c_start && (uint8_t)i != c_end) {
            placeholder = (uint8_t)i;
            break;
        }
    }
    if (placeholder == 0xFF) {
        return;
    }

    size_t max_pixels = (size_t)GFX_WIDTH * (size_t)GFX_HEIGHT;
    size_t alloc_size = max_pixels * sizeof(uint32_t);
    if (alloc_size / sizeof(uint32_t) != max_pixels) {
        return;
    }
    uint32_t *stack = (uint32_t*)kmalloc(alloc_size);
    if (!stack) {
        return;
    }

    int min_x = x, max_x = x;
    int min_y = y, max_y = y;

    size_t sp = 0;
    stack[sp++] = ((uint32_t)y << 16) | (uint16_t)(x & 0xFFFF);

    while (sp > 0) {
        uint32_t v = stack[--sp];
        int cy = (int)(v >> 16);
        int cx = (int)(int16_t)(v & 0xFFFF);

        if (cx < 0 || cx >= GFX_WIDTH || cy < 0 || cy >= GFX_HEIGHT)
            continue;

        if (getpixel_raw(cx, cy) != target)
            continue;

        int lx = cx;
        int rx = cx;

        while (lx - 1 >= 0 && getpixel_raw(lx - 1, cy) == target) lx--;
        while (rx + 1 < GFX_WIDTH && getpixel_raw(rx + 1, cy) == target) rx++;

        for (int px = lx; px <= rx; px++)
            putpixel_raw(px, cy, placeholder);

        if (lx < min_x) min_x = lx;
        if (rx > max_x) max_x = rx;
        if (cy < min_y) min_y = cy;
        if (cy > max_y) max_y = cy;

        for (int ny = cy - 1; ny <= cy + 1; ny += 2) {
            if (ny < 0 || ny >= GFX_HEIGHT) continue;

            int nx = lx;
            while (nx <= rx) {
                while (nx <= rx && getpixel_raw(nx, ny) != target) nx++;
                if (nx > rx) break;
                int start = nx;
                while (nx <= rx && getpixel_raw(nx, ny) == target) nx++;
                int seed_x = (start + nx - 1) / 2;
                if (sp < max_pixels)
                    stack[sp++] = ((uint32_t)ny << 16) | (uint16_t)(seed_x & 0xFFFF);
            }
        }
    }

    kfree(stack);

    int eff_w = max_x - min_x + 1;
    int eff_h = max_y - min_y + 1;
    if (eff_w <= 0 || eff_h <= 0) {
        return;
    }

    int denom = (orientation == GRADIENT_V)
                ? ((eff_h > 1) ? (eff_h - 1) : 1)
                : ((eff_w > 1) ? (eff_w - 1) : 1);

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            if (getpixel_raw(px, py) != placeholder) continue;

            int pos = (orientation == GRADIENT_V)
                      ? (py - min_y)
                      : (px - min_x);

            int bx = px & 7;
            int by = py & 7;

            uint8_t final_col = calculate_gradient_color(pos, denom, c_start, c_end, bx, by);
            putpixel_raw(px, py, final_col);
        }
    }

    mark_dirty(min_x, min_y, eff_w, eff_h);
}
