#pragma once

#include "../graphics.h"
#include "../../console.h"
#include "../../mem/heap.h"
#include "../../irq/io.h"
#include "vga.h"
#include "../fat32.h"

#define GFX_LOCK() __asm__ volatile("cli")
#define GFX_UNLOCK() __asm__ volatile("sti")

extern uint8_t *gfx_backbuffer;
extern uint8_t *gfx_frontbuffer;
extern uint8_t gfx_plane_pair_table[4][256];
extern int gfx_plane_table_init;
extern int gfx_active;
extern volatile int gfx_dirty_y0;
extern volatile int gfx_dirty_y1;
extern volatile int gfx_dirty_row_x0[GFX_HEIGHT];
extern volatile int gfx_dirty_row_x1[GFX_HEIGHT];
extern volatile int gfx_full_redraw;
extern int gfx_clip_enabled;
extern int gfx_clip_x0;
extern int gfx_clip_y0;
extern int gfx_clip_x1;
extern int gfx_clip_y1;

#define backbuffer gfx_backbuffer
#define frontbuffer gfx_frontbuffer
#define plane_pair_table gfx_plane_pair_table
#define plane_table_init gfx_plane_table_init
#define graphics_active gfx_active
#define dirty_y0 gfx_dirty_y0
#define dirty_y1 gfx_dirty_y1
#define full_redraw gfx_full_redraw

static inline void mark_dirty(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;

    int64_t x1_wide = (int64_t)x + w;
    int64_t y1_wide = (int64_t)y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int x1 = x1_wide > GFX_WIDTH ? GFX_WIDTH : (int)x1_wide;
    int y1 = y1_wide > GFX_HEIGHT ? GFX_HEIGHT : (int)y1_wide;
    if (gfx_clip_enabled) {
        if (x < gfx_clip_x0) x = gfx_clip_x0;
        if (y < gfx_clip_y0) y = gfx_clip_y0;
        if (x1 > gfx_clip_x1) x1 = gfx_clip_x1;
        if (y1 > gfx_clip_y1) y1 = gfx_clip_y1;
    }
    if (x >= x1 || y >= y1) return;

    if (y < dirty_y0) dirty_y0 = y;
    if (y1 - 1 > dirty_y1) dirty_y1 = y1 - 1;
    for (int row = y; row < y1; row++) {
        if (x < gfx_dirty_row_x0[row]) gfx_dirty_row_x0[row] = x;
        if (x1 - 1 > gfx_dirty_row_x1[row]) gfx_dirty_row_x1[row] = x1 - 1;
    }
}

static inline void putpixel_raw(int x, int y, uint8_t color) {
    if (!backbuffer) return;
    if (x < 0 || x >= GFX_WIDTH || y < 0 || y >= GFX_HEIGHT) return;
    if (gfx_clip_enabled &&
        (x < gfx_clip_x0 || x >= gfx_clip_x1 ||
         y < gfx_clip_y0 || y >= gfx_clip_y1)) return;

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
