#include <stdarg.h>
#include "osletio.h"

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static uint16_t* const VGA_BUFFER = (uint16_t*)VGA_ADDRESS;
static int cursor_x = 0, cursor_y = 0;
static uint8_t color = 0x07; // light gray on black

static inline void update_cursor() {
    // optional: write to VGA I/O ports 0x3D4, 0x3D5 to move cursor
}

void vga_putc(char c) {
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
    } else {
        VGA_BUFFER[cursor_y * VGA_WIDTH + cursor_x] = (uint16_t)c | (uint16_t)color << 8;
        cursor_x++;
    }
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VGA_HEIGHT) cursor_y = 0;
    update_cursor();
}

void vga_puts(const char* str) {
    while (*str) vga_putc(*str++);
}

void vga_clear() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
        VGA_BUFFER[i] = (uint16_t)' ' | (uint16_t)color << 8;
    cursor_x = cursor_y = 0;
    update_cursor();
}

void vga_putint(int n) {
    char buf[16];
    int i = 0;

    if (n == 0) {
        vga_putc('0');
        return;
    }

    if (n < 0) {
        vga_putc('-');
        n = -n;
    }

    while (n > 0 && i < 15) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }

    while (i--) vga_putc(buf[i]);
}

void vga_puthex(unsigned int n) {
    char hex[] = "0123456789ABCDEF";
    char buf[9];
    int i = 0;

    if (n == 0) {
        vga_puts("0x0");
        return;
    }

    while (n && i < 8) {
        buf[i++] = hex[n & 0xF];
        n >>= 4;
    }

    vga_puts("0x");
    while (i--) vga_putc(buf[i]);
}

void kputc(char c) {
    vga_putc(c);
}

void kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (; *fmt; fmt++) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'c':
                    vga_putc((char)va_arg(args, int));
                    break;
                case 's':
                    vga_puts(va_arg(args, const char *));
                    break;
                case 'd':
                case 'i':
                    vga_putint(va_arg(args, int));
                    break;
                case 'x':
                    vga_puthex(va_arg(args, unsigned int));
                    break;
                case '%':
                    vga_putc('%');
                    break;
                default:
                    vga_putc('%');
                    vga_putc(*fmt);
                    break;
            }
        } else {
            vga_putc(*fmt);
        }
    }

    va_end(args);
}

int kstrlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}
