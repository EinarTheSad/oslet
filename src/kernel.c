#include <stdint.h>

extern void putchar(char c);
extern void vga_clear(void);
extern void kmain(void);

void kputs(const char *s) {
    while (*s) putchar(*s++);
}

void kmain(void) {
    vga_clear();
    kputs("OSlet: tiny kernel alive.\n");
    kputs("Press a key: ");

    /* simple keyboard loop handled in keyboard.c — polls an atomic buffer */
    extern int kb_wait_and_echo(void);
    kb_wait_and_echo();

    kputs("\nDone. Halting.\n");
    for (;;) __asm__ volatile ("hlt");
}
