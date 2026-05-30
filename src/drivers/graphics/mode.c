#include "gpriv.h"

uint8_t *backbuffer = NULL;
int graphics_active = 0;
uint8_t *frontbuffer = NULL;
uint8_t plane_pair_table[4][256];
int plane_table_init = 0;
volatile int dirty_x0 = GFX_WIDTH, dirty_y0 = GFX_HEIGHT;
volatile int dirty_x1 = -1, dirty_y1 = -1;
volatile int full_redraw = 1;

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
    0x0C, 0x00, 0x0F, 0x00, 0x00
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
    if (frontbuffer) memset_s(frontbuffer, 0, GFX_BUFFER_SIZE);
}

void gfx_exit_mode(void) {
    graphics_active = 0;
    vga_write_regs(mode_80x25_text);
    vga_reset_textmode();
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
uint8_t* gfx_get_backbuffer(void) {
    return backbuffer;
}
