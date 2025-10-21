#include "osletio.h"

extern int kb_wait_and_echo(void);

void kmain(void) {
    vga_clear();
    kprintf("OSlet: tiny kernel alive. Debug code: %x, current user: %s\n", 0xDEADBEEF, "root");
    kprintf("Press a key: ");

    kb_wait_and_echo();

    kprintf("\nDone. Halting.\n");
    for (;;) __asm__ volatile ("hlt");
}
