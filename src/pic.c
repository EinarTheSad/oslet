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

    outb(PIC1_CMD, 0x11); // ICW1: init, expect ICW4
    outb(PIC2_CMD, 0x11);
    outb(PIC1_DATA, 0x20); // ICW2: master offset 0x20
    outb(PIC2_DATA, 0x28); // ICW2: slave  offset 0x28
    outb(PIC1_DATA, 0x04); // ICW3: master has a slave at IRQ2
    outb(PIC2_DATA, 0x02); // ICW3: slave identity is 2
    outb(PIC1_DATA, 0x01); // ICW4: 8086 mode
    outb(PIC2_DATA, 0x01);

    // Mask everything first
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    // Unmask only IRQ1 (keyboard) on master
    outb(PIC1_DATA, 0xFF & ~(1 << 1));
    outb(PIC2_DATA, 0xFF);

    (void)a1; (void)a2;
}

void pic_send_eoi(int irq) {
    if (irq >= 8) outb(PIC2_CMD, PIC_EOI);
    outb(PIC1_CMD, PIC_EOI);
}
