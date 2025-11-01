#include "console.h"
#include "vga.h"
#include "keyboard.h"
#include "early_alloc.h"

extern void idt_init(void);
extern void pic_remap(void);
extern void keyboard_init(void);
extern uint8_t __kernel_end;

void kmain(void) {
    vga_use_as_console();
    vga_set_color(0,7);
    vga_clear();
    idt_init();
    pic_remap();
    keyboard_init();
    mm_early_init((uintptr_t)&__kernel_end);

    __asm__ volatile ("sti"); /* start listening to interrupts */

    printf("OSlet has booted. VGA width=%d height=%d\n", 80, 25);
    printf("Memory allocation pointer %p\n\n", mm_early_alloc(4096, 4096));

    char line[128];
    for (;;) {
        printf("oslet> ");
        size_t n = kbd_getline(line, sizeof(line));
        /* TODO: command handler */
        if (STREQ(line,"help")) printf("There is no help for you.\n");
        if (STREQ(line,"except")) {
            printf("I'm going dark.\n");
            __asm__ volatile (
                "xor %%edx, %%edx\n\t"
                "mov $1, %%eax\n\t"
                "div %%edx\n\t"
                :
                :
                : "eax", "edx"
            );         
        }
    }
}
