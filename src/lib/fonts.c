#include "fonts.h"
#include "../syscall.h"
#include "string.h"
#include "stdlib.h"
#include "stdio.h"

static inline uint8_t read_u8(const uint8_t **p) {
    return *(*p)++;
}

static inline uint16_t read_u16(const uint8_t **p) {
    uint8_t lo = read_u8(p);
    uint8_t hi = read_u8(p);
    return (uint16_t)(lo | (hi << 8));
}

static int usr_bmf_find_sequence_for_size(usr_bmf_font_t *font, uint8_t point_size) {
    if (!font) return -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == point_size)
            return i;
    }
    return -1;
}

static int usr_bmf_load_from_memory(usr_bmf_font_t *font, const uint8_t *data, size_t size) {
    if (!font || !data || size < 36) return -1;
    
    const uint8_t *p = data;
    
    if (p[0] != 'B' || p[1] != 'M' || p[2] != 'F' || p[3] != 0) return -1;
    p += 4;
    
    memcpy(font->name, p, USR_BMF_MAX_NAME);
    font->name[USR_BMF_MAX_NAME - 1] = 0;
    p += USR_BMF_MAX_NAME;
    
    font->version = read_u8(&p);
    font->flags = read_u8(&p);
    font->size_count = read_u16(&p);
    
    if (font->version != USR_BMF_VERSION || font->size_count > USR_BMF_MAX_SIZES) return -1;
    
    font->data = malloc(size);
    if (!font->data) return -1;
    memcpy(font->data, data, size);
    
    p = font->data + 36;
    for (uint16_t s = 0; s < font->size_count; s++) {
        usr_bmf_sequence_t *seq = &font->sequences[s];
        
        seq->height = read_u8(&p);
        seq->baseline = read_u8(&p);
        seq->point_size = read_u8(&p);
        seq->glyph_count = read_u16(&p);
        seq->glyph_data = (uint8_t*)p;
        
        for (uint16_t g = 0; g < seq->glyph_count; g++) {
            if ((size_t)(p - font->data) + 3 > size) break;
            
            read_u8(&p);
            uint8_t width = read_u8(&p);
            uint8_t pitch = read_u8(&p);
            (void)width;
            
            size_t bitmap_size = pitch * seq->height;
            if ((size_t)(p - font->data) + bitmap_size > size) break;
            p += bitmap_size;
        }
    }
    
    memset(font->cache, 0, sizeof(font->cache));
    font->cache_height = 0xFF;
    
    return 0;
}

int usr_bmf_import(usr_bmf_font_t *font, const char *path) {
    if (!font || !path) return -1;
    
    int fd = sys_open(path, "r");
    if (fd < 0 || fd == (int)0xFFFFFFFF) {
        sys_write("Failed to open: ");
        sys_write(path);
        sys_write("\n");
        return -1;
    }
    
    char buf[512];
    int total = 0;
    int n;
    uint8_t *data = NULL;
    size_t capacity = 0;
    
    while ((n = sys_read(fd, buf, 512)) > 0) {
        if (total + n > (int)capacity) {
            capacity = (total + n) * 2;
            uint8_t *new_data = malloc(capacity);
            if (!new_data) {
                sys_write("Malloc failed\n");
                free(data);
                sys_close(fd);
                return -1;
            }
            if (data) {
                memcpy(new_data, data, total);
                free(data);
            }
            data = new_data;
        }
        memcpy(data + total, buf, n);
        total += n;
    }
    
    sys_close(fd);
    
    if (total < 36) {
        free(data);
        return -1;
    }
    
    int result = usr_bmf_load_from_memory(font, data, total);
    free(data);
    
    return result;
}

void usr_bmf_free(usr_bmf_font_t *font) {
    if (font && font->data) {
        free(font->data);
        font->data = NULL;
    }
}

const usr_bmf_glyph_t* usr_bmf_get_glyph(usr_bmf_font_t *font, uint8_t point_size, uint8_t ascii) {
    if (!font || !font->data) return NULL;

    int seq_idx = usr_bmf_find_sequence_for_size(font, point_size);
    if (seq_idx < 0) return NULL;

    usr_bmf_sequence_t *seq = &font->sequences[seq_idx];

    if (font->cache_height == seq->height && ascii < USR_BMF_CACHE_SIZE) {
        usr_bmf_glyph_t *cached = &font->cache[ascii];
        if (cached->bitmap) return cached;
    } else if (font->cache_height != seq->height) {
        memset(font->cache, 0, sizeof(font->cache));
        font->cache_height = seq->height;
    }

    const uint8_t *p = seq->glyph_data;
    for (uint16_t i = 0; i < seq->glyph_count; i++) {
        uint8_t code = *p++;
        uint8_t width = *p++;
        uint8_t pitch = *p++;

        size_t bitmap_size = pitch * seq->height;

        if (code == ascii) {
            if (ascii < USR_BMF_CACHE_SIZE) {
                font->cache[ascii].ascii = code;
                font->cache[ascii].width = width;
                font->cache[ascii].pitch = pitch;
                font->cache[ascii].bitmap = p;
                return &font->cache[ascii];
            }

            static usr_bmf_glyph_t temp;
            temp.ascii = code;
            temp.width = width;
            temp.pitch = pitch;
            temp.bitmap = p;
            return &temp;
        }

        p += bitmap_size;
    }

    return NULL;
}

static void usr_bmf_draw_glyph(int x, int y, const usr_bmf_glyph_t *glyph, 
                           uint8_t height, uint8_t baseline, uint8_t fg) {
    if (!glyph || !glyph->bitmap) return;
    if (glyph->width > 100 || height > 100) return;

    const uint8_t *bitmap = glyph->bitmap;
    int y_base = y - (height - baseline);

    for (uint8_t row = 0; row < height; row++) {
        for (uint8_t col = 0; col < glyph->width; col++) {
            uint8_t byte_idx = col / 8;
            uint8_t bit_idx = 7 - (col % 8);

            if (row * glyph->pitch + byte_idx >= height * glyph->pitch) continue;

            uint8_t byte = bitmap[row * glyph->pitch + byte_idx];
            uint8_t pixel = (byte >> bit_idx) & 1;

            if (pixel) {
                sys_gfx_putpixel(x + col, y_base + row, fg);
            }
        }
    }
}

int usr_bmf_printf(int x, int y, usr_bmf_font_t *font, uint8_t point_size,
               uint8_t fg, const char *fmt, ...) {
    if (!font || !fmt) return -1;

    int seq_idx = usr_bmf_find_sequence_for_size(font, point_size);
    if (seq_idx < 0) return -1;

    usr_bmf_sequence_t *seq = &font->sequences[seq_idx];
    uint8_t height = seq->height;
    uint8_t baseline = seq->baseline;

    char buffer[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    int cx = x;
    int cy = y;
    int written = 0;

    for (const char *p = buffer; *p; p++) {
        if (*p == '\n') {
            cx = x;
            cy += height + 2;
            continue;
        }

        if (*p == '\t') {
            const usr_bmf_glyph_t *space = usr_bmf_get_glyph(font, point_size, ' ');
            int tab_width = space ? space->width * 4 : 16;
            cx = ((cx + tab_width) / tab_width) * tab_width;
            continue;
        }

        const usr_bmf_glyph_t *glyph = usr_bmf_get_glyph(font, point_size, (uint8_t)*p);
        if (glyph) {
            usr_bmf_draw_glyph(cx, cy, glyph, height, baseline, fg);
            cx += glyph->width;
            written++;
        }
    }

    return written;
}

int usr_bmf_measure(usr_bmf_font_t *font, uint8_t point_size, const char *text) {
    if (!font || !text) return 0;

    int width = 0;
    for (const char *p = text; *p; p++) {
        const usr_bmf_glyph_t *glyph = usr_bmf_get_glyph(font, point_size, (uint8_t)*p);
        if (glyph) {
            width += glyph->width;
        }
    }
    return width;
}