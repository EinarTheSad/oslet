#include <stdint.h>
#include <stddef.h>
#include "console.h"
#include "io.h"

#define VGA_ADDRESS 0xB8000u
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

static volatile uint16_t* const VGA = (volatile uint16_t*)(uintptr_t)VGA_ADDRESS;
static uint8_t vga_color = 0x07;
static int cx = 0, cy = 0;

static inline void move_hw_cursor(void) {
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= VGA_WIDTH)  cx = VGA_WIDTH - 1;
    if (cy >= VGA_HEIGHT) cy = VGA_HEIGHT - 1;
    uint16_t pos = (uint16_t)(cy * VGA_WIDTH + cx);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)(pos >> 8));
}

static inline void put_at(char c, int x, int y) {
    VGA[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)vga_color << 8);
}

static inline void enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);
    
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

static inline void disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

static void scroll_if_needed(void) {
    if (cy < VGA_HEIGHT) return;
    for (int y = 1; y < VGA_HEIGHT; ++y) {
        for (int x = 0; x < VGA_WIDTH; ++x)
            VGA[(y - 1) * VGA_WIDTH + x] = VGA[y * VGA_WIDTH + x];
    }
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)vga_color << 8);
    for (int x = 0; x < VGA_WIDTH; ++x)
        VGA[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    cy = VGA_HEIGHT - 1;
}

static void vga_putc_internal(char c) {
    switch ((unsigned char)c) {
        case '\r': cx = 0; break;
        case '\n': cx = 0; cy++; break;
        case '\t': {
            int next = (cx + 4) & ~3;
            for (; cx < next; ++cx) put_at(' ', cx, cy);
        } break;
        case '\b':
            if (cx > 0) { cx--; put_at(' ', cx, cy); }
            break;
        default:
            put_at(c, cx, cy);
            if (++cx >= VGA_WIDTH) { cx = 0; cy++; }
            break;
    }
}

static size_t vga_write(const char* s, size_t n, void* ctx) {
    (void)ctx;
    size_t i = 0;
    for (; i < n; ++i) {
        vga_putc_internal(s[i]);
        if (cy >= VGA_HEIGHT) scroll_if_needed();
    }
    move_hw_cursor(); // one cursor update per write
    return n;
}

void vga_clear(void) {
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)vga_color << 8);
    for (int y = 0; y < VGA_HEIGHT; ++y)
        for (int x = 0; x < VGA_WIDTH; ++x)
            VGA[y * VGA_WIDTH + x] = blank;
    cx = cy = 0;
    move_hw_cursor();
}

static const console_t VGA_CONSOLE = { .write = vga_write, .ctx = 0 };

void vga_use_as_console(void) {
    console_set(&VGA_CONSOLE);
    enable_cursor(12, 12);
}

void vga_set_color(uint8_t background, uint8_t foreground) {
    vga_color = (foreground & 0x0F) | ((background & 0x0F) << 4);
}