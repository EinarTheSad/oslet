#include "bitmap.h"
#include "../mem/heap.h"
#include "../drivers/graphics.h"
#include "../console.h"

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

static uint32_t bitmap_rgb(const bitmap_t *src, const uint32_t *colors,
                           int x, int y, int masked) {
    size_t pos = (size_t)y * src->width + x;
    if (colors) return colors[pos] & (GFX_BMP_TRANSPARENT | 0x00FFFFFFu);
    int row_bytes = (src->width + 1) / 2;
    uint8_t packed = src->data[(size_t)y * row_bytes + x / 2];
    uint8_t index = (x & 1) ? packed & 15 : packed >> 4;
    uint32_t rgb = ((uint32_t)gfx_palette[index][0] << 16) |
                   ((uint32_t)gfx_palette[index][1] << 8) | gfx_palette[index][2];
    return masked && index == 5 ? rgb | GFX_BMP_TRANSPARENT : rgb;
}

bitmap_t* bitmap_scale_bilinear(bitmap_t *src, int new_w, int new_h, int transparent) {
    if (!src || !src->data || src->width <= 0 || src->height <= 0 ||
        src->width > 4096 || src->height > 4096 || new_w <= 0 || new_h <= 0 ||
        new_w > 4096 || new_h > 4096) return NULL;

    bitmap_t *dst = NULL;
    uint8_t *dst_buf = NULL;
    uint32_t *dst_colors = NULL;
    int dst_row_bytes = (new_w + 1) / 2;
    int dst_buf_size = dst_row_bytes * new_h;
    dst = kmalloc(sizeof(*dst));
    dst_buf = kmalloc(dst_buf_size);
    dst_colors = kmalloc((size_t)new_w * new_h * sizeof(*dst_colors));
    if (!dst || !dst_buf || !dst_colors) goto fail;

    const uint32_t *src_colors = gfx_bmp_colors(src->data);
    int masked = transparent && (!src_colors || gfx_bmp_has_mask(src->data));

    /* Sample pixel centres with 16.16 coordinates and 8-bit fractions. */
    int x_step = (int)(((uint32_t)src->width << 16) / (uint32_t)new_w);
    int y_step = (int)(((uint32_t)src->height << 16) / (uint32_t)new_h);
    int y_pos = y_step / 2 - 32768;
    for (int y = 0; y < new_h; y++) {
        int sy = y_pos < 0 ? 0 : y_pos >> 16;
        int fy = y_pos < 0 ? 0 : (y_pos >> 8) & 255;
        if (sy >= src->height - 1) { sy = src->height - 1; fy = 0; }
        int sy1 = fy ? sy + 1 : sy;
        int x_pos = x_step / 2 - 32768;
        for (int x = 0; x < new_w; x++) {
            int sx = x_pos < 0 ? 0 : x_pos >> 16;
            int fx = x_pos < 0 ? 0 : (x_pos >> 8) & 255;
            if (sx >= src->width - 1) { sx = src->width - 1; fx = 0; }
            int sx1 = fx ? sx + 1 : sx;
            uint32_t samples[4] = {
                bitmap_rgb(src, src_colors, sx, sy, masked),
                bitmap_rgb(src, src_colors, sx1, sy, masked),
                bitmap_rgb(src, src_colors, sx, sy1, masked),
                bitmap_rgb(src, src_colors, sx1, sy1, masked)
            };
            unsigned weights[4] = {
                (unsigned)(256 - fx) * (256 - fy), (unsigned)fx * (256 - fy),
                (unsigned)(256 - fx) * fy, (unsigned)fx * fy
            };
            unsigned r = 0, g = 0, b = 0, total = 0;
            for (int i = 0; i < 4; i++) {
                if (masked && (samples[i] & GFX_BMP_TRANSPARENT)) continue;
                total += weights[i];
                r += ((samples[i] >> 16) & 255) * weights[i];
                g += ((samples[i] >> 8) & 255) * weights[i];
                b += (samples[i] & 255) * weights[i];
            }
            /* Ignore transparent samples when mixing colours to avoid fringes. */
            if (masked && total < 32768) {
                dst_colors[(size_t)y * new_w + x] = GFX_BMP_TRANSPARENT |
                    ((uint32_t)gfx_palette[5][0] << 16) |
                    ((uint32_t)gfx_palette[5][1] << 8) | gfx_palette[5][2];
            } else {
                if (total == 65536) {
                    r = (r + 32768) >> 16;
                    g = (g + 32768) >> 16;
                    b = (b + 32768) >> 16;
                } else {
                    r = (r + total / 2) / total;
                    g = (g + total / 2) / total;
                    b = (b + total / 2) / total;
                }
                dst_colors[(size_t)y * new_w + x] = (r << 16) | (g << 8) | b;
            }
            x_pos += x_step;
        }
        y_pos += y_step;
    }

    if (gfx_dither_colors(dst_buf, dst_colors, new_w, new_h, masked) != 0 ||
        (masked ? gfx_bmp_set_masked_colors(dst_buf, dst_colors) :
                  gfx_bmp_set_colors(dst_buf, dst_colors)) != 0) goto fail;
    dst->data = dst_buf;
    dst->width = new_w;
    dst->height = new_h;
    dst->bits_per_pixel = src->bits_per_pixel;

    return dst;

fail:
    printf("Could not scale bitmap to %dx%d\n", new_w, new_h);
    if (dst_colors) kfree(dst_colors);
    if (dst_buf) kfree(dst_buf);
    if (dst) kfree(dst);
    return NULL;
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
