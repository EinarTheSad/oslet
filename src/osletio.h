#ifndef OSLET_IO_H
#define OSLET_IO_H

#include <stdint.h>

// Core VGA I/O
void vga_putc(char c);
void vga_clear();
void vga_setcolor(uint8_t color);
void vga_puts(const char* str);

// Kernel-level output wrappers
void kputs(const char* str);
void kputc(char c);

// String helpers
int kstrlen(const char* s);
void kitoa(int value, char* buffer, int base);

#endif
