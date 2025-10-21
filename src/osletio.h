#ifndef OSLET_IO_H
#define OSLET_IO_H

#include <stdint.h>

// Core VGA I/O
void vga_clear();
void vga_puts(const char* str);
void vga_putc(char c);
void vga_putint(int n);
void vga_puthex(unsigned int n);

// Kernel-level output wrappers
void kputc(char c);
void kprintf(const char* fmt, ...);

// String helpers
int kstrlen(const char* s);

#endif
