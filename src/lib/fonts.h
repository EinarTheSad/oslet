#pragma once
#include <stdint.h>

#define USR_BMF_MAGIC "BMF"
#define USR_BMF_VERSION 1
#define USR_BMF_MAX_NAME 28
#define USR_BMF_MAX_SIZES 8
#define USR_BMF_CACHE_SIZE 128
#define USR_BMF_TRANSPARENT 0xFF

typedef struct usr_bmf_sequence {
    uint8_t height;
    uint8_t baseline;
    uint8_t point_size;
    uint16_t glyph_count;
    uint8_t *glyph_data;
} usr_bmf_sequence_t;

typedef struct usr_bmf_glyph {
    uint8_t ascii;
    uint8_t width;
    uint8_t pitch;
    const uint8_t *bitmap;
} usr_bmf_glyph_t;

typedef struct usr_bmf_font {
    char name[USR_BMF_MAX_NAME];
    uint8_t version;
    uint8_t flags;
    uint16_t size_count;
    usr_bmf_sequence_t sequences[USR_BMF_MAX_SIZES];
    uint8_t *data;
    usr_bmf_glyph_t cache[USR_BMF_CACHE_SIZE];
    uint8_t cache_height;
} usr_bmf_font_t;

int usr_bmf_import(usr_bmf_font_t *font, const char *path);
void usr_bmf_free(usr_bmf_font_t *font);
const usr_bmf_glyph_t* usr_bmf_get_glyph(usr_bmf_font_t *font, uint8_t height, uint8_t ascii);

int usr_bmf_printf(int x, int y, usr_bmf_font_t *font, uint8_t height,
               uint8_t fg, const char *fmt, ...);
int usr_bmf_measure(usr_bmf_font_t *font, uint8_t point_size, const char *text);

/* Use usr_bmf_* directly in user code */