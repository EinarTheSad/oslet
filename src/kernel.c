#include "console.h"
#include "vga.h"
#include "keyboard.h"

extern void idt_init(void);
extern void pic_remap(void);
extern void keyboard_init(void);

void kmain(void) {
    vga_use_as_console();
    vga_set_color(0,7);
    vga_clear();
    idt_init();
    pic_remap();
    keyboard_init();

    __asm__ volatile ("sti"); /* start listening to interrupts */

    printf("OSlet has booted. VGA width=%d height=%d\n", 80, 25);
    printf("hex %x no prefix, pointer %p\n\n", 0xdeadbeefu, (void*)0xB8000);

    char line[128];
    for (;;) {
        printf("oslet> ");
        size_t n = kbd_getline(line, sizeof(line));
        /* TODO: command handler */
        if (STREQ(line,"help")) printf("There is no help for you.\n");
    }
}
