#include "graphics.h"
#include "../console.h"
#include "../mem/heap.h"
#include "../io.h"
#include "vga.h"
#include "../fonts/mono8x8.h"
#include "../drivers/fat32.h"

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

const uint8_t gfx_palette[16][3] = {
    {0x00, 0x00, 0x00}, // 0  black
    {0x00, 0x00, 0xA8}, // 1  dark blue
    {0x00, 0xA8, 0x54}, // 2  dark green
    {0x54, 0xA8, 0xA8}, // 3  cadet blue
    {0xA8, 0x00, 0x54}, // 4  purple
    {0xA8, 0x54, 0xA8}, // 5  plum
    {0xFC, 0xA8, 0x00}, // 6  orange
    {0xC0, 0xC4, 0xC8}, // 7  light grey
    {0x84, 0x88, 0x8C}, // 8  dark grey
    {0x00, 0x54, 0xA8}, // 9  blue
    {0x00, 0xFC, 0x00}, // 10 green
    {0x00, 0xFC, 0xFC}, // 11 cyan
    {0xFC, 0x00, 0x00}, // 12 red
    {0xFC, 0x00, 0xFC}, // 13 magenta
    {0xFC, 0xFC, 0x00}, // 14 yellow
    {0xFC, 0xFC, 0xFC}, // 15 white
};

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

static inline void putpixel_raw(int x, int y, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);

    if (x & 1) {
        backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
    } else {
        backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
    }
}

static inline uint8_t getpixel_raw(int x, int y) {
    if (!backbuffer) return 0;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return 0;

    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
    uint8_t byte = backbuffer[offset];

    if (x & 1) {
        return byte & 0x0F;
    } else {
        return (byte >> 4) & 0x0F;
    }
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

void gfx_hline(int x, int y, int w, uint8_t color) {
    if (!backbuffer) return;
    if (y < 0 || y >= GFX_HEIGHT || w <= 0) return;

    GFX_LOCK();

    int x0 = x;
    int x1 = x + w - 1;

    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_WIDTH) x1 = GFX_WIDTH - 1;

    for (int px = x0; px <= x1; px++) {
        putpixel_raw(px, y, color);
    }

    GFX_UNLOCK();
}

void gfx_vline(int x, int y, int h, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || h <= 0) return;

    GFX_LOCK();

    int y0 = y;
    int y1 = y + h - 1;

    if (y0 < 0) y0 = 0;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;

    for (int py = y0; py <= y1; py++) {
        putpixel_raw(x, py, color);
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

void gfx_floodfill(int x, int y, uint8_t new_color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    GFX_LOCK();

    uint8_t target = getpixel_raw(x, y);
    if (target == new_color) {
        GFX_UNLOCK();
        return;
    }

    size_t max_pixels = (size_t)GFX_WIDTH * (size_t)GFX_HEIGHT;
    uint32_t* stack = (uint32_t*)kmalloc(max_pixels * sizeof(uint32_t));
    if (!stack) {
        GFX_UNLOCK();
        return;
    }

    size_t sp = 0;
    stack[sp++] = ((uint32_t)y << 16) | (uint16_t)(x & 0xFFFF);

    while (sp > 0) {
        uint32_t v = stack[--sp];
        int cy = (int)(v >> 16);
        int cx = (int)(int16_t)(v & 0xFFFF);

        if (cx < 0 || cx >= GFX_WIDTH || cy < 0 || cy >= GFX_HEIGHT)
            continue;

        if (getpixel_raw(cx, cy) != target)
            continue;

        int lx = cx;
        int rx = cx;

        while (lx - 1 >= 0 && getpixel_raw(lx - 1, cy) == target) {
            lx--;
        }
        while (rx + 1 < GFX_WIDTH && getpixel_raw(rx + 1, cy) == target) {
            rx++;
        }

        for (int px = lx; px <= rx; px++) {
            putpixel_raw(px, cy, new_color);
        }

        for (int ny = cy - 1; ny <= cy + 1; ny += 2) {
            if (ny < 0 || ny >= GFX_HEIGHT)
                continue;

            int nx = lx;
            while (nx <= rx) {
                int start = -1;
                while (nx <= rx && getpixel_raw(nx, ny) == target) {
                    if (start < 0) start = nx;
                    nx++;
                }

                if (start >= 0) {
                    int seed_x = (start + nx - 1) / 2;
                    if (sp < max_pixels) {
                        stack[sp++] = ((uint32_t)ny << 16) | (uint16_t)(seed_x & 0xFFFF);
                    }
                }

                nx++;
            }
        }
    }

    kfree(stack);
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

static inline int clamp4(int v) {
    if (v < 0) return 0;
    if (v > 15) return 15;
    return v;
}

static uint8_t calculate_gradient_color(int pos, int denom, 
                                       uint8_t c_start, uint8_t c_end,
                                       int bx, int by) {
    int mix = (16 * pos) / denom;  // 0-16
    
    int threshold = bayer8[by][bx] >> 2;
    
    if (mix > threshold) {
        return c_end;
    } else {
        return c_start;
    }
}

void gfx_fillrect_gradient(int x, int y, int w, int h,
                           uint8_t c_start, uint8_t c_end,
                           int orientation) {
    if (!backbuffer) return;
    if (w <= 0 || h <= 0) return;

    int x0 = x;
    int y0 = y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    if (x0 >= GFX_WIDTH || y0 >= GFX_HEIGHT) return;
    if (x1 < 0 || y1 < 0) return;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_WIDTH)  x1 = GFX_WIDTH - 1;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;

    int eff_w = x1 - x0 + 1;
    int eff_h = y1 - y0 + 1;

    if (eff_w <= 0 || eff_h <= 0) return;

    int denom = (orientation == GRADIENT_V)
                ? ((eff_h > 1) ? (eff_h - 1) : 1)
                : ((eff_w > 1) ? (eff_w - 1) : 1);

    GFX_LOCK();

    for (int py = 0; py < eff_h; py++) {
        int sy = y0 + py;
        for (int px = 0; px < eff_w; px++) {
            int sx = x0 + px;

            int pos = (orientation == GRADIENT_V) ? py : px;
            int bx = sx & 7;
            int by = sy & 7;

            uint8_t final_col = calculate_gradient_color(pos, denom, c_start, c_end, bx, by);
            putpixel_raw(sx, sy, final_col);
        }
    }

    GFX_UNLOCK();
}

void gfx_floodfill_gradient(int x, int y,
                            uint8_t c_start, uint8_t c_end,
                            int orientation) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    GFX_LOCK();

    uint8_t target = getpixel_raw(x, y);

    if (target == c_start || target == c_end) {
        GFX_UNLOCK();
        return;
    }

    uint8_t placeholder = 0xFF;
    for (int i = 0; i < 16; i++) {
        if ((uint8_t)i != target && (uint8_t)i != c_start && (uint8_t)i != c_end) {
            placeholder = (uint8_t)i;
            break;
        }
    }
    if (placeholder == 0xFF) {
        GFX_UNLOCK();
        return;
    }

    size_t max_pixels = (size_t)GFX_WIDTH * (size_t)GFX_HEIGHT;
    uint32_t *stack = (uint32_t*)kmalloc(max_pixels * sizeof(uint32_t));
    if (!stack) {
        GFX_UNLOCK();
        return;
    }

    int min_x = x, max_x = x;
    int min_y = y, max_y = y;

    size_t sp = 0;
    stack[sp++] = ((uint32_t)y << 16) | (uint16_t)(x & 0xFFFF);

    while (sp > 0) {
        uint32_t v = stack[--sp];
        int cy = (int)(v >> 16);
        int cx = (int)(int16_t)(v & 0xFFFF);

        if (cx < 0 || cx >= GFX_WIDTH || cy < 0 || cy >= GFX_HEIGHT)
            continue;

        if (getpixel_raw(cx, cy) != target)
            continue;

        int lx = cx;
        int rx = cx;

        while (lx - 1 >= 0 && getpixel_raw(lx - 1, cy) == target) lx--;
        while (rx + 1 < GFX_WIDTH && getpixel_raw(rx + 1, cy) == target) rx++;

        for (int px = lx; px <= rx; px++)
            putpixel_raw(px, cy, placeholder);

        if (lx < min_x) min_x = lx;
        if (rx > max_x) max_x = rx;
        if (cy < min_y) min_y = cy;
        if (cy > max_y) max_y = cy;

        for (int ny = cy - 1; ny <= cy + 1; ny += 2) {
            if (ny < 0 || ny >= GFX_HEIGHT) continue;

            int nx = lx;
            while (nx <= rx) {
                while (nx <= rx && getpixel_raw(nx, ny) != target) nx++;
                if (nx > rx) break;
                int start = nx;
                while (nx <= rx && getpixel_raw(nx, ny) == target) nx++;
                int seed_x = (start + nx - 1) / 2;
                if (sp < max_pixels)
                    stack[sp++] = ((uint32_t)ny << 16) | (uint16_t)(seed_x & 0xFFFF);
            }
        }
    }

    kfree(stack);

    int eff_w = max_x - min_x + 1;
    int eff_h = max_y - min_y + 1;
    if (eff_w <= 0 || eff_h <= 0) {
        GFX_UNLOCK();
        return;
    }

    int denom = (orientation == GRADIENT_V)
                ? ((eff_h > 1) ? (eff_h - 1) : 1)
                : ((eff_w > 1) ? (eff_w - 1) : 1);

    for (int py = min_y; py <= max_y; py++) {
        for (int px = min_x; px <= max_x; px++) {
            if (getpixel_raw(px, py) != placeholder) continue;

            int pos = (orientation == GRADIENT_V)
                      ? (py - min_y)
                      : (px - min_x);

            int bx = px & 7;
            int by = py & 7;

            uint8_t final_col = calculate_gradient_color(pos, denom, c_start, c_end, bx, by);
            putpixel_raw(px, py, final_col);
        }
    }

    GFX_UNLOCK();
}

int gfx_load_bmp_4bit(const char *path, int dest_x, int dest_y) {
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) return -1;
    
    bmp_header_t header;
    bmp_info_t info;
    
    if (fat32_read(f, &header, sizeof(header)) != sizeof(header)) {
        fat32_close(f);
        return -1;
    }
    
    if (header.type != 0x4D42) { /* "BM" */
        fat32_close(f);
        return -1;
    }
    
    if (fat32_read(f, &info, sizeof(info)) != sizeof(info)) {
        fat32_close(f);
        return -1;
    }
    
    if (info.bpp != 4 || info.compression != 0) {
        fat32_close(f);
        return -1;
    }
    
    int width = info.width;
    int height = info.height > 0 ? info.height : -info.height;
    int bottom_up = info.height > 0;
    
    /* Calculate row size (4-byte aligned) */
    int row_size = ((width + 1) / 2 + 3) & ~3;
    
    /* Seek to pixel data */
    fat32_seek(f, header.offset);
    
    uint8_t *row_buf = kmalloc(row_size);
    if (!row_buf) {
        fat32_close(f);
        return -1;
    }
    
    GFX_LOCK();
    
    for (int y = 0; y < height; y++) {
        if (fat32_read(f, row_buf, row_size) != row_size) break;
        
        int screen_y = bottom_up ? (dest_y + height - 1 - y) : (dest_y + y);
        
        for (int x = 0; x < width; x++) {
            int byte_idx = x / 2;
            uint8_t pixel = (x & 1) ? (row_buf[byte_idx] & 0x0F) : (row_buf[byte_idx] >> 4);
            
            int screen_x = dest_x + x;
            
            if (screen_x >= 0 && screen_x < GFX_WIDTH && 
                screen_y >= 0 && screen_y < GFX_HEIGHT) {
                putpixel_raw(screen_x, screen_y, pixel);
            }
        }
    }
    
    GFX_UNLOCK();
    
    kfree(row_buf);
    fat32_close(f);
    return 0;
}