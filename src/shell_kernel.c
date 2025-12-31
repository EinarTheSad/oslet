#include <stdbool.h>
#include "console.h"
#include "drivers/vga.h"
#include "task.h"

static void print_banner(void) {
    vga_set_color(1, 15);
    printf("  _____   _____  .       ______ _______ \n");
    printf(" |     | |_____  |      |______    |    \n");
    printf(" |_____|  _____| |_____ |______    |    \n");
    printf("                                        \n");
    vga_set_color(0, 8);
    printf("Kernel %-17sEinarTheSad 2025\n\n",kernel_version);
    vga_set_color(0, 7);
}

void shell_init(void) {
    print_banner();
}

void shell_run(void) {
    printf("Kernel is loading %s from drive C...\n\n",shell_name);
    
    if (task_spawn_and_wait(shell_name) != 0) {
        vga_set_color(0, 12);
        printf("PANIC: Failed to load %s. System halt.\n",shell_name);
        for (;;) __asm__ volatile ("hlt");
    }
}