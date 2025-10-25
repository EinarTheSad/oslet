#include "osletio.h"

extern void idt_init(void);
// extern int kb_wait_and_echo(void);

void kmain(void) {
    idt_init();
    vga_clear();
    kprintf("OSlet has booted\n\n");

    /* kb_wait_and_echo(); */

    kprintf("\nHalt");
     for (;;) __asm__ volatile ("hlt");
}
