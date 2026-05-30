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
        if (x0 >= 0 && x0 < GFX_WIDTH && y0 >= 0 && y0 < GFX_HEIGHT) {
            uint32_t offset = y0 * (GFX_WIDTH / 2) + (x0 / 2);
            if (x0 & 1) {
                backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            } else {
                backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
            }
        }
        
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


void gfx_filltriangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color) {
    if (!backbuffer) return;

    int min_x = x1 < x2 ? (x1 < x3 ? x1 : x3) : (x2 < x3 ? x2 : x3);
    int max_x = x1 > x2 ? (x1 > x3 ? x1 : x3) : (x2 > x3 ? x2 : x3);
    int min_y = y1 < y2 ? (y1 < y3 ? y1 : y3) : (y2 < y3 ? y2 : y3);
    int max_y = y1 > y2 ? (y1 > y3 ? y1 : y3) : (y2 > y3 ? y2 : y3);

    if (min_x < 0) min_x = 0;
    if (max_x >= GFX_WIDTH) max_x = GFX_WIDTH - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= GFX_HEIGHT) max_y = GFX_HEIGHT - 1;

    for (int y = min_y; y <= max_y; y++) {
        int x_min = GFX_WIDTH;
        int x_max = -1;

        int dy12 = y2 - y1;
        int dy23 = y3 - y2;
        int dy31 = y1 - y3;

        if (dy12 == 0) dy12 = 1;
        if (dy23 == 0) dy23 = 1;
        if (dy31 == 0) dy31 = 1;

        if ((y1 <= y && y <= y2) || (y2 <= y && y <= y1)) {
            int x_at = x1 + ((y - y1) * (x2 - x1)) / dy12;
            if (x_at < x_min) x_min = x_at;
            if (x_at > x_max) x_max = x_at;
        }
        if ((y2 <= y && y <= y3) || (y3 <= y && y <= y2)) {
            int x_at = x2 + ((y - y2) * (x3 - x2)) / dy23;
            if (x_at < x_min) x_min = x_at;
            if (x_at > x_max) x_max = x_at;
        }
        if ((y3 <= y && y <= y1) || (y1 <= y && y <= y3)) {
            int x_at = x3 + ((y - y3) * (x1 - x3)) / dy31;
            if (x_at < x_min) x_min = x_at;
            if (x_at > x_max) x_max = x_at;
        }

        if (x_min < 0) x_min = 0;
        if (x_max > GFX_WIDTH) x_max = GFX_WIDTH;

        for (int x = x_min; x <= x_max; x++) {
            putpixel_raw(x, y, color);
        }
    }

    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
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
        if (cx + x >= 0 && cx + x < GFX_WIDTH && cy + y >= 0 && cy + y < GFX_HEIGHT) {
            uint32_t offset = (cy + y) * (GFX_WIDTH / 2) + ((cx + x) / 2);
            if ((cx + x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - x >= 0 && cx - x < GFX_WIDTH && cy + y >= 0 && cy + y < GFX_HEIGHT) {
            uint32_t offset = (cy + y) * (GFX_WIDTH / 2) + ((cx - x) / 2);
            if ((cx - x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + x >= 0 && cx + x < GFX_WIDTH && cy - y >= 0 && cy - y < GFX_HEIGHT) {
            uint32_t offset = (cy - y) * (GFX_WIDTH / 2) + ((cx + x) / 2);
            if ((cx + x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - x >= 0 && cx - x < GFX_WIDTH && cy - y >= 0 && cy - y < GFX_HEIGHT) {
            uint32_t offset = (cy - y) * (GFX_WIDTH / 2) + ((cx - x) / 2);
            if ((cx - x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + y >= 0 && cx + y < GFX_WIDTH && cy + x >= 0 && cy + x < GFX_HEIGHT) {
            uint32_t offset = (cy + x) * (GFX_WIDTH / 2) + ((cx + y) / 2);
            if ((cx + y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - y >= 0 && cx - y < GFX_WIDTH && cy + x >= 0 && cy + x < GFX_HEIGHT) {
            uint32_t offset = (cy + x) * (GFX_WIDTH / 2) + ((cx - y) / 2);
            if ((cx - y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + y >= 0 && cx + y < GFX_WIDTH && cy - x >= 0 && cy - x < GFX_HEIGHT) {
            uint32_t offset = (cy - x) * (GFX_WIDTH / 2) + ((cx + y) / 2);
            if ((cx + y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - y >= 0 && cx - y < GFX_WIDTH && cy - x >= 0 && cy - x < GFX_HEIGHT) {
            uint32_t offset = (cy - x) * (GFX_WIDTH / 2) + ((cx - y) / 2);
            if ((cx - y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
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

void gfx_fillcircle(int cx, int cy, int r, uint8_t color) {
    if (!backbuffer) return;
    if (r < 0) return;

    int min_x = cx - r;
    int max_x = cx + r;
    int min_y = cy - r;
    int max_y = cy + r;

    if (min_x < 0) min_x = 0;
    if (max_x >= GFX_WIDTH) max_x = GFX_WIDTH - 1;
    if (min_y < 0) min_y = 0;
    if (max_y >= GFX_HEIGHT) max_y = GFX_HEIGHT - 1;

    int r2 = r * r;
    for (int y = min_y; y <= max_y; y++) {
        int dy = y - cy;
        int dx2 = r2 - dy * dy;
        if (dx2 < 0) dx2 = 0;
        int half_width = 0;
        while ((half_width + 1) * (half_width + 1) <= dx2) half_width++;
        for (int x = cx - half_width; x <= cx + half_width; x++) {
            putpixel_raw(x, y, color);
        }
    }

    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

void gfx_triangle(int x1, int y1, int x2, int y2, int x3, int y3, uint8_t color) {
    gfx_line(x1, y1, x2, y2, color);
    gfx_line(x2, y2, x3, y3, color);
    gfx_line(x3, y3, x1, y1, color);
}
