#include "gpriv.h"

void gfx_read_screen_region_color(uint32_t *dst, int width, int height, int x, int y) {
    if (!dst || width <= 0 || height <= 0) return;
    if (backbuffer && gfx_colorbuffer && x >= 0 && y >= 0 &&
        width <= GFX_WIDTH - x && height <= GFX_HEIGHT - y) {
        for (int row = 0; row < height; ++row)
            memcpy_s(dst + (size_t)row * width,
                     gfx_colorbuffer + (size_t)(y + row) * GFX_WIDTH + x,
                     (size_t)width * sizeof(*dst));
        return;
    }
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col)
            dst[(size_t)row * width + col] = gfx_get_color(x + col, y + row);
}

void gfx_write_screen_region_color(const uint32_t *src, int width, int height, int x, int y) {
    if (!src || width <= 0 || height <= 0) return;
    for (int row = 0; row < height; ++row)
        for (int col = 0; col < width; ++col)
            putcolor_raw(x + col, y + row, src[(size_t)row * width + col]);
    mark_dirty(x, y, width, height);
}

void gfx_read_screen_region_packed(uint8_t *dst, int width, int height, int dest_x, int dest_y) {
    if (!backbuffer || !dst) return;

    int row_bytes = (width + 1) / 2;

    /* Byte-copy only exact whole-byte regions. Odd widths include one extra pixel. */
    if (dest_x >= 0 && dest_y >= 0 && dest_x + width <= GFX_WIDTH &&
        dest_y + height <= GFX_HEIGHT && (dest_x & 1) == 0 && (width & 1) == 0) {
        int src_byte_x = dest_x / 2;
        int screen_row_bytes = GFX_WIDTH / 2;
        for (int y = 0; y < height; y++) {
            uint8_t *src = backbuffer + (dest_y + y) * screen_row_bytes + src_byte_x;
            uint8_t *dst_row = dst + y * row_bytes;
            memcpy_s(dst_row, src, (size_t)row_bytes);
        }
        return;
    }

    for (int y = 0; y < height; y++) {
        int dy = dest_y + y;
        uint8_t *dst_row = dst + y * row_bytes;
        for (int b = 0; b < row_bytes; b++) dst_row[b] = 0;

        for (int x = 0; x < width; x++) {
            int sx = dest_x + x;
            uint8_t pix = 0;
            if (sx >= 0 && sx < GFX_WIDTH && dy >= 0 && dy < GFX_HEIGHT) {
                pix = gfx_getpixel(sx, dy);
            }
            int byte_idx = x / 2;
            if (x & 1) {
                dst_row[byte_idx] = (dst_row[byte_idx] & 0xF0) | (pix & 0x0F);
            } else {
                dst_row[byte_idx] = (dst_row[byte_idx] & 0x0F) | (pix << 4);
            }
        }
    }
}

void gfx_write_screen_region_packed(uint8_t *src, int width, int height, int dest_x, int dest_y) {
    if (!backbuffer || !src) return;

    int row_bytes = (width + 1) / 2;

    /* Byte-copy only exact whole-byte regions. Odd widths include one extra pixel. */
    if (!gfx_colorbuffer && !gfx_clip_enabled && dest_x >= 0 && dest_y >= 0 && dest_x + width <= GFX_WIDTH &&
        dest_y + height <= GFX_HEIGHT && (dest_x & 1) == 0 && (width & 1) == 0) {
        int dest_byte_x = dest_x / 2;
        int screen_row_bytes = GFX_WIDTH / 2;
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = backbuffer + (dest_y + y) * screen_row_bytes + dest_byte_x;
            uint8_t *src_row = src + y * row_bytes;
            memcpy_s(dst_row, src_row, (size_t)row_bytes);
        }

        mark_dirty(dest_x, dest_y, width, height);
        return;
    }

    for (int y = 0; y < height; y++) {
        int sy = dest_y + y;
        uint8_t *src_row = src + y * row_bytes;
        for (int x = 0; x < width; x++) {
            int byte_idx = x / 2;
            uint8_t packed = src_row[byte_idx];
            uint8_t pix = (x & 1) ? (packed & 0x0F) : (packed >> 4);
            int sx = dest_x + x;
            if (sx >= 0 && sx < GFX_WIDTH && sy >= 0 && sy < GFX_HEIGHT) {
                putpixel_raw(sx, sy, pix);
            }
        }
    }
    mark_dirty(dest_x, dest_y, width, height);
}

size_t gfx_region_size(int width, int height) {
    if (width <= 0 || height <= 0) return 0;
    if (gfx_colorbuffer && (size_t)width > (size_t)-1 / sizeof(uint32_t)) return 0;
    size_t row = gfx_colorbuffer ? (size_t)width * sizeof(uint32_t) :
                                  ((size_t)width + 1u) / 2u;
    if (row > (size_t)-1 / (size_t)height) return 0;
    return row * (size_t)height;
}

void gfx_save_region(void *dst, int width, int height, int x, int y) {
    if (gfx_colorbuffer) gfx_read_screen_region_color(dst, width, height, x, y);
    else gfx_read_screen_region_packed(dst, width, height, x, y);
}

void gfx_restore_region(void *src, int width, int height, int x, int y) {
    if (gfx_colorbuffer) gfx_write_screen_region_color(src, width, height, x, y);
    else gfx_write_screen_region_packed(src, width, height, x, y);
}

uint32_t gfx_region_color(const void *src, int width, int x, int y) {
    if (gfx_colorbuffer) return ((const uint32_t*)src)[(size_t)y * width + x];
    const uint8_t *packed = src;
    uint8_t value = packed[(size_t)y * ((width + 1) / 2) + x / 2];
    return 0x80000000u | ((x & 1) ? value & 0x0F : value >> 4);
}
