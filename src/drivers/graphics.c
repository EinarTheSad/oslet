#include "graphics.h"
#include "../console.h"
#include "../mem/heap.h"
#include "../irq/io.h"
#include "vga.h"
#include "../drivers/fat32.h"

static uint8_t* backbuffer = NULL;
static int graphics_active = 0;
static uint8_t* frontbuffer = NULL; /* cache of what's currently in VRAM (packed 4bpp) */

/* Precomputed table: for each plane (0-3) and each packed byte value (0-255),
   contains the two bits (high,low) for that plane as a 2-bit value. This lets
   us build the output plane byte for 8 pixels by four table lookups and shifts.
*/
static uint8_t plane_pair_table[4][256];
static int plane_table_init = 0;

/* Scheduler protection */
#define GFX_LOCK() __asm__ volatile("cli")
#define GFX_UNLOCK() __asm__ volatile("sti")

static volatile int dirty_x0 = GFX_WIDTH, dirty_y0 = GFX_HEIGHT;
static volatile int dirty_x1 = -1, dirty_y1 = -1;
static volatile int full_redraw = 1;

static inline void mark_dirty(int x, int y, int w, int h) {
    int x1 = x + w - 1;
    int y1 = y + h - 1;

    /* Avoid disabling interrupts for every pixel; slight races are acceptable */
    if (x < dirty_x0) dirty_x0 = x;
    if (y < dirty_y0) dirty_y0 = y;
    if (x1 > dirty_x1) dirty_x1 = x1;
    if (y1 > dirty_y1) dirty_y1 = y1;
}

static inline void reset_dirty(void) {
    dirty_x0 = GFX_WIDTH;
    dirty_y0 = GFX_HEIGHT;
    dirty_x1 = -1;
    dirty_y1 = -1;
    full_redraw = 0;
}

static const uint8_t mode_640x480x16[] = {
    0xE3,
    0x03, 0x01, 0x0F, 0x00, 0x06,
    0x5F, 0x4F, 0x50, 0x82, 0x54, 0x80, 0x0B, 0x3E,
    0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xEA, 0x8C, 0xDF, 0x28, 0x00, 0xE7, 0x04, 0xE3,
    0xFF,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x0F,
    0xFF,
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
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

const uint8_t gfx_palette[16][3] = { // osLET custom
    {0x00, 0x00, 0x00}, // 0  black
    {0x1E, 0x1E, 0x64}, // 1  dark blue
    {0x34, 0x65, 0x24}, // 2  dark green
    {0x40, 0x95, 0xAA}, // 3  cyan
    {0x64, 0x3C, 0x18}, // 4  brown
    {0x46, 0x23, 0x37}, // 5  dark purple
    {0x64, 0x64, 0x18}, // 6  olive
    {0xA0, 0xA0, 0xA0}, // 7  grey
    {0x55, 0x55, 0x55}, // 8  dark grey
    {0x59, 0x7D, 0xCE}, // 9  blue
    {0x6D, 0xAA, 0x2C}, // 10 green
    {0x6E, 0xCE, 0xD8}, // 11 light cyan
    {0xD0, 0x46, 0x48}, // 12 red
    {0xD2, 0xAA, 0x99}, // 13 peach
    {0xE6, 0xDC, 0x42}, // 14 yellow
    {0xFF, 0xFF, 0xFF}, // 15 white
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

    /* Initialize lookup table once */
    if (!plane_table_init) {
        for (int p = 0; p < 4; p++) {
            for (int b = 0; b < 256; b++) {
                uint8_t high = (uint8_t)((b >> 4) & 0x0F);
                uint8_t low  = (uint8_t)(b & 0x0F);
                uint8_t pair = (uint8_t)(((high >> p) & 1) << 1) | (uint8_t)((low >> p) & 1);
                plane_pair_table[p][b] = pair;
            }
        }
        plane_table_init = 1;
    }

    /* Allocate frontbuffer cache */
    if (!frontbuffer) {
        frontbuffer = kmalloc(GFX_BUFFER_SIZE);
        if (frontbuffer) memset_s(frontbuffer, 0, GFX_BUFFER_SIZE);
    }
}

void gfx_enter_mode(void) {
    vga_save_state();
    vga_write_regs(mode_640x480x16);
    gfx_load_palette();
    /* Clear VGA video memory so BIOS artifacts disappear immediately. Write
       a zero byte across the visible scanlines with all planes enabled so
       the framebuffer is fully cleared. */
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F); /* enable all planes for writing */
    {
        int vram_bytes = (GFX_WIDTH / 8) * GFX_HEIGHT; /* 80*480 = 38400 */
        volatile uint8_t *v = GFX_VRAM;
        for (int i = 0; i < vram_bytes; i++) v[i] = 0;
    }
    graphics_active = 1;
    if (!backbuffer) gfx_init();
    /* Ensure frontbuffer cache matches cleared VRAM */
    if (frontbuffer) memset_s(frontbuffer, 0, GFX_BUFFER_SIZE);
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
    full_redraw = 1;
    GFX_UNLOCK();
}

void gfx_swap_buffers(void) {
    if (!backbuffer) return;
    
    GFX_LOCK();
    wait_vretrace();
    
    volatile uint8_t* vram = GFX_VRAM;
    
    int y0, y1, x0_aligned, x1_aligned;
    
    if (full_redraw) {
        y0 = 0;
        y1 = GFX_HEIGHT - 1;
        x0_aligned = 0;
        x1_aligned = GFX_WIDTH - 1;
    } else {
        if (dirty_x1 < dirty_x0 || dirty_y1 < dirty_y0) {
            GFX_UNLOCK();
            return;
        }
        
        y0 = dirty_y0;
        y1 = dirty_y1;
        x0_aligned = dirty_x0 & ~7;
        x1_aligned = (dirty_x1 + 7) & ~7;
        
        if (x1_aligned > GFX_WIDTH) x1_aligned = GFX_WIDTH;
    }
    
    /* Write per-scanline and finish all planes for that scanline before moving to the next one. */
    for (int y = y0; y <= y1; y++) {
        uint32_t bb_row = y * (GFX_WIDTH / 2);
        uint32_t vram_row = y * 80;

        for (int x = x0_aligned; x < x1_aligned; x += 8) {
            uint32_t bb_offset = bb_row + (x / 2);
            uint8_t b0 = backbuffer[bb_offset];
            uint8_t b1 = backbuffer[bb_offset + 1];
            uint8_t b2 = backbuffer[bb_offset + 2];
            uint8_t b3 = backbuffer[bb_offset + 3];

            /* Skip writing if frontbuffer already matches (and not full redraw) */
            if (!full_redraw && frontbuffer) {
                if (frontbuffer[bb_offset] == b0 && frontbuffer[bb_offset + 1] == b1
                    && frontbuffer[bb_offset + 2] == b2 && frontbuffer[bb_offset + 3] == b3) {
                    continue;
                }
            }

            for (uint8_t plane = 0; plane < 4; plane++) {
                outb(0x3C4, 0x02);
                outb(0x3C5, 1 << plane);

                uint8_t plane_byte = (uint8_t)(plane_pair_table[plane][b0] << 6)
                                   | (uint8_t)(plane_pair_table[plane][b1] << 4)
                                   | (uint8_t)(plane_pair_table[plane][b2] << 2)
                                   | (uint8_t)(plane_pair_table[plane][b3] << 0);

                vram[vram_row + (x / 8)] = plane_byte;
            }

            /* Update frontbuffer cache */
            if (frontbuffer) {
                frontbuffer[bb_offset] = b0;
                frontbuffer[bb_offset + 1] = b1;
                frontbuffer[bb_offset + 2] = b2;
                frontbuffer[bb_offset + 3] = b3;
            }
        }
    }
    
    outb(0x3C4, 0x02);
    outb(0x3C5, 0x0F);
    
    reset_dirty();
    GFX_UNLOCK();
}

void gfx_putpixel(int x, int y, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;
    
    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
    
    if (x & 1) {
        backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
    } else {
        backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
    }
    
    mark_dirty(x, y, 1, 1);
}

uint8_t gfx_getpixel(int x, int y) {
    if (!backbuffer) return 0;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return 0;
    uint32_t offset = y * (GFX_WIDTH / 2) + (x / 2);
    if (x & 1) {
        return backbuffer[offset] & 0x0F;
    } else {
        return (backbuffer[offset] >> 4) & 0x0F;
    }
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
    int dx = x1 - x0;
    int dy = y1 - y0;
    
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    
    int min_x = x0 < x1 ? x0 : x1;
    int max_x = x0 > x1 ? x0 : x1;
    int min_y = y0 < y1 ? y0 : y1;
    int max_y = y0 > y1 ? y0 : y1;
    
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
    
    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

void gfx_hline(int x, int y, int w, uint8_t color) {
    if (!backbuffer) return;
    if (y < 0 || y >= GFX_HEIGHT || w <= 0) return;

    int x0 = x;
    int x1 = x + w - 1;

    if (x0 < 0) x0 = 0;
    if (x1 >= GFX_WIDTH) x1 = GFX_WIDTH - 1;
    
    if (x0 > x1) return;

    uint32_t row_offset = y * (GFX_WIDTH / 2);

    /* Fast path: write two pixels at a time using full byte writes where possible */
    uint8_t pattern = (color & 0x0F) | ((color & 0x0F) << 4);
    int px = x0;
    /* Handle leading odd pixel */
    if (px & 1) {
        uint32_t offset = row_offset + (px / 2);
        backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
        px++;
    }

    /* Handle full bytes */
    while (px + 1 <= x1) {
        uint32_t offset = row_offset + (px / 2);
        backbuffer[offset] = pattern;
        px += 2;
    }

    /* Trailing single pixel */
    if (px <= x1) {
        uint32_t offset = row_offset + (px / 2);
        backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
    }
    
    mark_dirty(x0, y, x1 - x0 + 1, 1);
}

void gfx_vline(int x, int y, int h, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || h <= 0) return;

    int y0 = y;
    int y1 = y + h - 1;

    if (y0 < 0) y0 = 0;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;
    
    if (y0 > y1) return;

    for (int py = y0; py <= y1; py++) {
        uint32_t offset = py * (GFX_WIDTH / 2) + (x / 2);
        if (x & 1) {
            backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
        } else {
            backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
    }
    
    mark_dirty(x, y0, 1, y1 - y0 + 1);
}


void gfx_rect(int x, int y, int w, int h, uint8_t color) {
    if (w <= 0 || h <= 0) return;
    
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_fillrect(int x, int y, int w, int h, uint8_t color) {
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

    /* Use optimized horizontal line writer per row */
    for (int py = y0; py <= y1; py++) {
        gfx_hline(x0, py, x1 - x0 + 1, color);
    }
    
    mark_dirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

void gfx_floodfill(int x, int y, uint8_t new_color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    uint8_t target = getpixel_raw(x, y);
    if (target == new_color) {
        return;
    }

    size_t max_pixels = (size_t)GFX_WIDTH * (size_t)GFX_HEIGHT;
    uint32_t* stack = (uint32_t*)kmalloc(max_pixels * sizeof(uint32_t));
    if (!stack) {
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

        while (lx - 1 >= 0 && getpixel_raw(lx - 1, cy) == target) {
            lx--;
        }
        while (rx + 1 < GFX_WIDTH && getpixel_raw(rx + 1, cy) == target) {
            rx++;
        }

        for (int px = lx; px <= rx; px++) {
            putpixel_raw(px, cy, new_color);
        }
        
        if (lx < min_x) min_x = lx;
        if (rx > max_x) max_x = rx;
        if (cy < min_y) min_y = cy;
        if (cy > max_y) max_y = cy;

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
    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

void gfx_circle(int cx, int cy, int r, uint8_t color) {
    if (!backbuffer) return;
    if (r < 0) return;
    
    int x = r;
    int y = 0;
    int err = 0;
    
    int min_x = cx - r;
    int max_x = cx + r;
    int min_y = cy - r;
    int max_y = cy + r;
    
    while (x >= y) {
        if (cx + x >= 0 && cx + x < GFX_WIDTH && cy + y >= 0 && cy + y < GFX_HEIGHT) {
            uint32_t offset = (cy + y) * (GFX_WIDTH / 2) + ((cx + x) / 2);
            if ((cx + x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - x >= 0 && cx - x < GFX_WIDTH && cy + y >= 0 && cy + y < GFX_HEIGHT) {
            uint32_t offset = (cy + y) * (GFX_WIDTH / 2) + ((cx - x) / 2);
            if ((cx - x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx + x >= 0 && cx + x < GFX_WIDTH && cy - y >= 0 && cy - y < GFX_HEIGHT) {
            uint32_t offset = (cy - y) * (GFX_WIDTH / 2) + ((cx + x) / 2);
            if ((cx + x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - x >= 0 && cx - x < GFX_WIDTH && cy - y >= 0 && cy - y < GFX_HEIGHT) {
            uint32_t offset = (cy - y) * (GFX_WIDTH / 2) + ((cx - x) / 2);
            if ((cx - x) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
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
        
        if (cx + y >= 0 && cx + y < GFX_WIDTH && cy - x >= 0 && cy - x < GFX_HEIGHT) {
            uint32_t offset = (cy - x) * (GFX_WIDTH / 2) + ((cx + y) / 2);
            if ((cx + y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
            else backbuffer[offset] = (backbuffer[offset] & 0x0F) | ((color & 0x0F) << 4);
        }
        
        if (cx - y >= 0 && cx - y < GFX_WIDTH && cy - x >= 0 && cy - x < GFX_HEIGHT) {
            uint32_t offset = (cy - x) * (GFX_WIDTH / 2) + ((cx - y) / 2);
            if ((cx - y) & 1) backbuffer[offset] = (backbuffer[offset] & 0xF0) | (color & 0x0F);
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
    
    mark_dirty(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
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

    mark_dirty(x0, y0, eff_w, eff_h);
}

void gfx_floodfill_gradient(int x, int y,
                            uint8_t c_start, uint8_t c_end,
                            int orientation) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;

    uint8_t target = getpixel_raw(x, y);

    if (target == c_start || target == c_end) {
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
        return;
    }

    size_t max_pixels = (size_t)GFX_WIDTH * (size_t)GFX_HEIGHT;
    uint32_t *stack = (uint32_t*)kmalloc(max_pixels * sizeof(uint32_t));
    if (!stack) {
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

    mark_dirty(min_x, min_y, eff_w, eff_h);
}

static uint8_t find_closest_color(uint8_t r, uint8_t g, uint8_t b) {
    int best_idx = 0;
    int best_dist = 0x7FFFFFFF;
    
    for (int i = 0; i < 16; i++) {
        int dr = (int)r - (int)gfx_palette[i][0];
        int dg = (int)g - (int)gfx_palette[i][1];
        int db = (int)b - (int)gfx_palette[i][2];
        int dist = dr*dr + dg*dg + db*db;
        
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    
    return (uint8_t)best_idx;
}

uint8_t* gfx_load_bmp_to_buffer(const char *path, int *out_width, int *out_height) {
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) return NULL;

    bmp_header_t header;
    bmp_info_t info;

    if (fat32_read(f, &header, sizeof(header)) != sizeof(header)) {
        fat32_close(f);
        return NULL;
    }

    if (header.type != 0x4D42) {
        fat32_close(f);
        return NULL;
    }

    if (fat32_read(f, &info, sizeof(info)) != sizeof(info)) {
        fat32_close(f);
        return NULL;
    }

    if (info.compression != 0) {
        fat32_close(f);
        return NULL;
    }

    if (info.bpp != 4 && info.bpp != 8 && info.bpp != 24) {
        fat32_close(f);
        return NULL;
    }

    int width = info.width;
    int height = info.height > 0 ? info.height : -info.height;
    int bottom_up = info.height > 0;

    /* Reject implausibly large bitmaps that would exhaust memory or CPU */
    const int MAX_DIM = 4096;
    if (width > MAX_DIM || height > MAX_DIM) {
        fat32_close(f);
        return NULL;
    }

    /* Allocate buffer for output (always 4-bit = 2 pixels per byte) */
    int out_row_bytes = (width + 1) / 2;
    size_t buffer_size = (size_t)out_row_bytes * (size_t)height;
    const size_t MAX_BUFFER = 1024 * 1024;
    if (buffer_size == 0 || buffer_size > MAX_BUFFER) {
        fat32_close(f);
        return NULL;
    }

    uint8_t *bitmap = kmalloc((int)buffer_size);
    if (!bitmap) {
        fat32_close(f);
        return NULL;
    }
    /* Zero the output buffer so preserved nibbles are clean when writing pixels */
    memset_s(bitmap, 0, buffer_size);

    /* Read palette for indexed formats, capped to 16 colors for our 4-bit display */
    uint8_t (*palette)[3] = NULL;
    int palette_entries = 0;

    if (info.bpp <= 8) {
        palette_entries = info.colors_used ? (int)info.colors_used : (1 << info.bpp);
        if (palette_entries > 256) palette_entries = 256;
        
        int load_entries = palette_entries < 16 ? palette_entries : 16;
        palette = kmalloc(16 * 3);
        if (!palette) {
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }
        memset_s(palette, 0, 16 * 3);
        
        for (int i = 0; i < load_entries; i++) {
            uint8_t bgr[4];
            if (fat32_read(f, bgr, 4) != 4) {
                kfree(palette);
                kfree(bitmap);
                fat32_close(f);
                return NULL;
            }
            palette[i][0] = bgr[2];
            palette[i][1] = bgr[1];
            palette[i][2] = bgr[0];
        }
        
        /* Skip remaining palette entries if palette is larger than 16 */
        for (int i = load_entries; i < palette_entries; i++) {
            uint8_t bgr[4];
            fat32_read(f, bgr, 4);
        }
        
        palette_entries = load_entries;
    }

    /* Seek to pixel data */
    fat32_seek(f, header.offset);

    /* Calculate source row size (DWORD-aligned) */
    int src_row_size;
    if (info.bpp == 24) {
        src_row_size = ((width * 3) + 3) & ~3;
    } else if (info.bpp == 8) {
        src_row_size = (width + 3) & ~3;
    } else {
        src_row_size = (((width + 1) / 2) + 3) & ~3;
    }

    uint8_t *row_buf = kmalloc(src_row_size);
    if (!row_buf) {
        if (palette) kfree(palette);
        kfree(bitmap);
        fat32_close(f);
        return NULL;
    }

    /* Allocate error buffers for dithering (Floyd-Steinberg) */
    int16_t *err_r_curr = NULL, *err_g_curr = NULL, *err_b_curr = NULL;
    int16_t *err_r_next = NULL, *err_g_next = NULL, *err_b_next = NULL;
    
    if (info.bpp >= 8 && width > 0 && width <= MAX_DIM) {
        size_t err_buf_size = (size_t)width * sizeof(int16_t);
        if (err_buf_size > 0 && err_buf_size < MAX_BUFFER) {
            err_r_curr = kmalloc((int)err_buf_size);
            err_g_curr = kmalloc((int)err_buf_size);
            err_b_curr = kmalloc((int)err_buf_size);
            err_r_next = kmalloc((int)err_buf_size);
            err_g_next = kmalloc((int)err_buf_size);
            err_b_next = kmalloc((int)err_buf_size);
        }
        
        if (!err_r_curr || !err_g_curr || !err_b_curr || !err_r_next || !err_g_next || !err_b_next) {
            if (err_r_curr) kfree(err_r_curr);
            if (err_g_curr) kfree(err_g_curr);
            if (err_b_curr) kfree(err_b_curr);
            if (err_r_next) kfree(err_r_next);
            if (err_g_next) kfree(err_g_next);
            if (err_b_next) kfree(err_b_next);
            kfree(row_buf);
            if (palette) kfree(palette);
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }
        
        for (int x = 0; x < width; x++) {
            err_r_curr[x] = err_g_curr[x] = err_b_curr[x] = 0;
            err_r_next[x] = err_g_next[x] = err_b_next[x] = 0;
        }
    }

    /* Read and convert bitmap data */
    for (int y = 0; y < height; y++) {
        if (fat32_read(f, row_buf, src_row_size) != src_row_size) {
            if (err_r_curr) kfree(err_r_curr);
            if (err_g_curr) kfree(err_g_curr);
            if (err_b_curr) kfree(err_b_curr);
            if (err_r_next) kfree(err_r_next);
            if (err_g_next) kfree(err_g_next);
            if (err_b_next) kfree(err_b_next);
            kfree(row_buf);
            if (palette) kfree(palette);
            kfree(bitmap);
            fat32_close(f);
            return NULL;
        }

        int dest_y = bottom_up ? (height - 1 - y) : y;
        int dest_offset = dest_y * out_row_bytes;

        if (info.bpp == 24) {
            for (int x = 0; x < width; x++) {
                int16_t r = (int16_t)row_buf[x * 3 + 2] + err_r_curr[x];
                int16_t g = (int16_t)row_buf[x * 3 + 1] + err_g_curr[x];
                int16_t b = (int16_t)row_buf[x * 3 + 0] + err_b_curr[x];
                
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                
                uint8_t color = find_closest_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
                
                int16_t err_r = r - (int16_t)gfx_palette[color][0];
                int16_t err_g = g - (int16_t)gfx_palette[color][1];
                int16_t err_b = b - (int16_t)gfx_palette[color][2];
                
                if (x + 1 < width) {
                    err_r_curr[x + 1] += (err_r * 7) / 16;
                    err_g_curr[x + 1] += (err_g * 7) / 16;
                    err_b_curr[x + 1] += (err_b * 7) / 16;
                }
                if (x > 0) {
                    err_r_next[x - 1] += (err_r * 3) / 16;
                    err_g_next[x - 1] += (err_g * 3) / 16;
                    err_b_next[x - 1] += (err_b * 3) / 16;
                }
                err_r_next[x] += (err_r * 5) / 16;
                err_g_next[x] += (err_g * 5) / 16;
                err_b_next[x] += (err_b * 5) / 16;
                if (x + 1 < width) {
                    err_r_next[x + 1] += err_r / 16;
                    err_g_next[x + 1] += err_g / 16;
                    err_b_next[x + 1] += err_b / 16;
                }
                
                int byte_idx = x / 2;
                if (x & 1) {
                    bitmap[dest_offset + byte_idx] = (bitmap[dest_offset + byte_idx] & 0xF0) | color;
                } else {
                    bitmap[dest_offset + byte_idx] = (color << 4) | (bitmap[dest_offset + byte_idx] & 0x0F);
                }
            }
            
            int16_t *tmp_r = err_r_curr; err_r_curr = err_r_next; err_r_next = tmp_r;
            int16_t *tmp_g = err_g_curr; err_g_curr = err_g_next; err_g_next = tmp_g;
            int16_t *tmp_b = err_b_curr; err_b_curr = err_b_next; err_b_next = tmp_b;
            for (int x = 0; x < width; x++) {
                err_r_next[x] = err_g_next[x] = err_b_next[x] = 0;
            }
        } else if (info.bpp == 8) {
            for (int x = 0; x < width; x++) {
                uint8_t idx = row_buf[x];
                if (idx >= palette_entries) idx = 0;
                
                int16_t r = (int16_t)palette[idx][0] + err_r_curr[x];
                int16_t g = (int16_t)palette[idx][1] + err_g_curr[x];
                int16_t b = (int16_t)palette[idx][2] + err_b_curr[x];
                
                if (r < 0) r = 0; else if (r > 255) r = 255;
                if (g < 0) g = 0; else if (g > 255) g = 255;
                if (b < 0) b = 0; else if (b > 255) b = 255;
                
                uint8_t color = find_closest_color((uint8_t)r, (uint8_t)g, (uint8_t)b);
                
                int16_t err_r = r - (int16_t)gfx_palette[color][0];
                int16_t err_g = g - (int16_t)gfx_palette[color][1];
                int16_t err_b = b - (int16_t)gfx_palette[color][2];
                
                if (x + 1 < width) {
                    err_r_curr[x + 1] += (err_r * 7) / 16;
                    err_g_curr[x + 1] += (err_g * 7) / 16;
                    err_b_curr[x + 1] += (err_b * 7) / 16;
                }
                if (x > 0) {
                    err_r_next[x - 1] += (err_r * 3) / 16;
                    err_g_next[x - 1] += (err_g * 3) / 16;
                    err_b_next[x - 1] += (err_b * 3) / 16;
                }
                err_r_next[x] += (err_r * 5) / 16;
                err_g_next[x] += (err_g * 5) / 16;
                err_b_next[x] += (err_b * 5) / 16;
                if (x + 1 < width) {
                    err_r_next[x + 1] += err_r / 16;
                    err_g_next[x + 1] += err_g / 16;
                    err_b_next[x + 1] += err_b / 16;
                }
                
                int byte_idx = x / 2;
                if (x & 1) {
                    bitmap[dest_offset + byte_idx] = (bitmap[dest_offset + byte_idx] & 0xF0) | color;
                } else {
                    bitmap[dest_offset + byte_idx] = (color << 4) | (bitmap[dest_offset + byte_idx] & 0x0F);
                }
            }
            
            int16_t *tmp_r = err_r_curr; err_r_curr = err_r_next; err_r_next = tmp_r;
            int16_t *tmp_g = err_g_curr; err_g_curr = err_g_next; err_g_next = tmp_g;
            int16_t *tmp_b = err_b_curr; err_b_curr = err_b_next; err_b_next = tmp_b;
            for (int x = 0; x < width; x++) {
                err_r_next[x] = err_g_next[x] = err_b_next[x] = 0;
            }
        } else {
            for (int x = 0; x < out_row_bytes; x++) {
                uint8_t byte_val = row_buf[x];
                uint8_t hi = (byte_val >> 4) & 0x0F;
                uint8_t lo = byte_val & 0x0F;
                
                if (palette_entries > 0) {
                    if (hi < palette_entries) hi = find_closest_color(palette[hi][0], palette[hi][1], palette[hi][2]);
                    if (lo < palette_entries) lo = find_closest_color(palette[lo][0], palette[lo][1], palette[lo][2]);
                }
                
                bitmap[dest_offset + x] = (hi << 4) | lo;
            }
        }
    }
    
    if (err_r_curr) kfree(err_r_curr);
    if (err_g_curr) kfree(err_g_curr);
    if (err_b_curr) kfree(err_b_curr);
    if (err_r_next) kfree(err_r_next);
    if (err_g_next) kfree(err_g_next);
    if (err_b_next) kfree(err_b_next);

    if (palette) kfree(palette);
    kfree(row_buf);
    fat32_close(f);

    *out_width = width;
    *out_height = height;
    return bitmap;
}

void gfx_draw_cached_bmp_ex(uint8_t *cached_data, int width, int height, int dest_x, int dest_y, int transparent) {
    if (!backbuffer || !cached_data) return;

    /* Calculate actual dirty region */
    int dirty_x0 = dest_x < 0 ? 0 : dest_x;
    int dirty_y0 = dest_y < 0 ? 0 : dest_y;
    int dirty_x1 = dest_x + width - 1;
    int dirty_y1 = dest_y + height - 1;

    if (dirty_x1 >= GFX_WIDTH) dirty_x1 = GFX_WIDTH - 1;
    if (dirty_y1 >= GFX_HEIGHT) dirty_y1 = GFX_HEIGHT - 1;

    int src_row_bytes = (width + 1) / 2;

    /* Fast path: no transparency, aligned coordinates, fully on screen */
    if (!transparent && (dest_x & 1) == 0 && dest_x >= 0 && dest_x + width <= GFX_WIDTH) {
        int dest_byte_x = dest_x / 2;
        for (int y = 0; y < height; y++) {
            int screen_y = dest_y + y;
            if (screen_y < 0 || screen_y >= GFX_HEIGHT) continue;

            uint8_t *src = cached_data + y * src_row_bytes;
            uint8_t *dst = backbuffer + screen_y * (GFX_WIDTH / 2) + dest_byte_x;
            memcpy_s(dst, src, src_row_bytes);
        }
    } else {
        /* Slow path: pixel-by-pixel for transparency or unaligned */
        for (int y = 0; y < height; y++) {
            int screen_y = dest_y + y;
            if (screen_y < 0 || screen_y >= GFX_HEIGHT) continue;

            int src_offset = y * src_row_bytes;

            for (int x = 0; x < width; x++) {
                int byte_idx = x / 2;
                uint8_t pixel = (x & 1) ? (cached_data[src_offset + byte_idx] & 0x0F) : (cached_data[src_offset + byte_idx] >> 4);

                int screen_x = dest_x + x;

                /* Skip transparent pixels (color index 5) only if transparency enabled */
                if (screen_x >= 0 && screen_x < GFX_WIDTH && (!transparent || pixel != 5)) {
                    putpixel_raw(screen_x, screen_y, pixel);
                }
            }
        }
    }

    /* Mark only the actually drawn region as dirty */
    if (dirty_x1 >= dirty_x0 && dirty_y1 >= dirty_y0) {
        mark_dirty(dirty_x0, dirty_y0, dirty_x1 - dirty_x0 + 1, dirty_y1 - dirty_y0 + 1);
    }
}

void gfx_draw_cached_bmp(uint8_t *cached_data, int width, int height, int dest_x, int dest_y) {
    gfx_draw_cached_bmp_ex(cached_data, width, height, dest_x, dest_y, 1);
}

/* Draw only a sub-rectangle of a cached bitmap. This mirrors the slow path in gfx_draw_cached_bmp_ex
   but reads pixels from the given source rectangle (src_x,src_y,src_w,src_h) and writes them at
   (dest_x,dest_y) on screen. */
void gfx_draw_cached_bmp_region(uint8_t *cached_data, int width, int height, int dest_x, int dest_y,
                                int src_x, int src_y, int src_w, int src_h, int transparent) {
    if (!backbuffer || !cached_data) return;

    /* Clip source rectangle to bitmap bounds */
    if (src_x < 0) { src_w += src_x; src_x = 0; }
    if (src_y < 0) { src_h += src_y; src_y = 0; }
    if (src_x >= width || src_y >= height) return;
    if (src_x + src_w > width) src_w = width - src_x;
    if (src_y + src_h > height) src_h = height - src_y;
    if (src_w <= 0 || src_h <= 0) return;

    int src_row_bytes = (width + 1) / 2;

    for (int y = 0; y < src_h; y++) {
        int screen_y = dest_y + y;
        if (screen_y < 0 || screen_y >= GFX_HEIGHT) continue;

        int src_offset = (src_y + y) * src_row_bytes;

        for (int x = 0; x < src_w; x++) {
            int sx = src_x + x;
            int byte_idx = sx / 2;
            uint8_t pixel = (sx & 1) ? (cached_data[src_offset + byte_idx] & 0x0F) : (cached_data[src_offset + byte_idx] >> 4);

            int screen_x = dest_x + x;

            if (screen_x >= 0 && screen_x < GFX_WIDTH && (!transparent || pixel != 5)) {
                putpixel_raw(screen_x, screen_y, pixel);
            }
        }
    }

    /* Mark the actually drawn region as dirty (clip to screen) */
    int x0 = dest_x;
    int y0 = dest_y;
    int x1 = dest_x + src_w - 1;
    int y1 = dest_y + src_h - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= GFX_WIDTH) x1 = GFX_WIDTH - 1;
    if (y1 >= GFX_HEIGHT) y1 = GFX_HEIGHT - 1;

    if (x1 >= x0 && y1 >= y0) {
        mark_dirty(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
    }
}

/* Fast/robust helpers to read/write rectangular regions as packed 4bpp rows. */
void gfx_read_screen_region_packed(uint8_t *dst, int width, int height, int dest_x, int dest_y) {
    if (!backbuffer || !dst) return;

    int row_bytes = (width + 1) / 2;

    /* Fast path: fully on-screen and byte-aligned (dest_x even) */
    if (dest_x >= 0 && dest_y >= 0 && dest_x + width <= GFX_WIDTH && dest_y + height <= GFX_HEIGHT && (dest_x & 1) == 0) {
        int src_byte_x = dest_x / 2;
        int screen_row_bytes = GFX_WIDTH / 2;
        for (int y = 0; y < height; y++) {
            uint8_t *src = backbuffer + (dest_y + y) * screen_row_bytes + src_byte_x;
            uint8_t *dst_row = dst + y * row_bytes;
            memcpy_s(dst_row, src, (size_t)row_bytes);
        }
        return;
    }

    /* Fallback: safe per-pixel read and packing into dst */
    for (int y = 0; y < height; y++) {
        int dy = dest_y + y;
        uint8_t *dst_row = dst + y * row_bytes;
        /* Initialize row to zero */
        for (int b = 0; b < row_bytes; b++) dst_row[b] = 0;

        for (int x = 0; x < width; x++) {
            int sx = dest_x + x;
            uint8_t pix = 0;
            if (sx >= 0 && sx < GFX_WIDTH && dy >= 0 && dy < GFX_HEIGHT) {
                pix = gfx_getpixel(sx, dy);
            }
            int byte_idx = x / 2;
            if (x & 1) {
                dst_row[byte_idx] = (dst_row[byte_idx] & 0xF0) | (pix & 0x0F);
            } else {
                dst_row[byte_idx] = (dst_row[byte_idx] & 0x0F) | (pix << 4);
            }
        }
    }
}

void gfx_write_screen_region_packed(uint8_t *src, int width, int height, int dest_x, int dest_y) {
    if (!backbuffer || !src) return;

    int row_bytes = (width + 1) / 2;

    /* Fast path: fully on-screen and aligned to byte boundary */
    if (dest_x >= 0 && dest_y >= 0 && dest_x + width <= GFX_WIDTH && dest_y + height <= GFX_HEIGHT && (dest_x & 1) == 0) {
        int dest_byte_x = dest_x / 2;
        int screen_row_bytes = GFX_WIDTH / 2;
        for (int y = 0; y < height; y++) {
            uint8_t *dst_row = backbuffer + (dest_y + y) * screen_row_bytes + dest_byte_x;
            uint8_t *src_row = src + y * row_bytes;
            memcpy_s(dst_row, src_row, (size_t)row_bytes);
        }
        mark_dirty(dest_x, dest_y, width, height);
        return;
    }

    /* Fallback: unpack pixels and write per-pixel */
    for (int y = 0; y < height; y++) {
        int sy = dest_y + y;
        uint8_t *src_row = src + y * row_bytes;
        for (int x = 0; x < width; x++) {
            int byte_idx = x / 2;
            uint8_t packed = src_row[byte_idx];
            uint8_t pix = (x & 1) ? (packed & 0x0F) : (packed >> 4);
            int sx = dest_x + x;
            if (sx >= 0 && sx < GFX_WIDTH && sy >= 0 && sy < GFX_HEIGHT) {
                putpixel_raw(sx, sy, pix);
            }
        }
    }
    mark_dirty(dest_x, dest_y, width, height);
}

int gfx_load_bmp_4bit_ex(const char *path, int dest_x, int dest_y, int transparent) {
    int width, height;
    uint8_t *bitmap = gfx_load_bmp_to_buffer(path, &width, &height);
    if (!bitmap) return -1;

    gfx_draw_cached_bmp_ex(bitmap, width, height, dest_x, dest_y, transparent);
    kfree(bitmap);

    return 0;
}

int gfx_load_bmp_4bit(const char *path, int dest_x, int dest_y) {
    return gfx_load_bmp_4bit_ex(path, dest_x, dest_y, 1);
}