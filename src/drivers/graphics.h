#pragma once
#include <stdint.h>
#include <stddef.h>

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

void gfx_putchar(int x, int y, char c, uint8_t fg, uint8_t bg);
void gfx_print(int x, int y, const char* str, uint8_t fg, uint8_t bg);