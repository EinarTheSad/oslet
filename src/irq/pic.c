#include <stdint.h>
#include "io.h"

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1
#define PIC_EOI   0x20

void pic_remap(void) {
    uint8_t a1 = inb(PIC1_DATA);
    uint8_t a2 = inb(PIC2_DATA);

    outb(PIC1_CMD, 0x11);
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20);
    outb(PIC2_DATA, 0x28);
    outb(PIC1_DATA, 0x04);
    outb(PIC2_DATA, 0x02);
    outb(PIC1_DATA, 0x01);
    outb(PIC2_DATA, 0x01);

    outb(PIC1_DATA, a1);
    outb(PIC2_DATA, a2);

    /* Unmask IRQ0 (timer), IRQ1 (keyboard), IRQ2 (cascade), IRQ5 (sound) */
    uint8_t new_a1 = a1 & ~((1 << 0) | (1 << 1) | (1 << 2) | (1 << 5));
    outb(PIC1_DATA, new_a1);
    
    /* Unmask IRQ12 (mouse) on slave PIC */
    uint8_t new_a2 = a2 & ~(1 << 4);
    outb(PIC2_DATA, new_a2);
}

void pic_send_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
