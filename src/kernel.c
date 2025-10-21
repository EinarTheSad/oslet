#include "osletio.h"

extern int kb_wait_and_echo(void);

void kmain(void) {
    vga_clear();
    kputs("OSlet: tiny kernel alive.\n");
    kputs("Press a key: ");

    kb_wait_and_echo();

    kputs("\nDone. Halting.\n");
    for (;;) __asm__ volatile ("hlt");
}
