#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint16_t ax, bx, cx, dx, si, di, es;
} vbe_bios_regs_t;

int vbe_bios_init(void);
int vbe_bios_call(vbe_bios_regs_t *regs, void *buffer, size_t buffer_size);
int vbe_bios_text_mode(void);
