#include <stdint.h>
#include "osletio.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  zero;
    uint8_t  type_attr;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_entry idt[256];
static struct idt_ptr idtr;

extern void isr0(void);

static inline void load_idt(void) {
    idtr.limit = sizeof(idt) - 1;
    idtr.base  = (uint32_t)&idt;
    __asm__ volatile ("lidt %0" :: "m"(idtr));
    kprintf("IDT loaded\n");
}

static void idt_set_entry(int num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].type_attr   = flags;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
}

void isr_common_stub(void) {
    kprintf("[EXCEPTION] System halted");
    for (;;) __asm__ volatile ("hlt");
}


void idt_init(void) {
    for (int i = 0; i < 256; ++i) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].zero = 0;
        idt[i].type_attr = 0;
        idt[i].offset_high = 0;
    }

    for (int i = 0; i < 32; ++i) {
        idt_set_entry(i, (uint32_t)isr0, 0x08, 0x8E);
    }

    load_idt();
}
