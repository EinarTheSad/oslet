#pragma once
#include <stdint.h>

#define GFX_WIDTH       640
#define GFX_HEIGHT      480
#define GFX_PLANES      4
#define GFX_BUFFER_SIZE ((GFX_WIDTH * GFX_HEIGHT) / 2)  /* 153600 bytes - 2 pixels per byte */
#define GFX_VRAM        ((uint8_t*)0xA0000)

#define COLOR_BLACK         0x00
#define COLOR_BLUE          0x01
#define COLOR_GREEN         0x02
#define COLOR_CYAN          0x03
#define COLOR_RED           0x04
#define COLOR_MAGENTA       0x05
#define COLOR_BROWN         0x06
#define COLOR_LIGHT_GRAY    0x07
#define COLOR_DARK_GRAY     0x08
#define COLOR_LIGHT_BLUE    0x09
#define COLOR_LIGHT_GREEN   0x0A
#define COLOR_LIGHT_CYAN    0x0B
#define COLOR_LIGHT_RED     0x0C
#define COLOR_LIGHT_MAGENTA 0x0D
#define COLOR_YELLOW        0x0E
#define COLOR_WHITE         0x0F

#define GRADIENT_H 0
#define GRADIENT_V 1

static const uint8_t bayer8[8][8] = {
    {  0, 48, 12, 60,  3, 51, 15, 63 },
    { 32, 16, 44, 28, 35, 19, 47, 31 },
    {  8, 56,  4, 52, 11, 59,  7, 55 },
    { 40, 24, 36, 20, 43, 27, 39, 23 },
    {  2, 50, 14, 62,  1, 49, 13, 61 },
    { 34, 18, 46, 30, 33, 17, 45, 29 },
    { 10, 58,  6, 54,  9, 57,  5, 53 },
    { 42, 26, 38, 22, 41, 25, 37, 21 }
};

typedef struct {
    uint16_t type;
    uint32_t size;
    uint16_t reserved1;
    uint16_t reserved2;
    uint32_t offset;
} __attribute__((packed)) bmp_header_t;

typedef struct {
    uint32_t size;
    int32_t width;
    int32_t height;
    uint16_t planes;
    uint16_t bpp;
    uint32_t compression;
    uint32_t image_size;
    int32_t x_ppm;
    int32_t y_ppm;
    uint32_t colors_used;
    uint32_t colors_important;
} __attribute__((packed)) bmp_info_t;

extern const uint8_t gfx_palette[16][3];

void gfx_init(void);
void gfx_enter_mode(void);
void gfx_exit_mode(void);
int gfx_is_active(void);

void gfx_clear(uint8_t color);
void gfx_swap_buffers(void);
uint8_t* gfx_get_backbuffer(void);

void gfx_putpixel(int x, int y, uint8_t color);
uint8_t gfx_getpixel(int x, int y);
void gfx_line(int x0, int y0, int x1, int y1, uint8_t color);
void gfx_rect(int x, int y, int w, int h, uint8_t color);
void gfx_fillrect(int x, int y, int w, int h, uint8_t color);
void gfx_circle(int cx, int cy, int r, uint8_t color);
void gfx_load_palette(void);

void gfx_hline(int x, int y, int w, uint8_t color);
void gfx_vline(int x, int y, int h, uint8_t color);
void gfx_floodfill(int x, int y, uint8_t new_color);
void gfx_floodfill_gradient(int x, int y,
                            uint8_t c_start, uint8_t c_end,
                            int orientation);
void gfx_fillrect_gradient(int x, int y, int w, int h,
                           uint8_t c_start, uint8_t c_end,
                           int orientation);
int gfx_load_bmp_4bit(const char *path, int dest_x, int dest_y);
int gfx_load_bmp_4bit_ex(const char *path, int dest_x, int dest_y, int transparent);
uint8_t* gfx_load_bmp_to_buffer(const char *path, int *out_width, int *out_height);
void gfx_draw_cached_bmp(uint8_t *cached_data, int width, int height, int dest_x, int dest_y);
void gfx_draw_cached_bmp_ex(uint8_t *cached_data, int width, int height, int dest_x, int dest_y, int transparent);

/* Draw a portion (src_x,src_y,src_w,src_h) of the cached bitmap to (dest_x,dest_y) */
void gfx_draw_cached_bmp_region(uint8_t *cached_data, int width, int height,
                                int dest_x, int dest_y,
                                int src_x, int src_y, int src_w, int src_h,
                                int transparent);