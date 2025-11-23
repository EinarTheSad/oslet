#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#define BMF_MAGIC "BMF"
#define BMF_VERSION 1
#define BMF_MAX_NAME 28
#define BMF_MAX_SIZES 8
#define BMF_CACHE_SIZE 128
#define BMF_TRANSPARENT 0xFF

typedef struct {
    uint8_t height;
    uint8_t baseline;
    uint16_t glyph_count;
    uint8_t *glyph_data;
} bmf_sequence_t;

typedef struct {
    uint8_t ascii;
    uint8_t width;
    uint8_t pitch;
    const uint8_t *bitmap;
} bmf_glyph_t;

typedef struct {
    char name[BMF_MAX_NAME];
    uint8_t version;
    uint8_t flags;
    uint16_t size_count;
    bmf_sequence_t sequences[BMF_MAX_SIZES];
    uint8_t *data;
    bmf_glyph_t cache[BMF_CACHE_SIZE];
    uint8_t cache_height;
} bmf_font_t;

/* Core API */
int bmf_import(bmf_font_t *font, const char *path);
void bmf_free(bmf_font_t *font);
const bmf_glyph_t* bmf_get_glyph(bmf_font_t *font, uint8_t height, uint8_t ascii);

/* Rendering */
void bmf_draw_glyph(int x, int y, const bmf_glyph_t *glyph, uint8_t height, uint8_t baseline, uint8_t fg, uint8_t bg);
int bmf_measure_text(bmf_font_t *font, uint8_t height, const char *text);

/* Printf-like functions - format: x, y, color, format, args */
int bmf_vprintf(int x, int y, bmf_font_t *font, uint8_t height, uint8_t fg, const char *fmt, va_list ap);
int bmf_printf(int x, int y, bmf_font_t *font, uint8_t height, uint8_t fg, const char *fmt, ...);

/* With background */
int bmf_vprintf_bg(int x, int y, bmf_font_t *font, uint8_t height, uint8_t fg, uint8_t bg, const char *fmt, va_list ap);
int bmf_printf_bg(int x, int y, bmf_font_t *font, uint8_t height, uint8_t fg, uint8_t bg, const char *fmt, ...);

/* Convenience */
#define bmf_print(x, y, font, height, fg, text) bmf_printf(x, y, font, height, fg, "%s", text)
void bmf_test(bmf_font_t *font);