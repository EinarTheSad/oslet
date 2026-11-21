#include "vbe_bios.h"
#include "../console.h"
#include "../irq/io.h"
#include "../mem/pmm.h"
#include "../drivers/keyboard.h"
#include "../drivers/mouse.h"

#define VBE_LOW_START 0x20000u
#define VBE_LOW_LIMIT 0xa0000u
#define VBE_THUNK_PAGES 8u
#define VBE_BUFFER_OFFSET 4096u
#define VBE_BUFFER_SIZE 4096u

extern uint8_t vbe_thunk_start[], vbe_thunk_end[], vbe_thunk_state[];
extern uint8_t vbe_thunk_gdt[], vbe_thunk_gdtr[];
extern uint8_t vbe_thunk_real_pointer[], vbe_thunk_protected_pointer[];
extern uint8_t vbe_thunk_protected32[], vbe_thunk_physical[];

typedef struct __attribute__((packed)) {
    vbe_bios_regs_t input;
    vbe_bios_regs_t output;
    uint32_t saved_esp, saved_cr0, saved_cr3;
    uint8_t saved_gdtr[6], saved_idtr[6];
    uint16_t saved_ds, saved_es, saved_fs, saved_gs, saved_ss;
} vbe_thunk_state_t;

_Static_assert(sizeof(vbe_bios_regs_t) == 14, "VBE register layout");
_Static_assert(sizeof(vbe_thunk_state_t) == 62, "VBE thunk state layout");

static uintptr_t low_base;
static int bios_busy;

static uint16_t read_bios_word(uintptr_t address) {
    uint16_t value;
    __asm__ volatile("movw (%1), %0" : "=r"(value) : "r"(address) : "memory");
    return value;
}

static uintptr_t relocated(const uint8_t *symbol) {
    return low_base + (uintptr_t)(symbol - vbe_thunk_start);
}

static void set_descriptor_base(uint8_t *descriptor, uintptr_t base) {
    descriptor[2] = (uint8_t)base;
    descriptor[3] = (uint8_t)(base >> 8);
    descriptor[4] = (uint8_t)(base >> 16);
    descriptor[7] = (uint8_t)(base >> 24);
}

static void pic_write(uint16_t port, uint8_t value) {
    outb(port, value);
    outb(0x80, 0);
}

static void pic_set_vectors(uint8_t master, uint8_t slave,
                            uint8_t master_mask, uint8_t slave_mask) {
    pic_write(0x20, 0x11);
    pic_write(0xa0, 0x11);
    pic_write(0x21, master);
    pic_write(0xa1, slave);
    pic_write(0x21, 4);
    pic_write(0xa1, 2);
    pic_write(0x21, 1);
    pic_write(0xa1, 1);
    pic_write(0x21, master_mask);
    pic_write(0xa1, slave_mask);
}

static void recover_ps2_input(uint8_t master_mask, uint8_t slave_mask) {
    for (unsigned i = 0; i < 32; ++i) {
        uint8_t status = inb(0x64);
        if (!(status & 1)) break;
        if (status & 0x20) {
            if ((master_mask & 4) || (slave_mask & 0x10)) break;
            mouse_handle_byte(inb(0x60));
        } else {
            if (master_mask & 2) break;
            keyboard_handle_byte(inb(0x60));
        }
    }
}

int vbe_bios_init(void) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    if (low_base) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return 0;
    }

    size_t code_size = (size_t)(vbe_thunk_end - vbe_thunk_start);
    uintptr_t limit = (uintptr_t)read_bios_word(0x413u) * 1024u;
    uintptr_t ebda = (uintptr_t)read_bios_word(0x40eu) << 4;
    if (limit > VBE_LOW_LIMIT) limit = VBE_LOW_LIMIT;
    if (ebda && ebda < limit) limit = ebda;

    if (code_size > VBE_BUFFER_OFFSET || limit <= VBE_LOW_START) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }
    low_base = pmm_alloc_contiguous(VBE_LOW_START, limit, VBE_THUNK_PAGES);
    if (!low_base) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }

    memcpy_s((void*)low_base, vbe_thunk_start, code_size);
    uint8_t *gdt = (uint8_t*)relocated(vbe_thunk_gdt);
    set_descriptor_base(gdt + 24, low_base);
    set_descriptor_base(gdt + 32, low_base);
    uint32_t value = (uint32_t)relocated(vbe_thunk_gdt);
    memcpy_s((void*)(relocated(vbe_thunk_gdtr) + 2), &value, sizeof(value));
    value = (uint32_t)relocated(vbe_thunk_protected32);
    memcpy_s((void*)relocated(vbe_thunk_protected_pointer), &value, sizeof(value));
    value = (uint32_t)low_base;
    memcpy_s((void*)relocated(vbe_thunk_physical), &value, sizeof(value));
    uint16_t segment = (uint16_t)(low_base >> 4);
    memcpy_s((void*)(relocated(vbe_thunk_real_pointer) + 2), &segment, sizeof(segment));

    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return 0;
}

static int bios_call(vbe_bios_regs_t *regs, void *buffer, size_t buffer_size, int vbe) {
    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    if (!low_base || bios_busy) {
        __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
        return -1;
    }
    bios_busy = 1;
    vbe_thunk_state_t *state = (vbe_thunk_state_t*)relocated(vbe_thunk_state);
    state->input = *regs;
    if (buffer_size) {
        memcpy_s((void*)(low_base + VBE_BUFFER_OFFSET), buffer, buffer_size);
        state->input.es = (uint16_t)((low_base + VBE_BUFFER_OFFSET) >> 4);
        state->input.di = 0;
    }

    uint8_t master_mask = inb(0x21);
    uint8_t slave_mask = inb(0xa1);
    uint8_t cmos_index = inb(0x70);
    outb(0x70, cmos_index | 0x80u);
    pic_set_vectors(0x08, 0x70, 0xfe, 0xff);
    ((void (*)(void))low_base)();
    pic_set_vectors(0x20, 0x28, master_mask, slave_mask);
    recover_ps2_input(master_mask, slave_mask);
    outb(0x70, cmos_index);

    *regs = state->output;
    if (buffer_size)
        memcpy_s(buffer, (const void*)(low_base + VBE_BUFFER_OFFSET), buffer_size);
    bios_busy = 0;
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return !vbe || regs->ax == 0x004f ? 0 : -2;
}

int vbe_bios_call(vbe_bios_regs_t *regs, void *buffer, size_t buffer_size) {
    if (!regs || (regs->ax & 0xff00u) != 0x4f00u ||
        buffer_size > VBE_BUFFER_SIZE || (buffer_size && !buffer)) return -1;
    return bios_call(regs, buffer, buffer_size, 1);
}

static int bios_standard_mode(uint16_t mode) {
    vbe_bios_regs_t regs = { .ax = mode };
    if (bios_call(&regs, NULL, 0, 0) != 0) return -1;
    regs.ax = 0x0f00;
    if (bios_call(&regs, NULL, 0, 0) != 0) return -1;
    return (regs.ax & 0x7fu) == mode &&
           (mode != 3 || (regs.ax >> 8) == 80) ? 0 : -1;
}

int vbe_bios_text_mode(void) {
    return bios_standard_mode(3);
}

int vbe_bios_vga_mode(void) {
    return bios_standard_mode(0x12);
}
