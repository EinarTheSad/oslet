#include "console.h"
#include "vga.h"
#include "keyboard.h"
#include "early_alloc.h"
#include "io.h"
#include "pmm.h"

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
    pmm_init_from_multiboot((uint32_t)multiboot_info_ptr);

    __asm__ volatile ("sti"); /* start listening to interrupts */

    printf("OSlet has booted. VGA width=%d height=%d\n", 80, 25);
    printf("Memory allocation pointer %p\n\n", mm_early_alloc(4096, 4096));

    uintptr_t f1 = pmm_alloc_frame();
    uintptr_t f2 = pmm_alloc_frame();
    printf("pmm alloc frames: %p %p\n", (void*)f1, (void*)f2);
    pmm_free_frame(f1);
    uintptr_t f3 = pmm_alloc_frame();
    printf("after free, new frame: %p (should be %p)\n", (void*)f3, (void*)f1);

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
