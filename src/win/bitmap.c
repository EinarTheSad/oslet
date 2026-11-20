#include "bitmap.h"
#include "../mem/heap.h"
#include "../drivers/graphics.h"

bitmap_t* bitmap_load_from_file(const char *path) {
    if (!path || !path[0]) return NULL;

    bitmap_t *bmp = (bitmap_t*)kmalloc(sizeof(bitmap_t));
    if (!bmp) return NULL;

    int width, height;
    bmp->data = gfx_load_bmp_to_buffer(path, &width, &height);

    if (!bmp->data) {
        kfree(bmp);
        return NULL;
    }

    bmp->width = width;
    bmp->height = height;
    bmp->bits_per_pixel = 4;  // VGA 4-bit mode

    return bmp;
}

/* Nearest-neighbor scale for packed 4-bit bitmap buffers. Returns a newly allocated
   bitmap_t or NULL on failure. */
bitmap_t* bitmap_scale_nearest(bitmap_t *src, int new_w, int new_h) {
    if (!src || !src->data || new_w <= 0 || new_h <= 0 ||
        new_w > 4096 || new_h > 4096) return NULL;

    /* Allocate new bitmap structure */
    bitmap_t *dst = (bitmap_t*)kmalloc(sizeof(bitmap_t));
    if (!dst) return NULL;

    int dst_row_bytes = (new_w + 1) / 2;
    int dst_buf_size = dst_row_bytes * new_h;
    uint8_t *dst_buf = (uint8_t*)kmalloc(dst_buf_size);
    if (!dst_buf) { kfree(dst); return NULL; }
    const uint32_t *src_colors = gfx_bmp_colors(src->data);
    uint32_t *dst_colors = NULL;
    if (src_colors) {
        if ((size_t)new_w > (size_t)-1 / (size_t)new_h / sizeof(*dst_colors)) {
            kfree(dst_buf);
            kfree(dst);
            return NULL;
        }
        dst_colors = kmalloc((size_t)new_w * new_h * sizeof(*dst_colors));
        if (!dst_colors || gfx_bmp_set_colors(dst_buf, dst_colors) != 0) {
            if (dst_colors) kfree(dst_colors);
            kfree(dst_buf);
            kfree(dst);
            return NULL;
        }
    }

    /* Zero buffer to ensure nibbles are clean */
    for (int i = 0; i < dst_buf_size; i++) dst_buf[i] = 0;

    int src_row_bytes = (src->width + 1) / 2;

    /* Nearest neighbor mapping */
    for (int y = 0; y < new_h; y++) {
        /* src_y = floor(y * src_h / new_h) */
        int src_y = (y * src->height) / new_h;
        if (src_y >= src->height) src_y = src->height - 1;
        int src_offset = src_y * src_row_bytes;
        int dst_offset = y * dst_row_bytes;
        for (int x = 0; x < new_w; x++) {
            int src_x = (x * src->width) / new_w;
            if (src_x >= src->width) src_x = src->width - 1;
            if (dst_colors)
                dst_colors[(size_t)y * new_w + x] =
                    src_colors[(size_t)src_y * src->width + src_x];
            int src_byte = src_offset + (src_x / 2);
            uint8_t src_val = src->data[src_byte];
            uint8_t pixel = (src_x & 1) ? (src_val & 0x0F) : (src_val >> 4);

            int dst_byte = dst_offset + (x / 2);
            if ((x & 1) == 0) {
                /* even pixel goes into high nibble */
                dst_buf[dst_byte] &= 0x0F; /* clear high nibble */
                dst_buf[dst_byte] |= (pixel << 4);
            } else {
                /* odd pixel -> low nibble */
                dst_buf[dst_byte] &= 0xF0; /* clear low nibble */
                dst_buf[dst_byte] |= (pixel & 0x0F);
            }
        }
    }

    dst->data = dst_buf;
    dst->width = new_w;
    dst->height = new_h;
    dst->bits_per_pixel = src->bits_per_pixel;

    return dst;
}

void bitmap_draw(bitmap_t *bmp, int x, int y) {
    if (!bmp || !bmp->data) return;

    /* Draw with transparency (default) */
    gfx_draw_cached_bmp(bmp->data, bmp->width, bmp->height, x, y);
}

void bitmap_draw_opaque(bitmap_t *bmp, int x, int y) {
    if (!bmp || !bmp->data) return;

    /* Draw without transparency (force all pixels drawn) */
    gfx_draw_cached_bmp_ex(bmp->data, bmp->width, bmp->height, x, y, 0);
}

void bitmap_free(bitmap_t *bmp) {
    if (!bmp) return;

    if (bmp->data) {
        gfx_free_bmp(bmp->data);
        bmp->data = NULL;
    }

    kfree(bmp);
}
