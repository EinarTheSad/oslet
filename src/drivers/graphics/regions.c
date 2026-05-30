#include "gpriv.h"

void gfx_read_screen_region_packed(uint8_t *dst, int width, int height, int dest_x, int dest_y) {
    if (!backbuffer || !dst) return;

    int row_bytes = (width + 1) / 2;

    /* Byte-aligned rectangles can be copied a row at a time. */
    if (dest_x >= 0 && dest_y >= 0 && dest_x + width <= GFX_WIDTH && dest_y + height <= GFX_HEIGHT && (dest_x & 1) == 0) {
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

    /* Byte-aligned rectangles can be copied a row at a time. */
    if (dest_x >= 0 && dest_y >= 0 && dest_x + width <= GFX_WIDTH && dest_y + height <= GFX_HEIGHT && (dest_x & 1) == 0) {
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
