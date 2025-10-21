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

void kputs(const char* str) {
    vga_puts(str);
}

void kputc(char c) {
    vga_putc(c);
}

int kstrlen(const char* s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

void kitoa(int value, char* buffer, int base) {
    char digits[] = "0123456789ABCDEF";
    char temp[32];
    int i = 0;
    if (value == 0) {
        buffer[0] = '0';
        buffer[1] = '\0';
        return;
    }
    while (value > 0 && i < 31) {
        temp[i++] = digits[value % base];
        value /= base;
    }
    int j = 0;
    while (i--) buffer[j++] = temp[i];
    buffer[j] = '\0';
}
