#include "osletio.h"

extern int kb_wait_and_echo(void);

void kmain(void) {
    vga_clear();
    kprintf("OSlet has booted. Debug code: %x, current user: %s\n", 0xDEADBEEF, "horse");
    kprintf("\nDebugging hexadecimal values: ");

    /* Simple loop to debug some hex */
    for (unsigned int i = 0; i <= 0xF; i++) {
        kprintf("%x ",i);
    }

    kprintf("\nPress a key: ");

    kb_wait_and_echo();

    kprintf("\nDone. Halting.\n");
    for (;;) __asm__ volatile ("hlt");
}
