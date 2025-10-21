#include <stdint.h>

#define VGA_ADDRESS 0xB8000
#define VGA_WIDTH 80
#define VGA_HEIGHT 25

static volatile uint16_t *vga = (uint16_t*)VGA_ADDRESS;
static int cursor_pos = 0;

void vga_putc(char c) {
    if (c == '\n') {
        int col = cursor_pos % VGA_WIDTH;
        cursor_pos += (VGA_WIDTH - col);
        return;
    }
    vga[cursor_pos++] = (uint16_t)((0x07 << 8) | (uint8_t)c);
    if (cursor_pos >= VGA_WIDTH*VGA_HEIGHT) {
        cursor_pos = 0;
    }
}

void putchar(char c) {
    vga_putc(c);
}

void vga_clear() {
    for (int y = 0; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga[y * VGA_WIDTH + x] = (uint16_t)(' ' | (0x07 << 8)); // white on black
        }
    }
}
