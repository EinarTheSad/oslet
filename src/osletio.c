#include <stdarg.h>
#include <stdint.h>
#include "osletio.h"

/* Text-mode VGA */
#define VGA_ADDRESS 0xB8000u
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

/* MMIO: volatile to stop the compiler from caching writes */
static volatile uint16_t* const VGA_BUFFER = (volatile uint16_t*) (uintptr_t)VGA_ADDRESS;

/* Cursor and color state */
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t color = 0x07;

/* Port I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static inline void move_cursor_hw(void);
static inline void scroll(void);
static inline void put_at(char c, int x, int y);

/* Update the hardware cursor (0x3D4/0x3D5) */
static inline void move_cursor_hw(void) {
    if (cursor_x < 0) cursor_x = 0;
    if (cursor_y < 0) cursor_y = 0;
    if (cursor_x >= VGA_WIDTH)  cursor_x = VGA_WIDTH - 1;
    if (cursor_y >= VGA_HEIGHT) cursor_y = VGA_HEIGHT - 1;

    uint16_t pos = (uint16_t)(cursor_y * VGA_WIDTH + cursor_x);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/* Scroll up by one line when the cursor goes off the bottom */
static inline void scroll(void) {
    if (cursor_y < VGA_HEIGHT) return;

    /* Move lines 1..24 to 0..23 */
    for (int y = 1; y < VGA_HEIGHT; ++y) {
        for (int x = 0; x < VGA_WIDTH; ++x) {
            VGA_BUFFER[(y - 1) * VGA_WIDTH + x] = VGA_BUFFER[y * VGA_WIDTH + x];
        }
    }
    /* Clear last line */
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)color << 8);
    for (int x = 0; x < VGA_WIDTH; ++x) {
        VGA_BUFFER[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
    cursor_y = VGA_HEIGHT - 1;
}

/* Write a character at a specific location */
static inline void put_at(char c, int x, int y) {
    VGA_BUFFER[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)color << 8);
}

/* Public API */

void vga_clear(void) {
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)color << 8);
    for (int y = 0; y < VGA_HEIGHT; ++y) {
        for (int x = 0; x < VGA_WIDTH; ++x) {
            VGA_BUFFER[y * VGA_WIDTH + x] = blank;
        }
    }
    cursor_x = 0;
    cursor_y = 0;
    move_cursor_hw();
}

void vga_putc(char c) {
    switch ((unsigned char)c) {
        case '\r':
            cursor_x = 0;
            break;
        case '\n':
            cursor_x = 0;
            cursor_y++;
            break;
        case '\t': {
            int next = (cursor_x + 4) & ~3;
            while (cursor_x < next) {
                put_at(' ', cursor_x, cursor_y);
                cursor_x++;
            }
            break;
        }
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                put_at(' ', cursor_x, cursor_y);
            }
            break;
        default:
            put_at(c, cursor_x, cursor_y);
            cursor_x++;
            if (cursor_x >= VGA_WIDTH) {
                cursor_x = 0;
                cursor_y++;
            }
            break;
    }

    if (cursor_y >= VGA_HEIGHT) {
        scroll();
    }
    move_cursor_hw();
}

void vga_puts(const char* str) {
    if (!str) return;
    while (*str) vga_putc(*str++);
}

void vga_putint(int n) {
    unsigned int u;
    if (n < 0) {
        vga_putc('-');
        /* cast to long to avoid UB on INT_MIN, then negate and cast back */
        u = (unsigned int)(-(long)n);
    } else {
        u = (unsigned int)n;
    }

    char buf[16];
    int i = 0;
    do {
        buf[i++] = (char)('0' + (u % 10));
        u /= 10;
    } while (u && i < (int)sizeof(buf));

    while (i--) vga_putc(buf[i]);
}

void vga_puthex(unsigned int n) {
    vga_puts("0x");

    if (n == 0) {
        vga_putc('0');
        return;
    }
    
    int shift = 28; /* highest nibble in 32-bit */
    while (((n >> shift) & 0xF) == 0 && shift > 0)
        shift -=4;
    /* skip leading zeroes */
    for (; shift >= 0; shift -= 4) {
        uint8_t digit = (n >> shift) & 0xF;
        vga_putc(digit < 10 ? '0' + digit : 'A' + (digit - 10));
    }
}

void kputc(char c) {
    vga_putc(c);
}

/* Tiny printf: supports %c %s %d %u %x %p %% \n */
void kprintf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);

    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') {
            vga_putc(*p);
            continue;
        }

        ++p;
        switch (*p) {
            case '%': vga_putc('%'); break;
            case 'c': {
                int ch = va_arg(ap, int);
                vga_putc((char)ch);
            } break;
            case 's': {
                const char* s = va_arg(ap, const char*);
                vga_puts(s ? s : "(null)");
            } break;
            case 'd': {
                int val = va_arg(ap, int);
                vga_putint(val);
            } break;
            case 'u': {
                unsigned int u = va_arg(ap, unsigned int);
                char buf[16]; int i = 0;
                do {
                    buf[i++] = (char)('0' + (u % 10));
                    u /= 10;
                } while (u && i < (int)sizeof(buf));
                while (i--) vga_putc(buf[i]);
            } break;
            case 'x': /* empty */
            case 'p': {
                unsigned int x = va_arg(ap, unsigned int);
                vga_puthex(x);
            } break;
            default:
                /* Unknown specifier: print it literally */
                vga_putc('%');
                vga_putc(*p);
                break;
        }
    }

    va_end(ap);
}

int kstrlen(const char* s) {
    if (!s) return 0;
    int n = 0;
    while (*s++) n++;
    return n;
}
