#include "console.h"
#include "vga.h"
#include "keyboard.h"
#include "early_alloc.h"
#include "io.h"
#include "pmm.h"
#include "paging.h"

extern void idt_init(void);
extern void pic_remap(void);
extern uint8_t __kernel_end;

void kmain(void) {
    vga_use_as_console();

    idt_init();
    pic_remap();
    keyboard_init();

    mm_early_init((uintptr_t)&__kernel_end);
    pmm_init_from_multiboot((uint32_t)multiboot_info_ptr);

    /* Paging setup */
    __asm__ volatile ("cli");

    uintptr_t kernel_end = (uintptr_t)&__kernel_end;
    uintptr_t map_upto = (kernel_end + 0xFFF) & ~((uintptr_t)0xFFF);
    map_upto += 16 * 1024 * 1024; /* 16 MB mapped */

    if (paging_identity_enable(map_upto) != 0) {
        printf("Paging: FAILED to enable\n");
        for (;;) __asm__ volatile ("hlt");
    }

    __asm__ volatile ("sti");

    printf("Memory allocation pointer %p\n\n", mm_early_alloc(4096, 4096));
    printf("Paging enabled, identity-mapped up to 0x%08x\n", (unsigned)map_upto);

    uintptr_t f1 = pmm_alloc_frame();
    uintptr_t f2 = pmm_alloc_frame();

    printf("PMM allocated frames: %p %p\n", (void*)f1, (void*)f2);
    pmm_free_frame(f1);

    uintptr_t f3 = pmm_alloc_frame();
    printf("After free, new frame: %p (expected %p)\n\n", (void*)f3, (void*)f1);

    vga_clear();
    vga_set_color(1, 7); printf("osLET Development Kernel\n");
    vga_set_color(0, 7);

    char line[128];
    for (;;) {
        printf("oslet> ");

        int n = kbd_getline(line, sizeof(line));
        if (n <= 0) {
            __asm__ volatile ("hlt");
            continue;
        }

        // Strip CR/LF
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (line[0] == '\0')
            continue;

        if (STREQ(line, "help")) {
            printf("Commands: bitmap, cls, help, mem\n");
            continue;
        }

        if (STREQ(line, "cls")) {
            vga_clear();
            continue;
        }

        if (STREQ(line, "mem")) {
            pmm_print_stats();
            continue;
        }

        if (STREQ(line, "bitmap")) {
            pmm_debug_dump_bitmap();
            continue;
        }

        printf("Unknown command: %s\n", line);
    }
}
