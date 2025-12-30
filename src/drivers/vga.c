#include <stdint.h>
#include "../console.h"
#include "../irq/io.h"
#include "../fonts/mono8x16.h"

#define VGA_ADDRESS 0xB8000u
#define VGA_WIDTH   80
#define VGA_HEIGHT  25

static volatile uint16_t* const VGA = (volatile uint16_t*)(uintptr_t)VGA_ADDRESS;
static uint8_t vga_color = 0x07;
static int cx = 0, cy = 0;

static uint8_t saved_sequencer[5];
static uint8_t saved_crtc[25];
static uint8_t saved_graphics[9];
static uint8_t saved_attribute[21];
static uint8_t saved_misc;
static int state_saved = 0;

static const uint8_t default_palette[16][3] = {
    {0x00, 0x00, 0x00},  /* 0: Black */
    {0x00, 0x00, 0xAA},  /* 1: Blue */
    {0x00, 0xAA, 0x00},  /* 2: Green */
    {0x00, 0xAA, 0xAA},  /* 3: Cyan */
    {0xAA, 0x00, 0x00},  /* 4: Red */
    {0xAA, 0x00, 0xAA},  /* 5: Magenta */
    {0xAA, 0x55, 0x00},  /* 6: Brown */
    {0xAA, 0xAA, 0xAA},  /* 7: Light Gray */
    {0x55, 0x55, 0x55},  /* 8: Dark Gray */
    {0x55, 0x55, 0xFF},  /* 9: Light Blue */
    {0x55, 0xFF, 0x55},  /* 10: Light Green */
    {0x55, 0xFF, 0xFF},  /* 11: Light Cyan */
    {0xFF, 0x55, 0x55},  /* 12: Light Red */
    {0xFF, 0x55, 0xFF},  /* 13: Light Magenta */
    {0xFF, 0xFF, 0x55},  /* 14: Yellow */
    {0xFF, 0xFF, 0xFF},  /* 15: White */
};

void vga_save_state(void) {
    saved_misc = inb(0x3CC);
    
    for (uint8_t i = 0; i < 5; i++) {
        outb(0x3C4, i);
        saved_sequencer[i] = inb(0x3C5);
    }
    
    for (uint8_t i = 0; i < 25; i++) {
        outb(0x3D4, i);
        saved_crtc[i] = inb(0x3D5);
    }
    
    for (uint8_t i = 0; i < 9; i++) {
        outb(0x3CE, i);
        saved_graphics[i] = inb(0x3CF);
    }
    
    (void)inb(0x3DA);
    for (uint8_t i = 0; i < 21; i++) {
        outb(0x3C0, i);
        saved_attribute[i] = inb(0x3C1);
    }
    outb(0x3C0, 0x20);
    
    state_saved = 1;
}

void vga_restore_state(void) {
    if (!state_saved) return;
    
    outb(0x3C2, saved_misc);
    
    for (uint8_t i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, saved_sequencer[i]);
    }
    
    outb(0x3D4, 0x03);
    outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);
    
    for (uint8_t i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, saved_crtc[i]);
    }
    
    for (uint8_t i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, saved_graphics[i]);
    }
    
    (void)inb(0x3DA);
    for (uint8_t i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, saved_attribute[i]);
    }
    outb(0x3C0, 0x20);
}

static void vga_load_font(void) {
    uint8_t seq2, seq4, gc4, gc5, gc6;
    
    outb(0x3C4, 0x02);
    seq2 = inb(0x3C5);
    
    outb(0x3C4, 0x04);
    seq4 = inb(0x3C5);
    
    outb(0x3CE, 0x04);
    gc4 = inb(0x3CF);
    
    outb(0x3CE, 0x05);
    gc5 = inb(0x3CF);
    
    outb(0x3CE, 0x06);
    gc6 = inb(0x3CF);
    
    /* Font injection */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x04);  /* Plane 2 */
    
    outb(0x3C4, 0x04);
    outb(0x3C5, 0x07);  /* Sequential addressing */
    
    outb(0x3CE, 0x04);
    outb(0x3CF, 0x02);  /* Read plane 2 */
    
    outb(0x3CE, 0x05);
    outb(0x3CF, 0x00);  /* Write mode 0 */
    
    outb(0x3CE, 0x06);
    outb(0x3CF, 0x00);  /* Map A0000-BFFFF */
    
    /* Load font into VGA memory (plane 2) */
    volatile uint8_t* font_mem = (volatile uint8_t*)0xA0000;
    
    for (int ch = 0; ch < 256; ch++) {
        for (int row = 0; row < 16; row++) {
            font_mem[ch * 32 + row] = font_8x16[ch][row];
        }
    }
    
    outb(0x3C4, 0x02);
    outb(0x3C5, seq2);
    
    outb(0x3C4, 0x04);
    outb(0x3C5, seq4);
    
    outb(0x3CE, 0x04);
    outb(0x3CF, gc4);
    
    outb(0x3CE, 0x05);
    outb(0x3CF, gc5);
    
    outb(0x3CE, 0x06);
    outb(0x3CF, gc6);
}

static inline void clamp_cursor(void) {
    if (cx < 0) cx = 0;
    if (cy < 0) cy = 0;
    if (cx >= VGA_WIDTH)  cx = VGA_WIDTH - 1;
    if (cy >= VGA_HEIGHT) cy = VGA_HEIGHT - 1;
}

static inline void move_hw_cursor(void) {
    clamp_cursor();
    uint16_t pos = (uint16_t)(cy * VGA_WIDTH + cx);
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)(pos >> 8));
}

static inline void put_at(char c, int x, int y) {
    if ((unsigned)x < VGA_WIDTH && (unsigned)y < VGA_HEIGHT) {
        VGA[y * VGA_WIDTH + x] = (uint16_t)c | ((uint16_t)vga_color << 8);
    }
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
    if (cy < VGA_HEIGHT)
        return;

    for (int y = 1; y < VGA_HEIGHT; ++y) {
        for (int x = 0; x < VGA_WIDTH; ++x) {
            VGA[(y - 1) * VGA_WIDTH + x] = VGA[y * VGA_WIDTH + x];
        }
    }

    uint16_t blank = (uint16_t)(' ') | ((uint16_t)vga_color << 8);
    int last = VGA_HEIGHT - 1;
    for (int x = 0; x < VGA_WIDTH; ++x)
        VGA[last * VGA_WIDTH + x] = blank;

    cy = VGA_HEIGHT - 1;
    if (cx < 0) cx = 0;
    if (cx >= VGA_WIDTH) cx = VGA_WIDTH - 1;
}

static void vga_putc_internal(char c) {
    switch ((unsigned char)c) {
        case '\r':
            cx = 0;
            break;

        case '\n':
            cx = 0;
            cy++;
            break;

        case '\t': {
            int next = (cx + 4) & ~3;
            for (; cx < next && cx < VGA_WIDTH; ++cx)
                put_at(' ', cx, cy);
        } break;

        case '\b':
            if (cx > 0) {
                cx--;
                put_at(' ', cx, cy);
            }
            break;

        default:
            put_at(c, cx, cy);
            if (++cx >= VGA_WIDTH) {
                cx = 0;
                cy++;
            }
            break;
    }
}

static size_t vga_write(const char* s, size_t n, void* ctx) {
    (void)ctx;
    for (size_t i = 0; i < n; ++i) {
        vga_putc_internal(s[i]);
        if (cy >= VGA_HEIGHT)
            scroll_if_needed();
    }
    move_hw_cursor();
    return n;
}

void vga_clear(void) {
    /* Clear with CURRENT vga_color on purpose */
    uint16_t blank = (uint16_t)(' ') | ((uint16_t)vga_color << 8);
    for (int y = 0; y < VGA_HEIGHT; ++y) {
        for (int x = 0; x < VGA_WIDTH; ++x) {
            VGA[y * VGA_WIDTH + x] = blank;
        }
    }
    cx = 0;
    cy = 0;
    move_hw_cursor();
}

static const console_t VGA_CONSOLE = {
    .write = vga_write,
    .ctx   = 0
};

void vga_use_as_console(void) {
    console_set(&VGA_CONSOLE);
    enable_cursor(12, 12);
    vga_clear();
}

static void vga_load_palette(const uint8_t palette[16][3]) {
    for (int i = 0; i < 16; i++) {
        outb(0x3C8, i);
        outb(0x3C9, palette[i][0] >> 2);  /* R (6-bit) */
        outb(0x3C9, palette[i][1] >> 2);  /* G (6-bit) */
        outb(0x3C9, palette[i][2] >> 2);  /* B (6-bit) */
    }
}

void vga_reset_palette(void) {
    vga_load_palette(default_palette);
}

void vga_reset_textmode(void) {
    vga_load_font();
    vga_reset_palette();
    (void)inb(0x3DA);
    for (int i = 0; i < 16; ++i) {
        outb(0x3C0, i);
        outb(0x3C0, i);
    }
    outb(0x3C0, 0x20);
    vga_clear();
}

void vga_set_color(uint8_t background, uint8_t foreground) {
    vga_color = (foreground & 0x0F) | ((background & 0x07) << 4); /* or 0x0F */
}

void vga_set_cursor(int x, int y) {
    if (x >= 0 && x < VGA_WIDTH) cx = x;
    if (y >= 0 && y < VGA_HEIGHT) cy = y;
    move_hw_cursor();
}

void vga_get_cursor(int *x, int *y) {
    if (x) *x = cx;
    if (y) *y = cy;
}

void vga_write_regs(const uint8_t* regs) {
    outb(0x3C2, *regs++);
    
    for (uint8_t i = 0; i < 5; i++) {
        outb(0x3C4, i);
        outb(0x3C5, *regs++);
    }
    
    outb(0x3D4, 0x03);
    outb(0x3D5, inb(0x3D5) | 0x80);
    outb(0x3D4, 0x11);
    outb(0x3D5, inb(0x3D5) & 0x7F);
    
    for (uint8_t i = 0; i < 25; i++) {
        outb(0x3D4, i);
        outb(0x3D5, *regs++);
    }
    
    for (uint8_t i = 0; i < 9; i++) {
        outb(0x3CE, i);
        outb(0x3CF, *regs++);
    }
    
    (void)inb(0x3DA);
    for (uint8_t i = 0; i < 21; i++) {
        outb(0x3C0, i);
        outb(0x3C0, *regs++);
    }
    outb(0x3C0, 0x20);
}