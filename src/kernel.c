#include "console.h"

extern void idt_init(void);
extern void vga_clear(void);
extern void vga_use_as_console(void);

void kmain(void) {
    idt_init();
    vga_use_as_console();
    vga_clear();

    printf("OSlet has booted. VGA width=%d height=%d\n", 80, 25);
    printf("hex %x no prefix, pointer %p\n", 0xBADC0DEu, (void*)0xB8000);

    for (;;) __asm__ volatile ("hlt");
}
