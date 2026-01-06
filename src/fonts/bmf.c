#include "bmf.h"
#include "../drivers/graphics.h"
#include "../drivers/fat32.h"
#include "../mem/heap.h"
#include "../console.h"
#include <stdarg.h>

static inline uint8_t read_u8(const uint8_t **p) {
    return *(*p)++;
}

static inline uint16_t read_u16(const uint8_t **p) {
    uint8_t lo = read_u8(p);
    uint8_t hi = read_u8(p);
    return (uint16_t)(lo | (hi << 8));
}

static int bmf_find_sequence_for_size(bmf_font_t *font, uint8_t point_size) {
    if (!font) return -1;
    for (int i = 0; i < font->size_count; i++) {
        if (font->sequences[i].point_size == point_size)
            return i;
    }
    return -1;
}

static int bmf_load_from_memory(bmf_font_t *font, const uint8_t *data, size_t size) {
    if (!font || !data || size < 36) return -1;
    
    const uint8_t *p = data;
    
    /* Verify magic */
    if (p[0] != 'B' || p[1] != 'M' || p[2] != 'F' || p[3] != 0) {
        return -1;
    }
    p += 4;
    
    /* Read font name */
    memcpy_s(font->name, p, BMF_MAX_NAME);
    font->name[BMF_MAX_NAME - 1] = 0;
    p += BMF_MAX_NAME;
    
    /* Version & flags */
    font->version = read_u8(&p);
    font->flags = read_u8(&p);
    font->size_count = read_u16(&p);
    
    if (font->version != BMF_VERSION || font->size_count > BMF_MAX_SIZES) {
        return -1;
    }
    
    /* Allocate copy of data */
    font->data = kmalloc(size);
    if (!font->data) return -1;
    memcpy_s(font->data, data, size);
    
    /* Parse all sequences */
    p = font->data + 36;
    for (uint16_t s = 0; s < font->size_count; s++) {
        bmf_sequence_t *seq = &font->sequences[s];
        
        seq->height = read_u8(&p);
        seq->baseline = read_u8(&p);
        seq->point_size = read_u8(&p);
        seq->glyph_count = read_u16(&p);
        seq->glyph_data = (uint8_t*)p;
        
        /* Skip glyph data for this sequence */
        for (uint16_t g = 0; g < seq->glyph_count; g++) {
            if ((size_t)(p - font->data) + 3 > size) break;
            
            read_u8(&p);  // ascii
            uint8_t width = read_u8(&p);
            uint8_t pitch = read_u8(&p);
            (void)width;
            
            size_t bitmap_size = pitch * seq->height;
            
            if ((size_t)(p - font->data) + bitmap_size > size) break;
            p += bitmap_size;
        }
    }
    
    /* Clear cache */
    memset_s(font->cache, 0, sizeof(font->cache));
    font->cache_height = 0xFF;
    
    return 0;
}

int bmf_import(bmf_font_t *font, const char *path) {
    if (!font || !path) return -1;
    
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) return -1;
    
    uint32_t size = f->size;
    if (size < 36) {
        fat32_close(f);
        return -1;
    }
    
    uint8_t *data = kmalloc(size);
    if (!data) {
        fat32_close(f);
        return -1;
    }
    
    int bytes = fat32_read(f, data, size);
    fat32_close(f);
    
    if (bytes != (int)size) {
        kfree(data);
        return -1;
    }
    
    int result = bmf_load_from_memory(font, data, size);
    kfree(data);
    
    return result;
}

void bmf_free(bmf_font_t *font) {
    if (font && font->data) {
        kfree(font->data);
        font->data = NULL;
    }
}

const bmf_glyph_t* bmf_get_glyph(bmf_font_t *font, uint8_t point_size, uint8_t ascii) {
    if (!font || !font->data) return NULL;

    int seq_idx = bmf_find_sequence_for_size(font, point_size);
    if (seq_idx < 0) return NULL;

    bmf_sequence_t *seq = &font->sequences[seq_idx];

    /* Check cache */
    if (font->cache_height == seq->height && ascii < BMF_CACHE_SIZE) {
        bmf_glyph_t *cached = &font->cache[ascii];
        if (cached->bitmap) return cached;
    } else if (font->cache_height != seq->height) {
        memset_s(font->cache, 0, sizeof(font->cache));
        font->cache_height = seq->height;
    }

    const uint8_t *p = seq->glyph_data;
    for (uint16_t i = 0; i < seq->glyph_count; i++) {
        uint8_t code = *p++;
        uint8_t width = *p++;
        uint8_t pitch = *p++;

        size_t bitmap_size = pitch * seq->height;

        if (code == ascii) {
            if (ascii < BMF_CACHE_SIZE) {
                font->cache[ascii].ascii = code;
                font->cache[ascii].width = width;
                font->cache[ascii].pitch = pitch;
                font->cache[ascii].bitmap = p;
                return &font->cache[ascii];
            }

            static bmf_glyph_t temp;
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

void bmf_draw_glyph(int x, int y, const bmf_glyph_t *glyph, uint8_t height, uint8_t baseline, uint8_t fg, uint8_t bg) {
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
                gfx_putpixel(x + col, y_base + row, fg);
            } else if (bg != BMF_TRANSPARENT) {
                gfx_putpixel(x + col, y_base + row, bg);
            }
        }
    }
}

int bmf_measure_text(bmf_font_t *font, uint8_t point_size, const char *text) {
    if (!font || !text) return 0;

    int seq_idx = bmf_find_sequence_for_size(font, point_size);
    if (seq_idx < 0) return 0;

    int width = 0;
    while (*text) {
        const bmf_glyph_t *g = bmf_get_glyph(font, point_size, (uint8_t)*text);
        if (g) width += g->width + 1;
        text++;
    }
    return width;
}

/* Printf implementation */

typedef struct {
    bmf_font_t *font;
    uint8_t point_size;
    uint8_t height;
    uint8_t baseline;
    int x;
    int line_start_x;
    int y;
    uint8_t fg;
    uint8_t bg;
    int written;
} bmf_ctx_t;

static void bmf_emit(char ch, void *user) {
    bmf_ctx_t *ctx = (bmf_ctx_t*)user;

    if (ch == '\n') {
        ctx->x = ctx->line_start_x;
        ctx->y += ctx->height + 2;
        return;
    }

    if (ch == '\t') {
        const bmf_glyph_t *space = bmf_get_glyph(ctx->font, ctx->height, ' ');
        int tab_width = space ? space->width * 4 : 16;
        ctx->x = ((ctx->x + tab_width) / tab_width) * tab_width;
        return;
    }

    const bmf_glyph_t *glyph = bmf_get_glyph(ctx->font, ctx->point_size, (uint8_t)ch);
    if (glyph) {
        bmf_draw_glyph(ctx->x, ctx->y, glyph, ctx->height, ctx->baseline, ctx->fg, ctx->bg);
        ctx->x += glyph->width;
        ctx->written++;
    }
}

int bmf_vprintf_bg(int x, int y, bmf_font_t *font, uint8_t point_size,
                   uint8_t fg, uint8_t bg, const char *fmt, va_list ap) {
    if (!font || !fmt) return -1;

    int seq_idx = bmf_find_sequence_for_size(font, point_size);
    if (seq_idx < 0) return -1;

    uint8_t height = font->sequences[seq_idx].height;
    uint8_t baseline = font->sequences[seq_idx].baseline;

    bmf_ctx_t ctx = {
        .font = font,
        .point_size = point_size,
        .height = height,
        .baseline = baseline,
        .x = x,
        .line_start_x = x,
        .y = y,
        .fg = fg,
        .bg = bg,
        .written = 0
    };

    extern int kvprintf(const char *fmt, va_list ap, void (*emit)(char, void*), void *user);

    va_list cp;
    va_copy(cp, ap);
    kvprintf(fmt, cp, bmf_emit, &ctx);
    va_end(cp);

    return ctx.written;
}

int bmf_printf_bg(int x, int y, bmf_font_t *font, uint8_t height,
                  uint8_t fg, uint8_t bg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = bmf_vprintf_bg(x, y, font, height, fg, bg, fmt, ap);
    va_end(ap);
    return ret;
}

int bmf_vprintf(int x, int y, bmf_font_t *font, uint8_t height,
                uint8_t fg, const char *fmt, va_list ap) {
    return bmf_vprintf_bg(x, y, font, height, fg, BMF_TRANSPARENT, fmt, ap);
}

int bmf_printf(int x, int y, bmf_font_t *font, uint8_t height,
               uint8_t fg, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int ret = bmf_vprintf(x, y, font, height, fg, fmt, ap);
    va_end(ap);
    return ret;
}