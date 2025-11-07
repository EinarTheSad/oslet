#include "console.h"
#include "vga.h"
#include "keyboard.h"
#include "early_alloc.h"
#include "io.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "task.h"
#include "rtc.h"

extern void idt_init(void);
extern void pic_remap(void);
extern uint8_t __kernel_end;

static void test_task_high(void) {
    for (int i = 0; i < 5; i++) {
        printf("[HIGH] Iteration %d\n", i);
        task_sleep(500);  /* 500ms */
    }
    printf("[HIGH] Finished!\n");
}

static void test_task_normal(void) {
    for (int i = 0; i < 5; i++) {
        printf("[NORMAL] Iteration %d\n", i);
        task_sleep(800);
    }
    printf("[NORMAL] Finished!\n");
}

static void test_task_low(void) {
    for (int i = 0; i < 5; i++) {
        printf("[LOW] Iteration %d\n", i);
        task_sleep(1000);
    }
    printf("[LOW] Finished!\n");
}

void kmain(void) {
    vga_use_as_console();
    vga_clear();
    idt_init();
    pic_remap();
    keyboard_init();
    timer_init(100);

    mm_early_init((uintptr_t)&__kernel_end);
    pmm_init_from_multiboot((uint32_t)multiboot_info_ptr);

    __asm__ volatile ("cli");

    uintptr_t kernel_end = (uintptr_t)&__kernel_end;
    uintptr_t map_upto = (kernel_end + 0xFFF) & ~((uintptr_t)0xFFF);
    map_upto += 16 * 1024 * 1024;

    if (paging_identity_enable(map_upto) != 0) {
        vga_set_color(12,15);
        printf("FAILED to enable memory paging\n");
        for (;;) __asm__ volatile ("hlt");
    }
    
    pmm_identity_map_bitmap();
    heap_init();
    rtc_init();
    tasking_init();
    
    timer_enable_scheduling();

    __asm__ volatile ("sti");

    vga_set_color(1, 7);
    printf("osLET Development Kernel\n\n");
    vga_set_color(0, 7);

    char line[128];
    for (;;) {
        printf("oslet> ");

        int n = kbd_getline(line, sizeof(line));
        if (n <= 0) {
            __asm__ volatile ("hlt");
            continue;
        }

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (line[0] == '\0')
            continue;

        if (STREQ(line, "help")) {
            printf("Commands:\n");
            printf("  cls       - Clear screen\n");
            printf("  heap      - Show heap stats\n");
            printf("  mem       - Show memory stats\n");
            printf("  ps        - List tasks\n");
            printf("  rtc       - Show current time/date\n");
            printf("  uptime    - Show uptime\n");
            printf("  test      - Run test tasks\n");
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

        if (STREQ(line, "heap")) {
            heap_print_stats();
            continue;
        }

        if (STREQ(line, "uptime")) {
            uint32_t ticks = timer_get_ticks();
            uint32_t seconds = ticks / 100;
            printf("Uptime: %u ticks (%u seconds)\n", ticks, seconds);
            continue;
        }

        if (STREQ(line, "ps")) {
            task_list_print();
            continue;
        }
        
        if (STREQ(line, "rtc")) {
            rtc_print_time();
            continue;
        }
        
        if (STREQ(line, "test")) {
            printf("Creating test tasks...\n");
            task_create(test_task_high, "high_task", PRIORITY_HIGH);
            task_create(test_task_normal, "normal_task", PRIORITY_NORMAL);
            task_create(test_task_low, "low_task", PRIORITY_LOW);
            printf("Tasks created. Watch them run!\n");
            continue;
        }

        printf("Unknown command: %s\n", line);
    }
}