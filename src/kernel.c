#include "console.h"
#include "drivers/vga.h"
#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "drivers/mouse.h"
#include "mem/early_alloc.h"
#include "irq/io.h"
#include "mem/pmm.h"
#include "mem/paging.h"
#include "mem/heap.h"
#include "timer.h"
#include "task.h"
#include "rtc.h"
#include "syscall.h"
#include "drivers/ata.h"
#include "drivers/fat32.h"
#include "gdt.h"
#include "win/window.h"
#include "win/theme.h"

extern void idt_init(void);
extern void pic_remap(void);
extern void shell_init(void);
extern void shell_run(void);
extern uint8_t __kernel_end;

uint32_t boot_device = 0xFFFFFFFF;

static void boot_sequence(void) {
    vga_use_as_console();
    vga_reset_textmode();
    
    typedef struct {
        uint32_t flags;
        uint32_t mem_lower;
        uint32_t mem_upper;
        uint32_t boot_device;
    } multiboot_info_header_t;
    
    multiboot_info_header_t *mbi = (multiboot_info_header_t*)(uintptr_t)multiboot_info_ptr;
    if (mbi && (mbi->flags & 0x02)) {
        boot_device = mbi->boot_device;
    }
        
    idt_init();
    pic_remap();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] IDT & PIC\n");

    keyboard_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Keyboard\n");

    mouse_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Mouse\n");

    timer_init(100);
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Timer\n");
    
    mm_early_init((uintptr_t)&__kernel_end);
    pmm_init_from_multiboot((uint32_t)multiboot_info_ptr);
    
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Memory manager\n");
    
    __asm__ volatile ("cli");
    
    uintptr_t kernel_end = (uintptr_t)&__kernel_end;
    uintptr_t map_upto = (kernel_end + 0xFFF) & ~((uintptr_t)0xFFF);
    map_upto += 16 * 1024 * 1024;
    
    if (paging_identity_enable(map_upto) != 0) {
        printf("\r[");
        vga_set_color(0, 12);
        printf("FAIL");
        vga_set_color(0, 7);
        printf("] Paging error         \n");
        for (;;) __asm__ volatile ("hlt");
    }
    
    pmm_identity_map_bitmap();    
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Paging\n");

    gdt_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] GDT\n");

    heap_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Heap\n");
    
    rtc_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] RTC\n");

    ata_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] ATA driver\n");
    fat32_init();
       
    tasking_init();
    syscall_init();
    timer_enable_scheduling();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Multitasking\n");

    vga_set_color(0, 8);
    printf("[ .. ] Attempting to identify drive C...");
    
    if (ata_identify() == 0) {
        // Give drive time to settle after IDENTIFY
        for (volatile int i = 0; i < 1000; i++);
        vga_set_color(0, 8);
        printf("\r[ .. ] Attempting to auto-mount drive C...");
        vga_set_color(0, 7);
        if (fat32_mount_auto('C') == 0) {
            printf("\r[");
            vga_set_color(0, 10);
            printf(" OK");
            vga_set_color(0, 7);
            printf(" ] Mounted drive C                       \n");
        } else {
            printf("\r[");
            vga_set_color(0, 12);
            printf("FAIL");
            vga_set_color(0, 7);
            printf("] No filesystem!                         \n");
            for (;;) __asm__ volatile ("hlt");
        }
    } else {
        printf("\r[");
        vga_set_color(0, 12);
        printf("FAIL");
        vga_set_color(0, 7);
        printf("] No ATA drive detected                      \n");
        for (;;) __asm__ volatile ("hlt");
    }

    win_init_fonts();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Fonts\n");

    theme_init();
    printf("[ ");
    vga_set_color(0, 10);
    printf("OK");
    vga_set_color(0, 7);
    printf(" ] Theme\n");

    printf("\n");
    __asm__ volatile ("sti");
}

void kmain(void) {
    boot_sequence();
    shell_init();
    
    for (;;) __asm__ volatile ("hlt");
}