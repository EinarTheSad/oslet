#pragma once

#include <stdint.h>

#define MULTIBOOT_INFO_FLAG_MEMORY   (1u << 0)
#define MULTIBOOT_INFO_FLAG_BOOTDEV  (1u << 1)
#define MULTIBOOT_INFO_FLAG_MMAP     (1u << 6)
#define MULTIBOOT_INFO_FLAG_FRAMEBUF (1u << 12)

#define MULTIBOOT_FRAMEBUFFER_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TEXT    2

typedef struct __attribute__((packed)) {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
    uint32_t drives_length;
    uint32_t drives_addr;
    uint32_t config_table;
    uint32_t boot_loader_name;
    uint32_t apm_table;
    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t framebuffer_bpp;
    uint8_t framebuffer_type;
    uint16_t framebuffer_reserved;
    union {
        struct __attribute__((packed)) {
            uint32_t palette_addr;
            uint16_t palette_num_colors;
        } indexed;
        struct __attribute__((packed)) {
            uint8_t red_field_position;
            uint8_t red_mask_size;
            uint8_t green_field_position;
            uint8_t green_mask_size;
            uint8_t blue_field_position;
            uint8_t blue_mask_size;
        } rgb;
    } color_info;
} multiboot_info_t;

_Static_assert(__builtin_offsetof(multiboot_info_t, color_info) == 112,
               "Multiboot framebuffer colour information must start at byte 112");

typedef struct __attribute__((packed)) {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} multiboot_mmap_entry_t;
