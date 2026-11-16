#include "gpriv.h"

void gfx_line(int x0, int y0, int x1, int y1, uint8_t color) {   
    int dx = x1 - x0;
    int dy = y1 - y0;
    
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    int min_x = x0 < x1 ? x0 : x1;
    int max_x = x0 > x1 ? x0 : x1;
    int min_y = y0 < y1 ? y0 : y1;
    int max_y = y0 > y1 ? y0 : y1;
    
    while (1) {
        putpixel_raw(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    
    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}
void gfx_hline(int x, int y, int w, uint8_t color) {
    if (!backbuffer) return;
    if (y < 0 || y >= GFX_HEIGHT || w <= 0) return;

    int x0 = x;
    int x1 = x + w - 1;
    if (gfx_clip_enabled) {
        if (y < gfx_clip_y0 || y >= gfx_clip_y1) return;
        if (x0 < gfx_clip_x0) x0 = gfx_clip_x0;
        if (x1 >= gfx_clip_x1) x1 = gfx_clip_x1 - 1;
    }

    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_WIDTH) x1 = GFX_WIDTH - 1;
    
    if (x0 > x1) return;

    uint32_t row_offset = y * (GFX_WIDTH / 2);

    /* Fast path: write two pixels at a time using full byte writes where possible */
    uint8_t pattern = (color & 0x0F) | ((color & 0x0F) << 4);
    int px = x0;
    /* Handle leading odd pixel */
    if (px & 1) {
        uint32_t offset = row_offset + (px / 2);
        backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
        px++;
    }

    /* Handle full bytes */
    while (px + 1 <= x1) {
        uint32_t offset = row_offset + (px / 2);
        backbuffer[offset] = pattern;
        px += 2;
    }

    /* Trailing single pixel */
    if (px <= x1) {
        uint32_t offset = row_offset + (px / 2);
        backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
    }
    
    mark_dirty(x0, y, x1 - x0 + 1, 1);
}

void gfx_vline(int x, int y, int h, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || h <= 0) return;

    int y0 = y;
    int y1 = y + h - 1;
    if (gfx_clip_enabled) {
        if (x < gfx_clip_x0 || x >= gfx_clip_x1) return;
        if (y0 < gfx_clip_y0) y0 = gfx_clip_y0;
        if (y1 >= gfx_clip_y1) y1 = gfx_clip_y1 - 1;
    }

    if (y0 < 0) y0 = 0;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;
    
    if (y0 > y1) return;

    for (int py = y0; py <= y1; py++) {
        uint32_t offset = py * (GFX_WIDTH / 2) + (x / 2);
        if (x & 1) {
            backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
        } else {
            backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
    }
    
    mark_dirty(x, y0, 1, y1 - y0 + 1);
}


void gfx_rect(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
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
    if (gfx_clip_enabled) {
        if (x0 < gfx_clip_x0) x0 = gfx_clip_x0;
        if (y0 < gfx_clip_y0) y0 = gfx_clip_y0;
        if (x1 >= gfx_clip_x1) x1 = gfx_clip_x1 - 1;
        if (y1 >= gfx_clip_y1) y1 = gfx_clip_y1 - 1;
    }
    if (x0 > x1 || y0 > y1) return;

    /* Use optimized horizontal line writer per row */
    for (int py = y0; py <= y1; py++) {
        gfx_hline(x0, py, x1 - x0 + 1, color);
    }
    
    mark_dirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

void gfx_circle(int cx, int cy, int r, uint8_t color) {
    if (!backbuffer) return;
    if (r < 0) return;

    int x = r;
    int y = 0;
    int err = 0;

    int min_x = cx - r;
    int max_x = cx + r;
    int min_y = cy - r;
    int max_y = cy + r;

    while (x >= y) {
        putpixel_raw(cx + x, cy + y, color);
        putpixel_raw(cx - x, cy + y, color);
        putpixel_raw(cx + x, cy - y, color);
        putpixel_raw(cx - x, cy - y, color);
        putpixel_raw(cx + y, cy + x, color);
        putpixel_raw(cx - y, cy + x, color);
        putpixel_raw(cx + y, cy - x, color);
        putpixel_raw(cx - y, cy - x, color);

        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }

    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}
