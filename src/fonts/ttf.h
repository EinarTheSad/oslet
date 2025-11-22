#pragma once
#include <stdint.h>
#include <stddef.h>

#define TTF_MAX_FONTS 8
#define TTF_GLYPH_CACHE_SIZE 128

typedef struct {
    int16_t x_min, y_min;
    int16_t x_max, y_max;
} ttf_bbox_t;

typedef struct {
    uint8_t* bitmap;
    int width;
    int height;
    int advance;
    int bearing_x;
    int bearing_y;
} ttf_glyph_t;

typedef struct {
    uint16_t codepoint;
    uint8_t size;
    ttf_glyph_t glyph;
} ttf_cache_entry_t;

typedef struct {
    char name[64];
    uint8_t* data;
    uint32_t data_size;
    
    uint16_t units_per_em;
    int16_t ascent;
    int16_t descent;
    int16_t line_gap;
    
    uint32_t num_glyphs;
    uint32_t* loca_table;
    uint8_t* glyf_table;
    uint8_t* cmap_table;
    uint8_t* hmtx_table;
    
    ttf_cache_entry_t cache[TTF_GLYPH_CACHE_SIZE];
    uint32_t cache_head;
    
    int in_use;
} ttf_font_t;

void ttf_init(void);
ttf_font_t* ttf_load(const char* path);
void ttf_free(ttf_font_t* font);
ttf_glyph_t* ttf_render_glyph(ttf_font_t* font, uint16_t codepoint, uint8_t size);
int ttf_get_kerning(ttf_font_t* font, uint16_t left, uint16_t right, uint8_t size);
void ttf_print(int x, int y, const char* text, ttf_font_t* font, uint8_t size, uint8_t color);