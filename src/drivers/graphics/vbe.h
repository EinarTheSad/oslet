#pragma once

#include <stddef.h>
#include <stdint.h>

#define VBE_VGA_MODE 0xffffu

typedef struct {
    uint16_t number;
    uint16_t width, height;
    uint8_t bpp, memory_model, vga_compatible, dac_compatible;
    uint32_t pitch, physical_address;
    uint8_t red_position, red_size;
    uint8_t green_position, green_size;
    uint8_t blue_position, blue_size;
    uint16_t window_segment, window_size_kb, window_granularity_kb;
    uint8_t window_number;
} vbe_mode_t;

int vbe_discover_modes(void);
int vbe_parse_mode_info(uint16_t number, uint16_t version,
                        const uint8_t info[256], vbe_mode_t *mode);
int vbe_query_mode(uint16_t number, vbe_mode_t *mode);
size_t vbe_mode_count(void);
const vbe_mode_t *vbe_mode_at(size_t index);
int vbe_get_hardware_mode(uint16_t *mode);
int vbe_set_hardware_mode(uint16_t mode);
int vbe_set_window(uint8_t window, uint16_t bank);
int vbe_set_palette(const uint8_t palette[16][3], int vga_compatible);
int vbe_set_palette_entries(const uint8_t palette[][3], unsigned count, int vga_compatible);
