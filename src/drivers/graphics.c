#include "graphics.h"
#include "../console.h"
#include "../mem/heap.h"
#include "../io.h"
#include "vga.h"
#include "../fonts/mono8x8.h"

static uint8_t* backbuffer = NULL;
static int graphics_active = 0;

/* Scheduler protection */
#define GFX_LOCK() __asm__ volatile("cli")
#define GFX_UNLOCK() __asm__ volatile("sti")

static const uint8_t mode_640x480x16[] = {
    0xE3,
    0x03, 0x01, 0x0F, 0x00, 0x06,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x01, 0x00, 0x0F, 0x00, 0x00
};

static const uint8_t mode_80x25_text[] = {
    0x67,
    0x03, 0x00, 0x03, 0x00, 0x02,
    0x5F, 0x4F, 0x50, 0x82, 0x55, 0x81, 0xBF, 0x1F,
    0x00, 0x4F, 0x0D, 0x0E, 0x00, 0x00, 0x00, 0x00,
    0x9C, 0x8E, 0x8F, 0x28, 0x1F, 0x96, 0xB9, 0xA3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x0E, 0x00,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x14, 0x07,
    0x38, 0x39, 0x3A, 0x3B, 0x3C, 0x3D, 0x3E, 0x3F,
    0x0C, 0x00, 0x0F, 0x08, 0x00
};

static const uint8_t gfx_palette[16][3] = {
    {0x00, 0x00, 0x00},
    {0x00, 0x00, 0x80},
    {0x00, 0x80, 0x00},
    {0x00, 0x80, 0x80},
    {0x80, 0x00, 0x00},
    {0x80, 0x00, 0x80},
    {0x80, 0x80, 0x00},
    {0xC0, 0xC0, 0xC0},
    {0x80, 0x80, 0x80},
    {0x00, 0x00, 0xFF},
    {0x00, 0xFF, 0x00},
    {0x00, 0xFF, 0xFF},
    {0xFF, 0x00, 0x00},
    {0xFF, 0x00, 0xFF},
    {0xFF, 0xFF, 0x00},
    {0xFF, 0xFF, 0xFF},
};

static void vga_write_regs(const uint8_t* regs) {
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

static void wait_vretrace(void) {
    while (inb(0x3DA) & 0x08);
    while (!(inb(0x3DA) & 0x08));
}

void gfx_load_palette(void) {
    for (int i = 0; i < 16; i++) {
        outb(0x3C8, i);
        outb(0x3C9, gfx_palette[i][0] >> 2);
        outb(0x3C9, gfx_palette[i][1] >> 2);
        outb(0x3C9, gfx_palette[i][2] >> 2);
    }
}

void gfx_init(void) {
    if (!backbuffer) {
        backbuffer = kmalloc(GFX_BUFFER_SIZE);
        if (!backbuffer) {
            printf("Graphics: Failed to allocate backbuffer\n");
            return;
        }
    }
    memset_s(backbuffer, 0, GFX_BUFFER_SIZE);
}

void gfx_enter_mode(void) {
    vga_save_state();
    vga_write_regs(mode_640x480x16);
    gfx_load_palette();
    graphics_active = 1;
    if (!backbuffer) gfx_init();
}

void gfx_exit_mode(void) {
    graphics_active = 0;
    vga_write_regs(mode_80x25_text);
    vga_reset_textmode();
    vga_restore_state();
}

int gfx_is_active(void) {
    return graphics_active;
}

void gfx_clear(uint8_t color) {
    if (!backbuffer) return;
    
    GFX_LOCK();
    uint8_t pattern = (color & 0x0F) | ((color & 0x0F) << 4);
    memset_s(backbuffer, pattern, GFX_BUFFER_SIZE);
    GFX_UNLOCK();
}

void gfx_swap_buffers(void) {
    if (!backbuffer) return;
    
    GFX_LOCK();
    wait_vretrace();
    
    volatile uint8_t* vram = GFX_VRAM;
    
    for (int y = 0; y < GFX_HEIGHT; y++) {
        for (int x = 0; x < GFX_WIDTH; x += 8) {
            uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
            
            uint8_t pixels[8];
            for (int i = 0; i < 4; i++) {
                uint8_t byte = backbuffer[offset + i];
                pixels[i * 2] = (byte >> 4) & 0x0F;
                pixels[i * 2 + 1] = byte & 0x0F;
            }
            
            uint32_t vram_offset = y * 80 + (x / 8);
            
            for (uint8_t plane = 0; plane < 4; plane++) {
                outb(0x3C4, 0x02);
                outb(0x3C5, 1 << plane);
                
                uint8_t plane_byte = 0;
                for (int bit = 0; bit < 8; bit++) {
                    if (pixels[bit] & (1 << plane)) {
                        plane_byte |= (0x80 >> bit);
                    }
                }
                
                vram[vram_offset] = plane_byte;
            }
        }
    }

    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F);
    GFX_UNLOCK();
}

void gfx_putpixel(int x, int y, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;
    
    GFX_LOCK();
    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
    
    if (x & 1) {
        backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
    } else {
        backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
    }
    GFX_UNLOCK();
}

uint8_t gfx_getpixel(int x, int y) {
    if (!backbuffer) return 0;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return 0;
    
    GFX_LOCK();
    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
    uint8_t result;
    
    if (x & 1) {
        result = backbuffer[offset] & 0x0F;
    } else {
        result = (backbuffer[offset] >> 4) & 0x0F;
    }
    GFX_UNLOCK();
    
    return result;
}

void gfx_line(int x0, int y0, int x1, int y1, uint8_t color) {
    GFX_LOCK();
    
    int dx = x1 - x0;
    int dy = y1 - y0;
    
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    while (1) {
        if (x0 >= 0 && x0 < GFX_WIDTH && y0 >= 0 && y0 < GFX_HEIGHT) {
            uint32_t offset = y0 * (GFX_WIDTH / 2) + (x0 / 2);
            if (x0 & 1) {
                backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            } else {
                backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
            }
        }
        
        if (x0 == x1 && y0 == y1) break;
        
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
    
    GFX_UNLOCK();
}

void gfx_rect(int x, int y, int w, int h, uint8_t color) {
    GFX_LOCK();
    gfx_line(x, y, x + w - 1, y, color);
    gfx_line(x + w - 1, y, x + w - 1, y + h - 1, color);
    gfx_line(x + w - 1, y + h - 1, x, y + h - 1, color);
    gfx_line(x, y + h - 1, x, y, color);
    GFX_UNLOCK();
}

void gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
    if (!backbuffer) return;
    
    GFX_LOCK();
    for (int dy = 0; dy < h; dy++) {
        for (int dx = 0; dx < w; dx++) {
            int px = x + dx;
            int py = y + dy;
            if (px >= 0 && px < GFX_WIDTH && py >= 0 && py < GFX_HEIGHT) {
                uint32_t offset = py * (GFX_WIDTH / 2) + (px / 2);
                if (px & 1) {
                    backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
                } else {
                    backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
                }
            }
        }
    }
    GFX_UNLOCK();
}

void gfx_circle(int cx, int cy, int r, uint8_t color) {
    GFX_LOCK();
    
    int x = r;
    int y = 0;
    int err = 0;
    
    while (x >= y) {
        if (cx + x >= 0 && cx + x < GFX_WIDTH && cy + y >= 0 && cy + y < GFX_HEIGHT) {
            uint32_t offset = (cy + y) * (GFX_WIDTH / 2) + ((cx + x) / 2);
            if ((cx + x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + y >= 0 && cx + y < GFX_WIDTH && cy + x >= 0 && cy + x < GFX_HEIGHT) {
            uint32_t offset = (cy + x) * (GFX_WIDTH / 2) + ((cx + y) / 2);
            if ((cx + y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - y >= 0 && cx - y < GFX_WIDTH && cy + x >= 0 && cy + x < GFX_HEIGHT) {
            uint32_t offset = (cy + x) * (GFX_WIDTH / 2) + ((cx - y) / 2);
            if ((cx - y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - x >= 0 && cx - x < GFX_WIDTH && cy + y >= 0 && cy + y < GFX_HEIGHT) {
            uint32_t offset = (cy + y) * (GFX_WIDTH / 2) + ((cx - x) / 2);
            if ((cx - x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - x >= 0 && cx - x < GFX_WIDTH && cy - y >= 0 && cy - y < GFX_HEIGHT) {
            uint32_t offset = (cy - y) * (GFX_WIDTH / 2) + ((cx - x) / 2);
            if ((cx - x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - y >= 0 && cx - y < GFX_WIDTH && cy - x >= 0 && cy - x < GFX_HEIGHT) {
            uint32_t offset = (cy - x) * (GFX_WIDTH / 2) + ((cx - y) / 2);
            if ((cx - y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + y >= 0 && cx + y < GFX_WIDTH && cy - x >= 0 && cy - x < GFX_HEIGHT) {
            uint32_t offset = (cy - x) * (GFX_WIDTH / 2) + ((cx + y) / 2);
            if ((cx + y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + x >= 0 && cx + x < GFX_WIDTH && cy - y >= 0 && cy - y < GFX_HEIGHT) {
            uint32_t offset = (cy - y) * (GFX_WIDTH / 2) + ((cx + x) / 2);
            if ((cx + x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (err <= 0) {
            y += 1;
            err += 2*y + 1;
        }
        
        if (err > 0) {
            x -= 1;
            err -= 2*x + 1;
        }
    }
    
    GFX_UNLOCK();
}

void gfx_putchar(int x, int y, char c, uint8_t fg) {  
    const uint8_t* glyph = font8x8[(unsigned char)c];
    
    GFX_LOCK();
    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; col++) {
            int px = x + col;
            int py = y + row;
            
            if (px >= 0 && px < GFX_WIDTH && py >= 0 && py < GFX_HEIGHT) {
                /* Transparent background: only draw foreground pixels */
                if (bits & (1 << col)) {
                    uint32_t offset = py * (GFX_WIDTH / 2) + (px / 2);
                    if (px & 1) {
                        backbuffer[offset] = (backbuffer[offset] & 0xF0) | (fg & 0x0F);
                    } else {
                        backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((fg & 0x0F) << 4);
                    }
                }
            }
        }
    }
    GFX_UNLOCK();
}

void gfx_print(int x, int y, const char* str, uint8_t fg) {
    int cx = x;
    int cy = y;
    
    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy += 8;
        } else {
            gfx_putchar(cx, cy, *str, fg);
            cx += 8;
            if (cx >= GFX_WIDTH) {
                cx = x;
                cy += 8;
            }
        }
        str++;
    }
}

uint8_t* gfx_get_backbuffer(void) {
    return backbuffer;
}