#include "gpriv.h"
#include "../../lib/string.h"
#include "../../fonts/mono8x16.h"
#include "../../mem/paging.h"
#include "../../mem/pmm.h"
#include "../../task/exec.h"
#include "../../arch/vbe_bios.h"
#include "vbe.h"

uint8_t *backbuffer = NULL;
uint32_t *gfx_colorbuffer = NULL;
int graphics_active = 0;
uint8_t *frontbuffer = NULL;
uint8_t plane_pair_table[4][256];
int plane_table_init = 0;
int gfx_width = 640;
int gfx_height = 480;
volatile int dirty_y0 = 480;
volatile int dirty_y1 = -1;
volatile int *gfx_dirty_row_x0 = NULL;
volatile int *gfx_dirty_row_x1 = NULL;
volatile int full_redraw = 1;
int gfx_clip_enabled = 0;
int gfx_clip_x0 = 0, gfx_clip_y0 = 0;
int gfx_clip_x1 = 640, gfx_clip_y1 = 480;

typedef struct {
    volatile uint8_t *address;
    uintptr_t physical_address;
    size_t length;
    uint32_t pitch;
    uint32_t bpp;
    uint8_t red_position, red_size;
    uint8_t green_position, green_size;
    uint8_t blue_position, blue_size;
    uint8_t memory_model;
    uint8_t dac_compatible;
    uint8_t window_number;
    uint32_t window_size, window_granularity;
    uint32_t window_start;
    int window_valid;
    int error;
    int valid;
} framebuffer_t;

static framebuffer_t framebuffer;
static int framebuffer_mapped;
static uint8_t color_lookup[32 * 32 * 32];
static uint8_t color_palette[256][3];

#define GFX_MMIO_PAGES ((16u * 1024u * 1024u / PAGE_SIZE) + 1u)
static uint8_t gfx_owned_pages[0x100000u / 8u];

static size_t framebuffer_map_length(const framebuffer_t *fb) {
    return fb->window_size ? fb->window_size : fb->length;
}

static int framebuffer_contains_page(const framebuffer_t *fb, uint64_t page) {
    if (!fb->valid) return 0;
    uint64_t first = (uint64_t)fb->physical_address & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end = ((uint64_t)fb->physical_address + framebuffer_map_length(fb) +
                    PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    return page >= first && page < end;
}

static void release_framebuffer_pages(const framebuffer_t *fb,
                                      const framebuffer_t *keep) {
    if (!fb->valid) return;
    uint64_t first = (uint64_t)fb->physical_address & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end = ((uint64_t)fb->physical_address + framebuffer_map_length(fb) +
                    PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
    for (uint64_t page = first; page < end; page += PAGE_SIZE) {
        size_t index = (size_t)(page >> 12);
        uint8_t bit = (uint8_t)(1u << (index & 7));
        if (!(gfx_owned_pages[index >> 3] & bit) ||
            (keep && framebuffer_contains_page(keep, page))) continue;
        if (paging_unmap_page((uintptr_t)page) == 0)
            gfx_owned_pages[index >> 3] &= (uint8_t)~bit;
    }
}

static int map_framebuffer(framebuffer_t *fb, int check_ram) {
    uint8_t added[(GFX_MMIO_PAGES + 7u) / 8u] = {0};
    size_t length = framebuffer_map_length(fb);
    uint64_t first = (uint64_t)fb->physical_address & ~(uint64_t)(PAGE_SIZE - 1);
    uint64_t end = ((uint64_t)fb->physical_address + length + PAGE_SIZE - 1) &
                   ~(uint64_t)(PAGE_SIZE - 1);
    if (!length || end > 0x100000000ull ||
        (end - first) / PAGE_SIZE > GFX_MMIO_PAGES ||
        (first < (uint64_t)EXEC_SLOT_BASE + EXEC_SLOT_SIZE * EXEC_SLOT_COUNT &&
         end > EXEC_SLOT_BASE) ||
        (check_ram && pmm_region_overlaps_ram(fb->physical_address, length) != 0))
        return -1;

    size_t page_number = 0;
    for (uint64_t page = first; page < end; page += PAGE_SIZE, ++page_number) {
        uintptr_t mapped;
        if (paging_get_physical((uintptr_t)page, &mapped) == 0) {
            if (mapped != (uintptr_t)page) goto fail;
            continue;
        }
        if (paging_map_page((uintptr_t)page, (uintptr_t)page, P_PRESENT | P_RW) != 0)
            goto fail;
        paging_invalidate((uintptr_t)page);
        size_t index = (size_t)(page >> 12);
        gfx_owned_pages[index >> 3] |= (uint8_t)(1u << (index & 7));
        added[page_number >> 3] |= (uint8_t)(1u << (page_number & 7));
    }
    return 0;

fail:
    page_number = 0;
    for (uint64_t page = first; page < end; page += PAGE_SIZE, ++page_number) {
        if (!(added[page_number >> 3] & (1u << (page_number & 7)))) continue;
        if (paging_unmap_page((uintptr_t)page) == 0) {
            size_t index = (size_t)(page >> 12);
            gfx_owned_pages[index >> 3] &= (uint8_t)~(1u << (index & 7));
        }
    }
    return -1;
}

static int valid_component(uint8_t position, uint8_t size, uint8_t bpp) {
    return size > 0 && size <= 8 && position < bpp &&
           (unsigned)position + size <= bpp;
}

void gfx_set_boot_framebuffer(const multiboot_info_t *info) {
    framebuffer.valid = 0;
    framebuffer_mapped = 0;
    gfx_width = WM_DEFAULT_SCREEN_WIDTH;
    gfx_height = WM_DEFAULT_SCREEN_HEIGHT;
    gfx_clip_x0 = gfx_clip_y0 = 0;
    gfx_clip_x1 = gfx_width;
    gfx_clip_y1 = gfx_height;

    if (!info || !(info->flags & MULTIBOOT_INFO_FLAG_FRAMEBUF) ||
        info->framebuffer_type != MULTIBOOT_FRAMEBUFFER_RGB ||
        (info->framebuffer_bpp != 16 && info->framebuffer_bpp != 24 &&
         info->framebuffer_bpp != 32) ||
        info->framebuffer_width < 640 || info->framebuffer_width > 2048 ||
        info->framebuffer_height < 400 || info->framebuffer_height > 1536 ||
        (info->framebuffer_width & 1u) != 0 ||
        info->framebuffer_addr > UINT32_MAX) {
        return;
    }

    uint32_t bytes_per_pixel = (info->framebuffer_bpp + 7u) / 8u;
    uint64_t minimum_pitch = (uint64_t)info->framebuffer_width * bytes_per_pixel;
    uint64_t length = (uint64_t)info->framebuffer_pitch * info->framebuffer_height;
    uint64_t end = info->framebuffer_addr + length;
    if (minimum_pitch > info->framebuffer_pitch || length == 0 ||
        length > 16u * 1024u * 1024u || end > 0x100000000ull ||
        end <= info->framebuffer_addr ||
        !valid_component(info->color_info.rgb.red_field_position,
                         info->color_info.rgb.red_mask_size, info->framebuffer_bpp) ||
        !valid_component(info->color_info.rgb.green_field_position,
                         info->color_info.rgb.green_mask_size, info->framebuffer_bpp) ||
        !valid_component(info->color_info.rgb.blue_field_position,
                         info->color_info.rgb.blue_mask_size, info->framebuffer_bpp)) {
        return;
    }

    uint32_t red_mask = ((1u << info->color_info.rgb.red_mask_size) - 1u)
                        << info->color_info.rgb.red_field_position;
    uint32_t green_mask = ((1u << info->color_info.rgb.green_mask_size) - 1u)
                          << info->color_info.rgb.green_field_position;
    uint32_t blue_mask = ((1u << info->color_info.rgb.blue_mask_size) - 1u)
                         << info->color_info.rgb.blue_field_position;
    if ((red_mask & green_mask) || (red_mask & blue_mask) ||
        (green_mask & blue_mask)) return;

    framebuffer.physical_address = (uintptr_t)info->framebuffer_addr;
    framebuffer.length = (size_t)length;
    framebuffer.address = (volatile uint8_t*)framebuffer.physical_address;
    framebuffer.pitch = info->framebuffer_pitch;
    framebuffer.bpp = info->framebuffer_bpp;
    framebuffer.red_position = info->color_info.rgb.red_field_position;
    framebuffer.red_size = info->color_info.rgb.red_mask_size;
    framebuffer.green_position = info->color_info.rgb.green_field_position;
    framebuffer.green_size = info->color_info.rgb.green_mask_size;
    framebuffer.blue_position = info->color_info.rgb.blue_field_position;
    framebuffer.blue_size = info->color_info.rgb.blue_mask_size;
    framebuffer.memory_model = 6;
    framebuffer.valid = 1;
    gfx_width = (int)info->framebuffer_width;
    gfx_height = (int)info->framebuffer_height;
    gfx_clip_x1 = gfx_width;
    gfx_clip_y1 = gfx_height;
}

int gfx_map_boot_framebuffer(void) {
    if (!framebuffer.valid) return 0;
    if (map_framebuffer(&framebuffer, 0) != 0) return -1;
    framebuffer_mapped = 1;
    return 0;
}

int gfx_has_boot_framebuffer(void) {
    return framebuffer.valid;
}

static uint32_t scale_component(uint8_t value, uint8_t bits) {
    uint32_t max_value = (1u << bits) - 1u;
    return ((uint32_t)value * max_value + 127u) / 255u;
}

static uint32_t framebuffer_color(uint8_t index) {
    const uint8_t *color = gfx_palette[index & 0x0F];
    uint32_t rgb = scale_component(color[0], framebuffer.red_size)
                   << framebuffer.red_position;
    rgb |= scale_component(color[1], framebuffer.green_size)
           << framebuffer.green_position;
    rgb |= scale_component(color[2], framebuffer.blue_size)
           << framebuffer.blue_position;
    return rgb;
}

static uint32_t rgb_components[3][256];

static void prepare_rgb_components(void) {
    static uint16_t previous[3];
    static int ready;
    uint16_t format[3] = {
        (uint16_t)((framebuffer.red_size << 8) | framebuffer.red_position),
        (uint16_t)((framebuffer.green_size << 8) | framebuffer.green_position),
        (uint16_t)((framebuffer.blue_size << 8) | framebuffer.blue_position)
    };
    for (int channel = 0; channel < 3; ++channel) {
        if (ready && previous[channel] == format[channel]) continue;
        uint8_t bits = (uint8_t)(format[channel] >> 8);
        uint8_t position = (uint8_t)format[channel];
        for (int value = 0; value < 256; ++value)
            rgb_components[channel][value] = scale_component((uint8_t)value, bits) << position;
        previous[channel] = format[channel];
    }
    ready = 1;
}

static uint32_t framebuffer_rgb(uint32_t rgb) {
    return rgb_components[0][(rgb >> 16) & 255] |
           rgb_components[1][(rgb >> 8) & 255] |
           rgb_components[2][rgb & 255];
}

static uint8_t dither_component(uint8_t value, int offset) {
    int result = (int)value + offset;
    if (result < 0) return 0;
    if (result > 255) return 255;
    return (uint8_t)result;
}

static uint8_t framebuffer_index(uint32_t rgb, int x, int y) {
    uint8_t red = (uint8_t)(rgb >> 16), green = (uint8_t)(rgb >> 8), blue = (uint8_t)rgb;
    unsigned index = color_lookup[((unsigned)(red >> 3) << 10) |
                                  ((unsigned)(green >> 3) << 5) | (blue >> 3)];
    if (color_palette[index][0] == red && color_palette[index][1] == green &&
        color_palette[index][2] == blue) return (uint8_t)index;
    int offset = ((int)bayer8[y & 7][x & 7] - 32) * 24 / 64;
    unsigned r = dither_component(red, offset) >> 3;
    unsigned g = dither_component(green, offset) >> 3;
    unsigned b = dither_component(blue, offset) >> 3;
    return color_lookup[(r << 10) | (g << 5) | b];
}

static volatile uint8_t *window_address(size_t offset) {
    if (offset >= framebuffer.length) {
        framebuffer.error = 1;
        return NULL;
    }
    if (!framebuffer.window_valid ||
        offset < framebuffer.window_start ||
        offset - framebuffer.window_start >= framebuffer.window_size) {
        uint32_t bank = (uint32_t)(offset / framebuffer.window_granularity);
        if (bank > UINT16_MAX ||
            (framebuffer.length > framebuffer.window_size &&
             vbe_set_window(framebuffer.window_number, (uint16_t)bank) != 0)) {
            framebuffer.error = 1;
            return NULL;
        }
        framebuffer.window_start = bank * framebuffer.window_granularity;
        framebuffer.window_valid = 1;
    }
    return framebuffer.address + offset - framebuffer.window_start;
}

static void framebuffer_put_index(int x, int y, uint8_t index) {
    if (framebuffer.memory_model == 3) {
        size_t offset = (size_t)y * framebuffer.pitch + (unsigned)x / 8u;
        volatile uint8_t *pixel = window_address(offset);
        if (!pixel) return;
        uint8_t bit = (uint8_t)(0x80u >> (x & 7));
        for (uint8_t plane = 0; plane < 4; ++plane) {
            outb(0x3ce, 4);
            outb(0x3cf, plane);
            uint8_t old = *pixel;
            outb(0x3c4, 2);
            outb(0x3c5, (uint8_t)(1u << plane));
            *pixel = (index & (1u << plane)) ? old | bit : old & (uint8_t)~bit;
        }
        outb(0x3c4, 2);
        outb(0x3c5, 0x0f);
        return;
    }
    if (framebuffer.memory_model == 4) {
        size_t offset = (size_t)y * framebuffer.pitch + (unsigned)x;
        volatile uint8_t *pixel = framebuffer.window_size
                                  ? window_address(offset)
                                  : framebuffer.address + offset;
        if (pixel) *pixel = index;
        return;
    }
    uint32_t bytes = framebuffer.bpp / 8u;
    uint32_t rgb = framebuffer_color(index);
    size_t offset = (size_t)y * framebuffer.pitch + (size_t)x * bytes;
    for (uint32_t byte = 0; byte < bytes; ++byte, ++offset) {
        volatile uint8_t *pixel = framebuffer.window_size
                                  ? window_address(offset)
                                  : framebuffer.address + offset;
        if (!pixel) return;
        *pixel = (uint8_t)(rgb >> (byte * 8));
    }
}

void gfx_console_draw_cell(int x, int y, uint8_t ch, uint8_t attr) {
    if (!framebuffer.valid) return;
    int origin_x = (gfx_width - 80 * 8) / 2;
    int origin_y = (gfx_height - 25 * 16) / 2;
    int px = origin_x + x * 8;
    int py = origin_y + y * 16;
    uint8_t foreground = attr & 0x0F;
    uint8_t background = (attr >> 4) & 0x0F;
    for (int row = 0; row < 16; ++row) {
        uint8_t bits = font_8x16[ch][row];
        for (int col = 0; col < 8; ++col) {
            framebuffer_put_index(px + col, py + row,
                                  (bits & (0x80u >> col)) ? foreground : background);
        }
    }
}

void gfx_console_draw_cursor(int x, int y, uint8_t attr) {
    if (!framebuffer.valid || (unsigned)x >= 80 || (unsigned)y >= 25) return;
    int origin_x = (gfx_width - 80 * 8) / 2;
    int origin_y = (gfx_height - 25 * 16) / 2;
    int px = origin_x + x * 8;
    int py = origin_y + y * 16 + 14;
    uint8_t foreground = attr & 0x0F;
    for (int col = 0; col < 8; ++col)
        framebuffer_put_index(px + col, py, foreground);
}

void gfx_set_clip_rect(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) {
        gfx_clip_x0 = gfx_clip_x1 = 0;
        gfx_clip_y0 = gfx_clip_y1 = 0;
        gfx_clip_enabled = 1;
        return;
    }

    int64_t x1_wide = (int64_t)x + w;
    int64_t y1_wide = (int64_t)y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    int x1 = x1_wide > GFX_WIDTH ? GFX_WIDTH : (int)x1_wide;
    int y1 = y1_wide > GFX_HEIGHT ? GFX_HEIGHT : (int)y1_wide;
    if (x1 < x) x1 = x;
    if (y1 < y) y1 = y;

    gfx_clip_x0 = x;
    gfx_clip_y0 = y;
    gfx_clip_x1 = x1;
    gfx_clip_y1 = y1;
    gfx_clip_enabled = 1;
}

void gfx_reset_clip_rect(void) {
    gfx_clip_enabled = 0;
    gfx_clip_x0 = 0;
    gfx_clip_y0 = 0;
    gfx_clip_x1 = GFX_WIDTH;
    gfx_clip_y1 = GFX_HEIGHT;
}

int gfx_clip_rect_contains(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    if (!gfx_clip_enabled) return 1;
    return x >= gfx_clip_x0 && y >= gfx_clip_y0 &&
           (int64_t)x + w <= gfx_clip_x1 &&
           (int64_t)y + h <= gfx_clip_y1;
}

static inline void reset_dirty(void) {
    dirty_y0 = GFX_HEIGHT;
    dirty_y1 = -1;
    if (!gfx_dirty_row_x0 || !gfx_dirty_row_x1) return;
    for (int y = 0; y < GFX_HEIGHT; y++) {
        gfx_dirty_row_x0[y] = GFX_WIDTH;
        gfx_dirty_row_x1[y] = -1;
    }
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

typedef struct {
    framebuffer_t framebuffer;
    int mapped, width, height;
    uint8_t *back, *front;
    uint32_t *colors;
    volatile int *row_x0, *row_x1;
    uint16_t hardware_mode;
} display_snapshot_t;

static display_snapshot_t previous_display;
static int display_pending;
static display_snapshot_t text_display;
static int text_suspended;
static uint8_t text_palette[16][3];

enum {
    MODE_SNAPSHOT_FAILED = -4,
    MODE_MEMORY_FAILED = -5,
    MODE_MAPPING_FAILED = -6,
    MODE_SET_FAILED = -7,
    MODE_VERIFY_FAILED = -8,
    MODE_PALETTE_FAILED = -9,
    MODE_BANK_FAILED = -10,
    MODE_PLANAR_FAILED = -11
};

static int same_vbe_mode(const vbe_mode_t *a, const vbe_mode_t *b) {
    return a->number == b->number && a->width == b->width &&
           a->height == b->height && a->bpp == b->bpp &&
           a->memory_model == b->memory_model &&
           a->vga_compatible == b->vga_compatible &&
           a->dac_compatible == b->dac_compatible && a->pitch == b->pitch &&
           a->physical_address == b->physical_address &&
           a->red_position == b->red_position && a->red_size == b->red_size &&
           a->green_position == b->green_position && a->green_size == b->green_size &&
           a->blue_position == b->blue_position && a->blue_size == b->blue_size &&
           a->window_segment == b->window_segment &&
           a->window_size_kb == b->window_size_kb &&
           a->window_granularity_kb == b->window_granularity_kb &&
           a->window_number == b->window_number;
}

static int snapshot_matches_mode(const display_snapshot_t *saved, const vbe_mode_t *mode) {
    const framebuffer_t *fb = &saved->framebuffer;
    return mode->width == saved->width && mode->height == saved->height &&
           mode->bpp == fb->bpp && mode->memory_model == fb->memory_model &&
           mode->pitch == fb->pitch &&
           (mode->window_size_kb ?
            ((uintptr_t)mode->window_segment << 4) == fb->physical_address &&
             (uint32_t)mode->window_size_kb * 1024u == fb->window_size &&
             (uint32_t)mode->window_granularity_kb * 1024u == fb->window_granularity &&
             mode->window_number == fb->window_number :
             mode->physical_address == fb->physical_address) &&
           (mode->memory_model != 6 ||
            (mode->red_position == fb->red_position && mode->red_size == fb->red_size &&
             mode->green_position == fb->green_position && mode->green_size == fb->green_size &&
             mode->blue_position == fb->blue_position && mode->blue_size == fb->blue_size));
}

static int find_snapshot_mode(display_snapshot_t *saved) {
    uint16_t hardware;
    vbe_mode_t current;
    if (vbe_get_hardware_mode(&hardware) == 0 &&
        ((hardware & 0x4000u) != 0) == (saved->framebuffer.window_size == 0) &&
        vbe_query_mode(hardware & 0x3fffu, &current) == 0 &&
        snapshot_matches_mode(saved, &current)) {
        saved->hardware_mode = hardware;
        return 0;
    }

    /* GRUB may set an RGB framebuffer directly without updating VBE 4F03. */
    if (saved->framebuffer.memory_model != 6) return -1;
    for (size_t i = 0; i < vbe_mode_count(); ++i) {
        const vbe_mode_t *mode = vbe_mode_at(i);
        if (mode && snapshot_matches_mode(saved, mode)) {
            saved->hardware_mode = mode->number | (mode->window_size_kb ? 0u : 0x4000u);
            return 0;
        }
    }
    return -1;
}

static void set_planar_palette_mapping(void) {
    (void)inb(0x3da);
    for (uint8_t i = 0; i < 16; ++i) {
        outb(0x3c0, i);
        outb(0x3c0, i);
    }
    outb(0x3c0, 0x20);
    outb(0x3c6, 0xff);
}

static int set_mode_palette(uint8_t model, int vga_compatible) {
    if (model != 4) return vbe_set_palette(gfx_palette, vga_compatible);
    uint8_t (*palette)[3] = color_palette;
    for (int i = 0; i < 16; ++i)
        for (int c = 0; c < 3; ++c) palette[i][c] = gfx_palette[i][c];
    for (int i = 0; i < 216; ++i) {
        palette[16 + i][0] = (uint8_t)((i / 36) * 51);
        palette[16 + i][1] = (uint8_t)(((i / 6) % 6) * 51);
        palette[16 + i][2] = (uint8_t)((i % 6) * 51);
    }
    for (int i = 0; i < 24; ++i)
        for (int c = 0; c < 3; ++c) palette[232 + i][c] = (uint8_t)((i * 255) / 23);
    for (unsigned r = 0; r < 32; ++r) {
        for (unsigned g = 0; g < 32; ++g) {
            for (unsigned b = 0; b < 32; ++b) {
                int red = (int)((r * 255u + 15u) / 31u);
                int green = (int)((g * 255u + 15u) / 31u);
                int blue = (int)((b * 255u + 15u) / 31u);
                int best = 16 + ((red + 25) / 51) * 36 +
                           ((green + 25) / 51) * 6 + (blue + 25) / 51;
                int dr = red - palette[best][0];
                int dg = green - palette[best][1];
                int db = blue - palette[best][2];
                int distance = dr * dr + dg * dg + db * db;
                int grey = 232 + ((red + green + blue) * 23 + 382) / 765;
                dr = red - palette[grey][0];
                dg = green - palette[grey][1];
                db = blue - palette[grey][2];
                int candidate = dr * dr + dg * dg + db * db;
                if (candidate < distance) {
                    distance = candidate;
                    best = grey;
                }
                for (int i = 0; i < 16; ++i) {
                    int dr = red - palette[i][0];
                    int dg = green - palette[i][1];
                    int db = blue - palette[i][2];
                    candidate = dr * dr + dg * dg + db * db;
                    if (candidate < distance) {
                        distance = candidate;
                        best = i;
                    }
                }
                color_lookup[(r << 10) | (g << 5) | b] = (uint8_t)best;
            }
        }
    }
    return vbe_set_palette_entries(palette, 256, vga_compatible);
}

static int restore_hardware(const display_snapshot_t *saved) {
    if (!saved->framebuffer.valid) {
        if (vbe_bios_vga_mode() != 0) return -1;
        vga_write_regs(mode_640x480x16);
        for (int i = 0; i < 16; ++i) {
            outb(0x3c8, i);
            outb(0x3c9, gfx_palette[i][0] >> 2);
            outb(0x3c9, gfx_palette[i][1] >> 2);
            outb(0x3c9, gfx_palette[i][2] >> 2);
        }
        return 0;
    }
    if (vbe_set_hardware_mode(saved->hardware_mode) != 0) return -1;
    if ((saved->framebuffer.memory_model == 3 ||
         saved->framebuffer.memory_model == 4) &&
        set_mode_palette(saved->framebuffer.memory_model,
                         saved->framebuffer.dac_compatible) != 0)
        return -1;
    if (saved->framebuffer.memory_model == 3) set_planar_palette_mapping();
    return 0;
}

static int validate_planar_access(const framebuffer_t *candidate) {
    static const uint8_t patterns[4] = {0x00, 0x55, 0xaa, 0xff};
    volatile uint8_t *pixel = candidate->address;
    for (uint8_t plane = 0; plane < 4; ++plane) {
        outb(0x3c4, 2);
        outb(0x3c5, (uint8_t)(1u << plane));
        *pixel = patterns[plane];
    }
    outb(0x3c4, 2);
    outb(0x3c5, 0x0f);
    for (uint8_t plane = 0; plane < 4; ++plane) {
        outb(0x3ce, 4);
        outb(0x3cf, plane);
        if (*pixel != patterns[plane]) return -1;
    }
    return 0;
}

static void reset_display_geometry(int width, int height) {
    gfx_width = width;
    gfx_height = height;
    gfx_reset_clip_rect();
    full_redraw = 1;
    dirty_y0 = 0;
    dirty_y1 = height - 1;
}

int gfx_vbe_try_mode(size_t index) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    const vbe_mode_t *selected = vbe_mode_at(index);
    if (!selected || display_pending || !graphics_active ||
        !backbuffer || !frontbuffer || !gfx_dirty_row_x0 || !gfx_dirty_row_x1) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }
    vbe_mode_t mode = *selected;
    display_snapshot_t saved = {
        .framebuffer = framebuffer, .mapped = framebuffer_mapped,
        .width = gfx_width, .height = gfx_height,
        .back = backbuffer, .front = frontbuffer,
        .colors = gfx_colorbuffer,
        .row_x0 = gfx_dirty_row_x0, .row_x1 = gfx_dirty_row_x1
    };
    if (framebuffer.valid) {
        if (find_snapshot_mode(&saved) != 0) {
            __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
            return MODE_SNAPSHOT_FAILED;
        }
    }

    int failure = MODE_MEMORY_FAILED;
    size_t buffer_size = ((size_t)mode.width * mode.height + 1u) / 2u;
    uint8_t *new_back = kmalloc(buffer_size);
    uint8_t *new_front = kmalloc(buffer_size);
    uint32_t *new_colors = mode.bpp > 4 ?
        kmalloc((size_t)mode.width * mode.height * sizeof(*new_colors)) : NULL;
    volatile int *new_x0 = kmalloc(sizeof(*new_x0) * (size_t)mode.height);
    volatile int *new_x1 = kmalloc(sizeof(*new_x1) * (size_t)mode.height);
    if (!new_back || !new_front || !new_x0 || !new_x1 ||
        (mode.bpp > 4 && !new_colors)) goto fail_alloc;
    memset_s(new_back, 0, buffer_size);
    memset_s(new_front, 0, buffer_size);
    if (new_colors)
        for (size_t i = 0; i < (size_t)mode.width * mode.height; ++i)
            new_colors[i] = 0x80000000u;

    framebuffer_t candidate = {0};
    candidate.valid = mode.number != VBE_VGA_MODE;
    candidate.memory_model = mode.memory_model;
    candidate.dac_compatible = mode.dac_compatible;
    candidate.pitch = mode.pitch;
    candidate.bpp = mode.bpp;
    candidate.length = (size_t)mode.pitch * mode.height;
    candidate.red_position = mode.red_position;
    candidate.red_size = mode.red_size;
    candidate.green_position = mode.green_position;
    candidate.green_size = mode.green_size;
    candidate.blue_position = mode.blue_position;
    candidate.blue_size = mode.blue_size;
    candidate.window_number = mode.window_number;
    candidate.window_size = (uint32_t)mode.window_size_kb * 1024u;
    candidate.window_granularity = (uint32_t)mode.window_granularity_kb * 1024u;
    candidate.physical_address = mode.window_size_kb
                                 ? (uintptr_t)mode.window_segment << 4
                                 : mode.physical_address;
    candidate.address = (volatile uint8_t*)candidate.physical_address;
    if (candidate.valid && map_framebuffer(&candidate, 1) != 0) {
        failure = MODE_MAPPING_FAILED;
        goto fail_alloc;
    }

    uint16_t request = mode.number | (mode.window_size_kb ? 0u : 0x4000u);
    vbe_mode_t actual;
    display_snapshot_t vga = {0};
    int mode_result = candidate.valid ? vbe_set_hardware_mode(request) : restore_hardware(&vga);
    if (mode_result != 0) failure = MODE_SET_FAILED;
    if (mode_result == 0 && candidate.valid &&
        (vbe_query_mode(mode.number, &actual) != 0 || !same_vbe_mode(&mode, &actual))) {
        mode_result = -1;
        failure = MODE_VERIFY_FAILED;
    }
    if (mode_result == 0 && candidate.valid &&
        (mode.memory_model == 3 || mode.memory_model == 4) &&
        set_mode_palette(mode.memory_model, mode.dac_compatible) != 0) {
        mode_result = -1;
        failure = MODE_PALETTE_FAILED;
    }
    if (mode_result == 0 && mode.window_size_kb &&
        candidate.length > candidate.window_size) {
        uint32_t last_bank = (uint32_t)((candidate.length - 1) /
                                        candidate.window_granularity);
        if (last_bank > UINT16_MAX ||
            vbe_set_window(candidate.window_number, (uint16_t)last_bank) != 0 ||
            vbe_set_window(candidate.window_number, 0) != 0) {
            mode_result = -1;
            failure = MODE_BANK_FAILED;
        }
    }
    if (mode_result == 0 && candidate.valid && mode.memory_model == 3 &&
        validate_planar_access(&candidate) != 0) {
        mode_result = -1;
        failure = MODE_PLANAR_FAILED;
    }
    if (mode_result == 0 && candidate.valid && mode.memory_model == 3)
        set_planar_palette_mapping();
    if (mode_result != 0) {
        int restored = restore_hardware(&saved);
        framebuffer.window_valid = 0;
        reset_display_geometry(saved.width, saved.height);
        release_framebuffer_pages(&candidate, &saved.framebuffer);
        kfree(new_back);
        kfree(new_front);
        if (new_colors) kfree(new_colors);
        kfree((void*)new_x0);
        kfree((void*)new_x1);
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return restored == 0 ? failure : -2;
    }

    previous_display = saved;
    display_pending = 1;
    framebuffer = candidate;
    framebuffer_mapped = candidate.valid;
    vga_set_framebuffer_console(candidate.valid);
    backbuffer = new_back;
    frontbuffer = new_front;
    gfx_colorbuffer = new_colors;
    gfx_dirty_row_x0 = new_x0;
    gfx_dirty_row_x1 = new_x1;
    reset_display_geometry(mode.width, mode.height);
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return 0;

fail_alloc:
    if (new_back) kfree(new_back);
    if (new_front) kfree(new_front);
    if (new_colors) kfree(new_colors);
    if (new_x0) kfree((void*)new_x0);
    if (new_x1) kfree((void*)new_x1);
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return failure;
}

int gfx_vbe_confirm_mode(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    if (!display_pending) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }
    release_framebuffer_pages(&previous_display.framebuffer, &framebuffer);
    kfree(previous_display.back);
    kfree(previous_display.front);
    if (previous_display.colors) kfree(previous_display.colors);
    kfree((void*)previous_display.row_x0);
    kfree((void*)previous_display.row_x1);
    display_pending = 0;
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return 0;
}

int gfx_vbe_revert_mode(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    if (!display_pending || restore_hardware(&previous_display) != 0) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }
    framebuffer_t discarded = framebuffer;
    uint8_t *old_back = backbuffer, *old_front = frontbuffer;
    uint32_t *old_colors = gfx_colorbuffer;
    volatile int *old_x0 = gfx_dirty_row_x0, *old_x1 = gfx_dirty_row_x1;
    framebuffer = previous_display.framebuffer;
    vga_set_framebuffer_console(framebuffer.valid);
    framebuffer.window_valid = 0;
    framebuffer.error = 0;
    framebuffer_mapped = previous_display.mapped;
    backbuffer = previous_display.back;
    frontbuffer = previous_display.front;
    gfx_colorbuffer = previous_display.colors;
    gfx_dirty_row_x0 = previous_display.row_x0;
    gfx_dirty_row_x1 = previous_display.row_x1;
    reset_display_geometry(previous_display.width, previous_display.height);
    release_framebuffer_pages(&discarded, &framebuffer);
    kfree(old_back);
    kfree(old_front);
    if (old_colors) kfree(old_colors);
    kfree((void*)old_x0);
    kfree((void*)old_x1);
    display_pending = 0;
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return 0;
}

int gfx_vbe_mode_pending(void) {
    return display_pending;
}

unsigned gfx_physical_depth(void) {
    return framebuffer.valid ? framebuffer.bpp : 4;
}

void gfx_disable_boot_framebuffer(void) {
    framebuffer.valid = 0;
    framebuffer_mapped = 0;
    gfx_width = WM_DEFAULT_SCREEN_WIDTH;
    gfx_height = WM_DEFAULT_SCREEN_HEIGHT;
    gfx_clip_x0 = gfx_clip_y0 = 0;
    gfx_clip_x1 = gfx_width;
    gfx_clip_y1 = gfx_height;
    vga_write_regs(mode_80x25_text);
}

uint8_t gfx_palette[16][3] = { // osLET custom
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

void gfx_set_palette_data(const uint8_t palette[16][3]) {
    memcpy_s(gfx_palette, palette, sizeof(gfx_palette));
    gfx_load_palette();
    full_redraw = 1;
}

static void wait_vretrace(void) {
    while (inb(0x3DA) & 0x08);
    while (!(inb(0x3DA) & 0x08));
}

void gfx_load_palette(void) {
    if (framebuffer.valid) {
        if (framebuffer.memory_model == 3 || framebuffer.memory_model == 4) {
            if (set_mode_palette(framebuffer.memory_model, framebuffer.dac_compatible) != 0)
                framebuffer.error = 1;
            if (framebuffer.memory_model == 3) set_planar_palette_mapping();
            framebuffer.window_valid = 0;
        }
        return;
    }
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
    if (gfx_physical_depth() > 4 && !gfx_colorbuffer) {
        gfx_colorbuffer = kmalloc((size_t)GFX_WIDTH * GFX_HEIGHT * sizeof(*gfx_colorbuffer));
        if (!gfx_colorbuffer) {
            printf("Graphics: Failed to allocate colour buffer\n");
            return;
        }
    }
    if (!gfx_dirty_row_x0) {
        gfx_dirty_row_x0 = kmalloc(sizeof(*gfx_dirty_row_x0) * (size_t)GFX_HEIGHT);
        if (!gfx_dirty_row_x0) {
            printf("Graphics: Failed to allocate dirty-row state\n");
            return;
        }
    }
    if (!gfx_dirty_row_x1) {
        gfx_dirty_row_x1 = kmalloc(sizeof(*gfx_dirty_row_x1) * (size_t)GFX_HEIGHT);
        if (!gfx_dirty_row_x1) {
            printf("Graphics: Failed to allocate dirty-row state\n");
            return;
        }
    }
    memset_s(backbuffer, 0, GFX_BUFFER_SIZE);
    if (gfx_colorbuffer)
        for (size_t i = 0; i < (size_t)GFX_WIDTH * GFX_HEIGHT; ++i)
            gfx_colorbuffer[i] = 0x80000000u;

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

int gfx_enter_textmode(const uint8_t palette[16][3]) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    if (!palette || !graphics_active || display_pending || text_suspended) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }

    if (framebuffer.valid && framebuffer_mapped) {
        display_snapshot_t saved = {
            .framebuffer = framebuffer, .mapped = framebuffer_mapped,
            .width = gfx_width, .height = gfx_height
        };
        if (find_snapshot_mode(&saved) != 0) {
            __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
            return -1;
        }
        if (vbe_bios_text_mode() != 0) {
            int restored = restore_hardware(&saved);
            framebuffer.window_valid = 0;
            full_redraw = 1;
            __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
            return restored == 0 ? -1 : -2;
        }
        text_display = saved;
        text_suspended = 1;
    } else {
        vga_write_regs(mode_80x25_text);
    }
    memcpy_s(text_palette, palette, sizeof(text_palette));
    graphics_active = 0;
    vga_set_framebuffer_console(0);
    vga_reset_textmode();
    vga_set_text_palette(text_palette);
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return 0;
}

int gfx_enter_mode(void) {
    if (text_suspended) {
        uint32_t flags;
        __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
        if (restore_hardware(&text_display) != 0) {
            int text_ok = vbe_bios_text_mode();
            if (text_ok == 0) {
                vga_reset_textmode();
                vga_set_text_palette(text_palette);
            }
            __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
            return text_ok == 0 ? -1 : -2;
        }
        framebuffer.window_valid = 0;
        framebuffer.error = 0;
        vga_set_framebuffer_console(1);
        text_suspended = 0;
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    }
    if (framebuffer.valid && framebuffer_mapped) {
        if (!backbuffer || !gfx_dirty_row_x0 || !gfx_dirty_row_x1 ||
            (gfx_physical_depth() > 4 && !gfx_colorbuffer)) gfx_init();
        if (!backbuffer || !gfx_dirty_row_x0 || !gfx_dirty_row_x1 ||
            (gfx_physical_depth() > 4 && !gfx_colorbuffer)) return -1;
        if (!framebuffer.window_size) {
            for (size_t i = 0; i < framebuffer.length; ++i)
                framebuffer.address[i] = 0;
        }
    } else {
        gfx_width = WM_DEFAULT_SCREEN_WIDTH;
        gfx_height = WM_DEFAULT_SCREEN_HEIGHT;
        vga_write_regs(mode_640x480x16);
        gfx_load_palette();
        outb(0x3C4, 0x02);
        outb(0x3C5, 0x0F);
        {
            int vram_bytes = (GFX_WIDTH / 8) * GFX_HEIGHT;
            volatile uint8_t *v = GFX_VRAM;
            for (int i = 0; i < vram_bytes; i++) v[i] = 0;
        }
    }
    graphics_active = 1;
    if (!backbuffer || !gfx_dirty_row_x0 || !gfx_dirty_row_x1) gfx_init();
    if (!backbuffer || !gfx_dirty_row_x0 || !gfx_dirty_row_x1) {
        graphics_active = 0;
        if (!framebuffer.valid) {
            vga_write_regs(mode_80x25_text);
            vga_reset_textmode();
        }
        return -1;
    }
    if (frontbuffer) memset_s(frontbuffer, 0, GFX_BUFFER_SIZE);
    full_redraw = 1;
    dirty_y0 = 0;
    dirty_y1 = GFX_HEIGHT - 1;
    return 0;
}

void gfx_exit_mode(void) {
    graphics_active = 0;
    if (framebuffer.valid && framebuffer_mapped) {
        vga_clear();
        return;
    }
    vga_write_regs(mode_80x25_text);
    vga_reset_textmode();
}

int gfx_shutdown_console(void) {
    if (framebuffer.valid && framebuffer_mapped) {
        if (vbe_bios_init() != 0 || vbe_bios_text_mode() != 0)
            return -1;
    } else {
        vga_write_regs(mode_80x25_text);
    }
    graphics_active = 0;
    vga_set_framebuffer_console(0);
    vga_set_color(0, 7);
    vga_reset_textmode();
    return 0;
}

int gfx_is_active(void) {
    return graphics_active;
}

void gfx_clear(uint8_t color) {
    if (!backbuffer) return;

    if (gfx_clip_enabled) {
        gfx_fillrect(gfx_clip_x0, gfx_clip_y0,
                     gfx_clip_x1 - gfx_clip_x0,
                     gfx_clip_y1 - gfx_clip_y0, color);
        return;
    }
    
    GFX_LOCK();
    uint8_t pattern = (color & 0x0F) | ((color & 0x0F) << 4);
    memset_s(backbuffer, pattern, GFX_BUFFER_SIZE);
    if (gfx_colorbuffer)
        for (size_t i = 0; i < (size_t)GFX_WIDTH * GFX_HEIGHT; ++i)
            gfx_colorbuffer[i] = 0x80000000u | (color & 0x0F);
    full_redraw = 1;
    GFX_UNLOCK();
}

int gfx_has_pending_updates(void) {
    return full_redraw || dirty_y1 >= dirty_y0;
}

void gfx_swap_buffers(void) {
    if (!backbuffer) return;
    
    GFX_LOCK();

    if (!full_redraw && dirty_y1 < dirty_y0) {
        GFX_UNLOCK();
        return;
    }

    int y0, y1;
    
    if (full_redraw) {
        y0 = 0;
        y1 = GFX_HEIGHT - 1;
    } else {
        y0 = dirty_y0;
        y1 = dirty_y1;
    }
    
    if (framebuffer.valid && framebuffer_mapped && framebuffer.memory_model != 3) {
        uint32_t row_bytes = (GFX_WIDTH + 1u) / 2u;
        uint32_t colors[16];
        uint32_t bytes = framebuffer.bpp / 8u;
        if (framebuffer.memory_model != 4) {
            prepare_rgb_components();
            for (int i = 0; i < 16; ++i) colors[i] = framebuffer_color((uint8_t)i);
        }
        for (int y = y0; y <= y1; y++) {
            int row_x0 = full_redraw ? 0 : gfx_dirty_row_x0[y];
            int row_x1 = full_redraw ? GFX_WIDTH - 1 : gfx_dirty_row_x1[y];
            if (row_x1 < row_x0) continue;
            uint32_t row_offset = (uint32_t)y * row_bytes;
            size_t offset = (size_t)y * framebuffer.pitch + (size_t)row_x0 * bytes;
            volatile uint8_t *pixel = framebuffer.window_size ? NULL :
                                      framebuffer.address + offset;
            const uint32_t *row_colors = gfx_colorbuffer ?
                gfx_colorbuffer + (size_t)y * GFX_WIDTH : NULL;
            for (int x = row_x0; x <= row_x1; ++x) {
                uint32_t source;
                if (row_colors) {
                    source = row_colors[x];
                } else {
                    uint8_t packed = backbuffer[row_offset + (uint32_t)x / 2u];
                    uint8_t color = (x & 1) ? packed & 0x0F : packed >> 4;
                    source = 0x80000000u | color;
                }
                if (framebuffer.memory_model == 4) {
                    if (framebuffer.window_size) {
                        if (!pixel || offset - framebuffer.window_start >= framebuffer.window_size) {
                            pixel = window_address(offset);
                            if (!pixel) break;
                        }
                        ++offset;
                    }
                    *pixel++ = (source & 0x80000000u) ? (uint8_t)source :
                               framebuffer_index(source, x, y);
                } else {
                    uint32_t rgb = (source & 0x80000000u) ?
                                   colors[source & 0x0f] : framebuffer_rgb(source);
                    if (!framebuffer.window_size) {
                        *pixel++ = (uint8_t)rgb;
                        *pixel++ = (uint8_t)(rgb >> 8);
                        if (bytes > 2) *pixel++ = (uint8_t)(rgb >> 16);
                        if (bytes > 3) *pixel++ = (uint8_t)(rgb >> 24);
                    } else {
                        for (uint32_t byte = 0; byte < bytes; ++byte) {
                            if (!pixel || offset - framebuffer.window_start >= framebuffer.window_size) {
                                pixel = window_address(offset);
                                if (!pixel) break;
                            }
                            ++offset;
                            *pixel++ = (uint8_t)(rgb >> (byte * 8));
                        }
                    }
                    if (framebuffer.error) break;
                }
            }
            if (frontbuffer) {
                int first_byte = row_x0 / 2;
                int last_byte = row_x1 / 2;
                for (int byte = first_byte; byte <= last_byte; ++byte)
                    frontbuffer[row_offset + (uint32_t)byte] =
                        backbuffer[row_offset + (uint32_t)byte];
            }
        }
        if (!framebuffer.error) reset_dirty();
        GFX_UNLOCK();
        return;
    }

    if (!framebuffer.valid) {
        volatile uint8_t *vram = GFX_VRAM;
        static uint8_t planes[4][80 * 480];
        static uint8_t changed[80 * 480];
        uint8_t changed_rows[480] = {0};
        int any = 0;

        /* Prepare VGA plane bytes before retrace so presentation starts promptly. */
        for (int y = y0; y <= y1; ++y) {
            int x0 = full_redraw ? 0 : gfx_dirty_row_x0[y];
            int x1 = full_redraw ? GFX_WIDTH - 1 : gfx_dirty_row_x1[y];
            if (x1 < x0) continue;
            x0 &= ~7;
            x1 = (x1 + 8) & ~7;
            if (x1 > GFX_WIDTH) x1 = GFX_WIDTH;
            uint32_t row = (uint32_t)y * (GFX_WIDTH / 2);
            for (int x = x0; x < x1; x += 8) {
                uint32_t offset = row + (uint32_t)x / 2u;
                uint8_t b0 = backbuffer[offset];
                uint8_t b1 = backbuffer[offset + 1];
                uint8_t b2 = backbuffer[offset + 2];
                uint8_t b3 = backbuffer[offset + 3];
                uint32_t pos = (uint32_t)y * 80u + (uint32_t)x / 8u;
                changed[pos] = full_redraw || !frontbuffer ||
                    frontbuffer[offset] != b0 || frontbuffer[offset + 1] != b1 ||
                    frontbuffer[offset + 2] != b2 || frontbuffer[offset + 3] != b3;
                if (!changed[pos]) continue;
                any = 1;
                changed_rows[y] = 1;
                for (int plane = 0; plane < 4; ++plane) {
                    planes[plane][pos] =
                        (uint8_t)((plane_pair_table[plane][b0] << 6) |
                                  (plane_pair_table[plane][b1] << 4) |
                                  (plane_pair_table[plane][b2] << 2) |
                                  plane_pair_table[plane][b3]);
                }
            }
        }
        if (!any) {
            reset_dirty();
            GFX_UNLOCK();
            return;
        }

        wait_vretrace();
        outb(0x3c4, 0x02);
        for (int y = y0; y <= y1; ++y) {
            if (!changed_rows[y]) continue;
            int x0 = full_redraw ? 0 : gfx_dirty_row_x0[y];
            int x1 = full_redraw ? GFX_WIDTH - 1 : gfx_dirty_row_x1[y];
            x0 &= ~7;
            x1 = (x1 + 8) & ~7;
            if (x1 > GFX_WIDTH) x1 = GFX_WIDTH;
            uint32_t row = (uint32_t)y * 80u;
            for (uint8_t plane = 0; plane < 4; ++plane) {
                outb(0x3c5, (uint8_t)(1u << plane));
                for (uint32_t pos = row + (unsigned)x0 / 8u;
                     pos < row + (unsigned)x1 / 8u; ++pos)
                    if (changed[pos])
                        vram[pos] = planes[plane][pos];
            }
        }
        outb(0x3c5, 0x0f);
        if (frontbuffer) {
            for (int y = y0; y <= y1; ++y) {
                if (!changed_rows[y]) continue;
                int x0 = full_redraw ? 0 : gfx_dirty_row_x0[y];
                int x1 = full_redraw ? GFX_WIDTH - 1 : gfx_dirty_row_x1[y];
                x0 &= ~7;
                x1 = (x1 + 8) & ~7;
                if (x1 > GFX_WIDTH) x1 = GFX_WIDTH;
                size_t offset = (size_t)y * (GFX_WIDTH / 2) + (unsigned)x0 / 2u;
                memcpy_s(frontbuffer + offset, backbuffer + offset, (size_t)(x1 - x0) / 2u);
            }
        }
        reset_dirty();
        GFX_UNLOCK();
        return;
    }
    volatile uint8_t* vram = GFX_VRAM;

    /* Keep sequencer register 2 selected while updating plane masks. */
    outb(0x3C4, 0x02);

    /* Use each row's actual span instead of the full-screen damage bounding box. */
    for (int y = y0; y <= y1; y++) {
        int row_x0 = full_redraw ? 0 : gfx_dirty_row_x0[y];
        int row_x1 = full_redraw ? GFX_WIDTH - 1 : gfx_dirty_row_x1[y];
        if (row_x1 < row_x0) continue;

        int x0_aligned = row_x0 & ~7;
        int x1_aligned = (row_x1 + 8) & ~7;
        if (x1_aligned > GFX_WIDTH) x1_aligned = GFX_WIDTH;

        uint32_t bb_row = y * (GFX_WIDTH / 2);
        uint32_t vram_row = framebuffer.valid && framebuffer.memory_model == 3
                            ? (uint32_t)y * framebuffer.pitch : (uint32_t)y * 80u;

        for (int x = x0_aligned; x < x1_aligned; x += 8) {
            if (x + 8 > GFX_WIDTH) {
                for (int pixel = x; pixel < GFX_WIDTH; ++pixel)
                    framebuffer_put_index(pixel, y, getpixel_raw(pixel, y));
                break;
            }
            uint32_t bb_offset = bb_row + (x / 2);
            uint8_t b0 = backbuffer[bb_offset];
            uint8_t b1 = backbuffer[bb_offset + 1];
            uint8_t b2 = backbuffer[bb_offset + 2];
            uint8_t b3 = backbuffer[bb_offset + 3];

            if (!full_redraw && frontbuffer &&
                frontbuffer[bb_offset] == b0 && frontbuffer[bb_offset + 1] == b1 &&
                frontbuffer[bb_offset + 2] == b2 && frontbuffer[bb_offset + 3] == b3) {
                continue;
            }

            volatile uint8_t *destination = framebuffer.valid && framebuffer.memory_model == 3
                                            ? window_address(vram_row + (uint32_t)x / 8u)
                                            : vram + vram_row + (uint32_t)x / 8u;
            if (!destination) break;
            outb(0x3C4, 0x02);
            for (uint8_t plane = 0; plane < 4; plane++) {
                outb(0x3C5, 1 << plane);
                uint8_t plane_byte = (uint8_t)(plane_pair_table[plane][b0] << 6)
                                   | (uint8_t)(plane_pair_table[plane][b1] << 4)
                                   | (uint8_t)(plane_pair_table[plane][b2] << 2)
                                   | (uint8_t)(plane_pair_table[plane][b3] << 0);

                *destination = plane_byte;
            }

            if (frontbuffer) {
                frontbuffer[bb_offset] = b0;
                frontbuffer[bb_offset + 1] = b1;
                frontbuffer[bb_offset + 2] = b2;
                frontbuffer[bb_offset + 3] = b3;
            }
        }
    }
    
    outb(0x3C5, 0x0F);

    if (!framebuffer.error) reset_dirty();
    GFX_UNLOCK();
}
uint8_t* gfx_get_backbuffer(void) {
    return backbuffer;
}
