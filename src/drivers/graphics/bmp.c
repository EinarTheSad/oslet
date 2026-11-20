#include "gpriv.h"

typedef struct bmp_colors_s {
    uint8_t *data;
    uint32_t *colors;
    struct bmp_colors_s *next;
} bmp_colors_t;

static bmp_colors_t *bmp_colors;

const uint32_t *gfx_bmp_colors(const uint8_t *data) {
    for (bmp_colors_t *entry = bmp_colors; entry; entry = entry->next)
        if (entry->data == data) return entry->colors;
    return NULL;
}

int gfx_bmp_set_colors(uint8_t *data, uint32_t *colors) {
    bmp_colors_t *entry = kmalloc(sizeof(*entry));
    if (!entry) return -1;
    entry->data = data;
    entry->colors = colors;
    entry->next = bmp_colors;
    bmp_colors = entry;
    return 0;
}

void gfx_free_bmp(uint8_t *data) {
    bmp_colors_t **link = &bmp_colors;
    while (*link) {
        bmp_colors_t *entry = *link;
        if (entry->data == data) {
            *link = entry->next;
            kfree(entry->colors);
            kfree(entry);
            break;
        }
        link = &entry->next;
    }
    kfree(data);
}

static uint8_t find_closest_color(uint8_t r, uint8_t g, uint8_t b) {
    int best_idx = 0;
    int best_dist = 0x7FFFFFFF;
    
    for (int i = 0; i < 16; i++) {
        int dr = (int)r - (int)gfx_palette[i][0];
        int dg = (int)g - (int)gfx_palette[i][1];
        int db = (int)b - (int)gfx_palette[i][2];
        int dist = dr*dr + dg*dg + db*db;
        
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    
    return (uint8_t)best_idx;
}

uint8_t* gfx_load_bmp_to_buffer(const char *path, int *out_width, int *out_height) {
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) return NULL;

    bmp_header_t header;
    bmp_info_t info;

    if (fat32_read(f, &header, sizeof(header)) != sizeof(header)) {
        fat32_close(f);
        return NULL;
    }

    if (header.type != 0x4D42) {
        fat32_close(f);
        return NULL;
    }

    if (fat32_read(f, &info, sizeof(info)) != sizeof(info)) {
        fat32_close(f);
        return NULL;
    }

    if (info.size < sizeof(info) || header.offset < sizeof(header) ||
        info.size > header.offset - sizeof(header) ||
        info.planes != 1 || info.compression != 0) {
        fat32_close(f);
        return NULL;
    }

    if (info.bpp != 4 && info.bpp != 8 && info.bpp != 24) {
        fat32_close(f);
        return NULL;
    }

    const int MAX_DIM = 4096;
    if (info.width <= 0 || info.width > MAX_DIM ||
        info.height == 0 || info.height > MAX_DIM || info.height < -MAX_DIM) {
        fat32_close(f);
        return NULL;
    }

    int width = info.width;
    int height = info.height > 0 ? info.height : -info.height;
    int bottom_up = info.height > 0;

    int out_row_bytes = (width + 1) / 2;
    if ((size_t)height > (size_t)-1 / (size_t)out_row_bytes ||
        (size_t)height > (size_t)-1 / (size_t)width / sizeof(uint32_t)) {
        fat32_close(f);
        return NULL;
    }
    size_t buffer_size = (size_t)out_row_bytes * (size_t)height;
    uint8_t *bitmap = kmalloc(buffer_size);
    if (!bitmap) {
        fat32_close(f);
        return NULL;
    }
    memset_s(bitmap, 0, buffer_size);

    uint8_t (*palette)[3] = NULL;
    int palette_entries = 0;

    if (info.bpp <= 8) {
        unsigned max_entries = 1u << info.bpp;
        if (info.colors_used > max_entries) {
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }
        palette_entries = info.colors_used ? (int)info.colors_used : (int)max_entries;
        uint32_t palette_offset = sizeof(header) + info.size;
        if ((uint32_t)palette_entries * 4u > header.offset - palette_offset ||
            fat32_seek(f, palette_offset) != 0) {
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }
        palette = kmalloc((size_t)palette_entries * sizeof(*palette));
        if (!palette) {
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }
        
        for (int i = 0; i < palette_entries; i++) {
            uint8_t bgr[4];
            if (fat32_read(f, bgr, 4) != 4) {
                kfree(palette);
                kfree(bitmap);
                fat32_close(f);
                return NULL;
            }
            palette[i][0] = bgr[2];
            palette[i][1] = bgr[1];
            palette[i][2] = bgr[0];
        }
    }

    if (fat32_seek(f, header.offset) != 0) {
        if (palette) kfree(palette);
        kfree(bitmap);
        fat32_close(f);
        return NULL;
    }

    /* BMP rows are DWORD-aligned on disk. */
    int src_row_size;
    if (info.bpp == 24) {
        src_row_size = ((width * 3) + 3) & ~3;
    } else if (info.bpp == 8) {
        src_row_size = (width + 3) & ~3;
    } else {
        src_row_size = (((width + 1) / 2) + 3) & ~3;
    }

    uint8_t *row_buf = kmalloc(src_row_size);
    if (!row_buf) {
        if (palette) kfree(palette);
        kfree(bitmap);
        fat32_close(f);
        return NULL;
    }

    int16_t *err_r_curr = NULL, *err_g_curr = NULL, *err_b_curr = NULL;
    int16_t *err_r_next = NULL, *err_g_next = NULL, *err_b_next = NULL;
    uint32_t *colors = NULL;
    
    if (info.bpp >= 8) {
        size_t err_buf_size = (size_t)width * sizeof(int16_t);
        err_r_curr = kmalloc(err_buf_size);
        err_g_curr = kmalloc(err_buf_size);
        err_b_curr = kmalloc(err_buf_size);
        err_r_next = kmalloc(err_buf_size);
        err_g_next = kmalloc(err_buf_size);
        err_b_next = kmalloc(err_buf_size);
        
        if (!err_r_curr || !err_g_curr || !err_b_curr || !err_r_next || !err_g_next || !err_b_next) {
            if (err_r_curr) kfree(err_r_curr);
            if (err_g_curr) kfree(err_g_curr);
            if (err_b_curr) kfree(err_b_curr);
            if (err_r_next) kfree(err_r_next);
            if (err_g_next) kfree(err_g_next);
            if (err_b_next) kfree(err_b_next);
            kfree(row_buf);
            if (palette) kfree(palette);
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }
        
        for (int x = 0; x < width; x++) {
            err_r_curr[x] = err_g_curr[x] = err_b_curr[x] = 0;
            err_r_next[x] = err_g_next[x] = err_b_next[x] = 0;
        }
        colors = kmalloc((size_t)width * height * sizeof(*colors));
        if (!colors) goto invalid_bitmap;
    }

    for (int y = 0; y < height; y++) {
        if (fat32_read(f, row_buf, src_row_size) != src_row_size)
            goto invalid_bitmap;

        int dest_y = bottom_up ? (height - 1 - y) : y;
        int dest_offset = dest_y * out_row_bytes;

        if (info.bpp == 24) {
            for (int x = 0; x < width; x++) {
                int16_t r = (int16_t)row_buf[x * 3 + 2] + err_r_curr[x];
                int16_t g = (int16_t)row_buf[x * 3 + 1] + err_g_curr[x];
                int16_t b = (int16_t)row_buf[x * 3 + 0] + err_b_curr[x];
                
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                
                uint8_t color = find_closest_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
                colors[(size_t)dest_y * width + x] = ((uint32_t)color << 24) |
                    ((uint32_t)row_buf[x * 3 + 2] << 16) |
                    ((uint32_t)row_buf[x * 3 + 1] << 8) | row_buf[x * 3];
                
                int16_t err_r = r - (int16_t)gfx_palette[color][0];
                int16_t err_g = g - (int16_t)gfx_palette[color][1];
                int16_t err_b = b - (int16_t)gfx_palette[color][2];
                
                if (x + 1 < width) {
                    err_r_curr[x + 1] += (err_r * 7) / 16;
                    err_g_curr[x + 1] += (err_g * 7) / 16;
                    err_b_curr[x + 1] += (err_b * 7) / 16;
                }
                if (x > 0) {
                    err_r_next[x - 1] += (err_r * 3) / 16;
                    err_g_next[x - 1] += (err_g * 3) / 16;
                    err_b_next[x - 1] += (err_b * 3) / 16;
                }
                err_r_next[x] += (err_r * 5) / 16;
                err_g_next[x] += (err_g * 5) / 16;
                err_b_next[x] += (err_b * 5) / 16;
                if (x + 1 < width) {
                    err_r_next[x + 1] += err_r / 16;
                    err_g_next[x + 1] += err_g / 16;
                    err_b_next[x + 1] += err_b / 16;
                }
                
                int byte_idx = x / 2;
                if (x & 1) {
                    bitmap[dest_offset + byte_idx] = (bitmap[dest_offset + byte_idx] & 0xF0) | color;
                } else {
                    bitmap[dest_offset + byte_idx] = (color << 4) | (bitmap[dest_offset + byte_idx] & 0x0F);
                }
            }
            
            int16_t *tmp_r = err_r_curr; err_r_curr = err_r_next; err_r_next = tmp_r;
            int16_t *tmp_g = err_g_curr; err_g_curr = err_g_next; err_g_next = tmp_g;
            int16_t *tmp_b = err_b_curr; err_b_curr = err_b_next; err_b_next = tmp_b;
            for (int x = 0; x < width; x++) {
                err_r_next[x] = err_g_next[x] = err_b_next[x] = 0;
            }
        } else if (info.bpp == 8) {
            for (int x = 0; x < width; x++) {
                uint8_t idx = row_buf[x];
                if (idx >= palette_entries) goto invalid_bitmap;
                
                int16_t r = (int16_t)palette[idx][0] + err_r_curr[x];
                int16_t g = (int16_t)palette[idx][1] + err_g_curr[x];
                int16_t b = (int16_t)palette[idx][2] + err_b_curr[x];
                
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                
                uint8_t color = find_closest_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
                colors[(size_t)dest_y * width + x] = ((uint32_t)color << 24) |
                    ((uint32_t)palette[idx][0] << 16) |
                    ((uint32_t)palette[idx][1] << 8) | palette[idx][2];
                
                int16_t err_r = r - (int16_t)gfx_palette[color][0];
                int16_t err_g = g - (int16_t)gfx_palette[color][1];
                int16_t err_b = b - (int16_t)gfx_palette[color][2];
                
                if (x + 1 < width) {
                    err_r_curr[x + 1] += (err_r * 7) / 16;
                    err_g_curr[x + 1] += (err_g * 7) / 16;
                    err_b_curr[x + 1] += (err_b * 7) / 16;
                }
                if (x > 0) {
                    err_r_next[x - 1] += (err_r * 3) / 16;
                    err_g_next[x - 1] += (err_g * 3) / 16;
                    err_b_next[x - 1] += (err_b * 3) / 16;
                }
                err_r_next[x] += (err_r * 5) / 16;
                err_g_next[x] += (err_g * 5) / 16;
                err_b_next[x] += (err_b * 5) / 16;
                if (x + 1 < width) {
                    err_r_next[x + 1] += err_r / 16;
                    err_g_next[x + 1] += err_g / 16;
                    err_b_next[x + 1] += err_b / 16;
                }
                
                int byte_idx = x / 2;
                if (x & 1) {
                    bitmap[dest_offset + byte_idx] = (bitmap[dest_offset + byte_idx] & 0xF0) | color;
                } else {
                    bitmap[dest_offset + byte_idx] = (color << 4) | (bitmap[dest_offset + byte_idx] & 0x0F);
                }
            }
            
            int16_t *tmp_r = err_r_curr; err_r_curr = err_r_next; err_r_next = tmp_r;
            int16_t *tmp_g = err_g_curr; err_g_curr = err_g_next; err_g_next = tmp_g;
            int16_t *tmp_b = err_b_curr; err_b_curr = err_b_next; err_b_next = tmp_b;
            for (int x = 0; x < width; x++) {
                err_r_next[x] = err_g_next[x] = err_b_next[x] = 0;
            }
        } else {
            for (int x = 0; x < out_row_bytes; x++) {
                uint8_t byte_val = row_buf[x];
                /* 4bpp osLET assets already use VGA indexes. Preserve them so
                   index 5 remains the transparent icon color after palette edits. */
                bitmap[dest_offset + x] = byte_val;
            }
        }
    }
    
    if (colors && gfx_bmp_set_colors(bitmap, colors) != 0) goto invalid_bitmap;

    if (err_r_curr) kfree(err_r_curr);
    if (err_g_curr) kfree(err_g_curr);
    if (err_b_curr) kfree(err_b_curr);
    if (err_r_next) kfree(err_r_next);
    if (err_g_next) kfree(err_g_next);
    if (err_b_next) kfree(err_b_next);

    if (palette) kfree(palette);
    kfree(row_buf);
    fat32_close(f);

    *out_width = width;
    *out_height = height;
    return bitmap;

invalid_bitmap:
    if (colors) kfree(colors);
    if (err_r_curr) kfree(err_r_curr);
    if (err_g_curr) kfree(err_g_curr);
    if (err_b_curr) kfree(err_b_curr);
    if (err_r_next) kfree(err_r_next);
    if (err_g_next) kfree(err_g_next);
    if (err_b_next) kfree(err_b_next);
    if (palette) kfree(palette);
    kfree(row_buf);
    gfx_free_bmp(bitmap);
    fat32_close(f);
    return NULL;
}

void gfx_draw_cached_bmp_ex(uint8_t *cached_data, int width, int height, int dest_x, int dest_y, int transparent) {
    if (!backbuffer || !cached_data) return;
    const uint32_t *colors = gfx_bmp_colors(cached_data);

    int dirty_x0 = dest_x < 0 ? 0 : dest_x;
    int dirty_y0 = dest_y < 0 ? 0 : dest_y;
    int dirty_x1 = dest_x + width - 1;
    int dirty_y1 = dest_y + height - 1;

    if (dirty_x1 >= GFX_WIDTH) dirty_x1 = GFX_WIDTH - 1;
    if (dirty_y1 >= GFX_HEIGHT) dirty_y1 = GFX_HEIGHT - 1;

    int src_row_bytes = (width + 1) / 2;

    /* Fast path: no transparency, aligned coordinates, fully on screen */
    if (!gfx_colorbuffer && !gfx_clip_enabled && !transparent &&
        (dest_x & 1) == 0 && (width & 1) == 0 &&
        dest_x >= 0 && dest_x + width <= GFX_WIDTH) {
        int dest_byte_x = dest_x / 2;
        for (int y = 0; y < height; y++) {
            int screen_y = dest_y + y;
            if (screen_y < 0 || screen_y >= GFX_HEIGHT) continue;

            uint8_t *src = cached_data + y * src_row_bytes;
            uint8_t *dst = backbuffer + screen_y * (GFX_WIDTH / 2) + dest_byte_x;
            memcpy_s(dst, src, src_row_bytes);
        }
    } else {
        /* Slow path: pixel-by-pixel for transparency or unaligned */
        for (int y = 0; y < height; y++) {
            int screen_y = dest_y + y;
            if (screen_y < 0 || screen_y >= GFX_HEIGHT) continue;

            int src_offset = y * src_row_bytes;

            for (int x = 0; x < width; x++) {
                int byte_idx = x / 2;
                uint8_t pixel = (x & 1) ? (cached_data[src_offset + byte_idx] & 0x0F) : (cached_data[src_offset + byte_idx] >> 4);

                int screen_x = dest_x + x;

                /* Skip transparent pixels (color index 5) only if transparency enabled */
                if (screen_x >= 0 && screen_x < GFX_WIDTH &&
                    (!transparent || colors || pixel != 5)) {
                    if (colors && gfx_colorbuffer)
                        putcolor_raw(screen_x, screen_y, colors[(size_t)y * width + x]);
                    else
                        putpixel_raw(screen_x, screen_y, pixel);
                }
            }
        }
    }

    /* Mark only the actually drawn region as dirty */
    if (dirty_x1 >= dirty_x0 && dirty_y1 >= dirty_y0) {
        mark_dirty(dirty_x0, dirty_y0, dirty_x1 - dirty_x0 + 1, dirty_y1 - dirty_y0 + 1);
    }
}

void gfx_draw_cached_bmp(uint8_t *cached_data, int width, int height, int dest_x, int dest_y) {
    gfx_draw_cached_bmp_ex(cached_data, width, height, dest_x, dest_y, 1);
}

/* Draw only a sub-rectangle of a cached bitmap. */
void gfx_draw_cached_bmp_region(uint8_t *cached_data, int width, int height, int dest_x, int dest_y,
                                int src_x, int src_y, int src_w, int src_h, int transparent) {
    if (!backbuffer || !cached_data) return;
    const uint32_t *colors = gfx_bmp_colors(cached_data);

    /* Clip source rectangle to bitmap bounds */
    if (src_x < 0) { src_w += src_x; src_x = 0; }
    if (src_y < 0) { src_h += src_y; src_y = 0; }
    if (src_x >= width || src_y >= height) return;
    if (src_x + src_w > width) src_w = width - src_x;
    if (src_y + src_h > height) src_h = height - src_y;
    if (src_w <= 0 || src_h <= 0) return;

    int src_row_bytes = (width + 1) / 2;

    for (int y = 0; y < src_h; y++) {
        int screen_y = dest_y + y;
        if (screen_y < 0 || screen_y >= GFX_HEIGHT) continue;

        int src_offset = (src_y + y) * src_row_bytes;

        for (int x = 0; x < src_w; x++) {
            int sx = src_x + x;
            int byte_idx = sx / 2;
            uint8_t pixel = (sx & 1) ? (cached_data[src_offset + byte_idx] & 0x0F) : (cached_data[src_offset + byte_idx] >> 4);

            int screen_x = dest_x + x;

            if (screen_x >= 0 && screen_x < GFX_WIDTH &&
                (!transparent || colors || pixel != 5)) {
                if (colors && gfx_colorbuffer)
                    putcolor_raw(screen_x, screen_y,
                                 colors[(size_t)(src_y + y) * width + sx]);
                else
                    putpixel_raw(screen_x, screen_y, pixel);
            }
        }
    }

    /* Mark the actually drawn region as dirty (clip to screen) */
    int x0 = dest_x;
    int y0 = dest_y;
    int x1 = dest_x + src_w - 1;
    int y1 = dest_y + src_h - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_WIDTH) x1 = GFX_WIDTH - 1;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;

    if (x1 >= x0 && y1 >= y0) {
        mark_dirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
}

int gfx_load_bmp_4bit_ex(const char *path, int dest_x, int dest_y, int transparent) {
    int width, height;
    uint8_t *bitmap = gfx_load_bmp_to_buffer(path, &width, &height);
    if (!bitmap) return -1;

    gfx_draw_cached_bmp_ex(bitmap, width, height, dest_x, dest_y, transparent);
    gfx_free_bmp(bitmap);

    return 0;
}

int gfx_load_bmp_4bit(const char *path, int dest_x, int dest_y) {
    return gfx_load_bmp_4bit_ex(path, dest_x, dest_y, 1);
}
