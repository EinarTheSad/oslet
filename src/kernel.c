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

extern void idt_init(void);
extern void pic_remap(void);
extern uint8_t __kernel_end;

void demo_task_a(void) {
    for (int i = 0; i < 5; i++) {
        printf("Task A: iteration %d\n", i);
        timer_wait(100);
    }
    printf("Task A: completed\n");
}

void demo_task_b(void) {
    for (int i = 0; i < 5; i++) {
        printf("Task B: iteration %d\n", i);
        timer_wait(150);
    }
    printf("Task B: completed\n");
}

void demo_task_c(void) {
    for (int i = 0; i < 3; i++) {
        printf("Task C: counting... %d\n", i);
        timer_wait(200);
    }
    printf("Task C: done\n");
}

void kmain(void) {
    vga_use_as_console();
    vga_clear();
    idt_init();
    pic_remap();
    keyboard_init();
    timer_init(100); /* 10 ms timer (100 HZ) */

    mm_early_init((uintptr_t)&__kernel_end);
    pmm_init_from_multiboot((uint32_t)multiboot_info_ptr);

    /* Paging setup */
    __asm__ volatile ("cli");

    uintptr_t kernel_end = (uintptr_t)&__kernel_end;
    uintptr_t map_upto = (kernel_end + 0xFFF) & ~((uintptr_t)0xFFF);
    map_upto += 16 * 1024 * 1024; /* 16 MB mapped */

    if (paging_identity_enable(map_upto) != 0) {
        vga_set_color(12,15);
        printf("FAILED to enable memory paging\n");
        for (;;) __asm__ volatile ("hlt");
    }
    
    heap_init();
    tasking_init();

    __asm__ volatile ("sti");

    vga_set_color(1, 7); printf("osLET Development Kernel\n\n");
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
            printf("Commands: bitmap, cls, heap, help, mem, task, test, ps, uptime\n");
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

        if (STREQ(line, "task")) {
            printf("Starting multitasking demo...\n");
            printf("Creating 3 concurrent tasks...\n\n");
            
            task_create(demo_task_a, "TaskA");
            task_create(demo_task_b, "TaskB");
            task_create(demo_task_c, "TaskC");
            
            timer_enable_scheduling();
            
            printf("\nScheduler enabled. Tasks running...\n");
            printf("Type 'ps' to see task list\n\n");
            
            continue;
        }

        if (STREQ(line, "test")) {
            printf("Testing heap allocator...\n");
            
            int *arr = (int*)kmalloc(10 * sizeof(int));
            if (arr) {
                for (int i = 0; i < 10; i++) arr[i] = i * i;
                printf("Array: ");
                for (int i = 0; i < 10; i++) printf("%d ", arr[i]);
                printf("\n");
                kfree(arr);
                printf("Array freed\n");
            } else {
                printf("Allocation failed\n");
            }
            
            char *str = (char*)kmalloc(64);
            if (str) {
                snprintf(str, 64, "Dynamic string test: %d", 42);
                printf("%s\n", str);
                kfree(str);
            }

            printf("Testing timer wait (3 seconds)...\n");
            timer_wait(300);
            printf("Done!\n");

            continue;
        }

        printf("Unknown command: %s\n", line);
    }
}
