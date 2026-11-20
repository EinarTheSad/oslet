#include "vbe.h"
#include "../../arch/vbe_bios.h"
#include "../../mem/paging.h"
#include "../../console.h"
#include "../../irq/io.h"

#define VBE_LIST_LIMIT 512
#define VBE_MODE_LIMIT 64
#define VBE_FAR_LIMIT 0x10fff0u

static vbe_mode_t modes[VBE_MODE_LIMIT];
static size_t mode_count;
static uint16_t controller_version;

static uint16_t word_at(const uint8_t *data, size_t offset) {
    return (uint16_t)(data[offset] | ((uint16_t)data[offset + 1] << 8));
}

static uint32_t dword_at(const uint8_t *data, size_t offset) {
    return (uint32_t)word_at(data, offset) |
           ((uint32_t)word_at(data, offset + 2) << 16);
}

static int target_size(uint16_t width, uint16_t height) {
    return (width == 640 && height == 480) ||
           (width == 800 && height == 600) ||
           (width == 1024 && height == 768) ||
           (width == 1280 && height == 960) ||
           (width == 1280 && height == 720) ||
           (width == 1366 && height == 768) ||
           (width == 1920 && height == 1080);
}

static int valid_rgb(vbe_mode_t *mode) {
    if (!mode->red_size || mode->red_size > 8 ||
        !mode->green_size || mode->green_size > 8 ||
        !mode->blue_size || mode->blue_size > 8 ||
        (unsigned)mode->red_position + mode->red_size > mode->bpp ||
        (unsigned)mode->green_position + mode->green_size > mode->bpp ||
        (unsigned)mode->blue_position + mode->blue_size > mode->bpp)
        return 0;
    uint32_t red = ((1u << mode->red_size) - 1u) << mode->red_position;
    uint32_t green = ((1u << mode->green_size) - 1u) << mode->green_position;
    uint32_t blue = ((1u << mode->blue_size) - 1u) << mode->blue_position;
    return !(red & green) && !(red & blue) && !(green & blue);
}

int vbe_parse_mode_info(uint16_t number, uint16_t version,
                        const uint8_t data[256], vbe_mode_t *mode) {
    if (!data || !mode) return -1;
    uint16_t attributes = word_at(data, 0);
    uint16_t width = word_at(data, 18);
    uint16_t height = word_at(data, 20);
    uint8_t bpp = data[25];
    uint8_t model = data[27];
    if ((attributes & 0x19u) != 0x19u || !target_size(width, height))
        return -1;

    memset_s(mode, 0, sizeof(*mode));
    mode->number = number;
    mode->width = width;
    mode->height = height;
    mode->bpp = bpp;
    mode->memory_model = model;
    mode->vga_compatible = (attributes & 0x20u) != 0;

    if (bpp == 4 && model == 3 && data[24] == 4) {
        if (!mode->vga_compatible) return -1;
        uint8_t window;
        if ((data[2] & 7u) == 7u) window = 0;
        else if ((data[3] & 7u) == 7u) window = 1;
        else return -1;
        mode->window_number = window;
        mode->window_segment = word_at(data, window ? 10 : 8);
        mode->window_granularity_kb = word_at(data, 4);
        mode->window_size_kb = word_at(data, 6);
        mode->pitch = word_at(data, 16);
        uint32_t window_start = (uint32_t)mode->window_segment << 4;
        uint32_t window_bytes = (uint32_t)mode->window_size_kb * 1024u;
        if (mode->pitch < (width + 7u) / 8u ||
            !mode->window_granularity_kb ||
            mode->window_granularity_kb > mode->window_size_kb ||
            window_start < 0xa0000u || window_start >= 0xc0000u ||
            window_bytes > 0xc0000u - window_start)
            return -1;
        return 0;
    }

    if (!(attributes & 0x80u) || data[24] != 1 ||
        !((bpp == 8 && model == 4) ||
          ((bpp == 16 || bpp == 24 || bpp == 32) && model == 6)))
        return -1;
    mode->physical_address = dword_at(data, 40);
    mode->pitch = version >= 0x0300 ? word_at(data, 50) : word_at(data, 16);
    uint64_t length = (uint64_t)mode->pitch * height;
    if (!mode->physical_address || mode->pitch < (uint32_t)width * (bpp / 8u) ||
        !length || length > 16u * 1024u * 1024u ||
        (uint64_t)mode->physical_address + length > 0x100000000ull)
        return -1;
    if (model == 6) {
        size_t rgb = version >= 0x0300 ? 54 : 31;
        mode->red_size = data[rgb];
        mode->red_position = data[rgb + 1];
        mode->green_size = data[rgb + 2];
        mode->green_position = data[rgb + 3];
        mode->blue_size = data[rgb + 4];
        mode->blue_position = data[rgb + 5];
        if (!valid_rgb(mode)) return -1;
    }
    return 0;
}

static int copy_mode_numbers(uint32_t far_pointer, uint16_t *numbers, size_t *count) {
    uintptr_t address = ((uintptr_t)(far_pointer >> 16) << 4) +
                        (uintptr_t)(far_pointer & 0xffffu);
    if (address < 0x500u) return -1;
    for (size_t i = 0; i < VBE_LIST_LIMIT; ++i, address += 2) {
        if (address >= VBE_FAR_LIMIT - 1 ||
            !paging_is_mapped(address & ~(uintptr_t)0xfffu) ||
            !paging_is_mapped((address + 1) & ~(uintptr_t)0xfffu))
            return -1;
        uint16_t number = *(volatile uint16_t*)address;
        if (number == 0xffffu) {
            *count = i;
            return 0;
        }
        numbers[i] = number;
    }
    return -1;
}

int vbe_discover_modes(void) {
    uint8_t controller[512] = { 'V', 'B', 'E', '2' };
    uint8_t info[256];
    uint16_t numbers[VBE_LIST_LIMIT];
    size_t number_count;
    vbe_bios_regs_t regs = { .ax = 0x4f00 };
    mode_count = 0;
    if (vbe_bios_init() != 0 ||
        vbe_bios_call(&regs, controller, sizeof(controller)) != 0 ||
        controller[0] != 'V' || controller[1] != 'E' ||
        controller[2] != 'S' || controller[3] != 'A')
        return -1;
    uint16_t version = word_at(controller, 4);
    if (version < 0x0200 ||
        copy_mode_numbers(dword_at(controller, 14), numbers, &number_count) != 0)
        return -1;
    controller_version = version;

    for (size_t i = 0; i < number_count; ++i) {
        memset_s(info, 0, sizeof(info));
        regs.ax = 0x4f01;
        regs.cx = numbers[i];
        if (vbe_bios_call(&regs, info, sizeof(info)) != 0) continue;
        vbe_mode_t mode;
        if (vbe_parse_mode_info(numbers[i], version, info, &mode) != 0) continue;
        if (mode_count == VBE_MODE_LIMIT) {
            mode_count = 0;
            return -1;
        }
        modes[mode_count++] = mode;
    }
    return mode_count ? (int)mode_count : -1;
}

size_t vbe_mode_count(void) {
    return mode_count;
}

const vbe_mode_t *vbe_mode_at(size_t index) {
    return index < mode_count ? &modes[index] : NULL;
}

int vbe_query_mode(uint16_t number, vbe_mode_t *mode) {
    if (!mode || controller_version < 0x0200) return -1;
    uint8_t info[256] = {0};
    vbe_bios_regs_t regs = { .ax = 0x4f01, .cx = number };
    if (vbe_bios_call(&regs, info, sizeof(info)) != 0) return -1;
    return vbe_parse_mode_info(number, controller_version, info, mode);
}

int vbe_get_hardware_mode(uint16_t *mode) {
    if (!mode) return -1;
    vbe_bios_regs_t regs = { .ax = 0x4f03 };
    if (vbe_bios_call(&regs, NULL, 0) != 0) return -1;
    *mode = regs.bx;
    return 0;
}

int vbe_set_hardware_mode(uint16_t mode) {
    vbe_bios_regs_t regs = { .ax = 0x4f02, .bx = mode };
    if (vbe_bios_call(&regs, NULL, 0) != 0) return -1;
    uint16_t current;
    if (vbe_get_hardware_mode(&current) != 0 ||
        (current & 0x7fffu) != (mode & 0x7fffu)) return -1;
    return 0;
}

int vbe_set_window(uint8_t window, uint16_t bank) {
    if (window > 1) return -1;
    vbe_bios_regs_t regs = { .ax = 0x4f05, .bx = window, .dx = bank };
    return vbe_bios_call(&regs, NULL, 0);
}

int vbe_set_palette_entries(const uint8_t palette[][3], unsigned count, int vga_compatible) {
    if (!palette || !count || count > 256) return -1;
    uint8_t entries[256 * 4];
    for (unsigned i = 0; i < count; ++i) {
        entries[i * 4] = palette[i][2] >> 2;
        entries[i * 4 + 1] = palette[i][1] >> 2;
        entries[i * 4 + 2] = palette[i][0] >> 2;
        entries[i * 4 + 3] = 0;
    }
    vbe_bios_regs_t regs = { .ax = 0x4f09, .cx = (uint16_t)count };
    if (vbe_bios_call(&regs, entries, count * 4u) == 0) return 0;
    if (!vga_compatible) return -1;
    for (unsigned i = 0; i < count; ++i) {
        outb(0x3c8, (uint8_t)i);
        outb(0x3c9, palette[i][0] >> 2);
        outb(0x3c9, palette[i][1] >> 2);
        outb(0x3c9, palette[i][2] >> 2);
    }
    return 0;
}

int vbe_set_palette(const uint8_t palette[16][3], int vga_compatible) {
    return vbe_set_palette_entries(palette, 16, vga_compatible);
}
