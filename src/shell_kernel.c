#include <stdbool.h>
#include "console.h"
#include "drivers/vga.h"
#include "task.h"

void shell_init(void) {
    vga_set_color(0, 8);
    printf("osLET %s, EinarTheSad 2025-2026\n",kernel_version);
    printf("Loading %s from drive C...\n\n",shell_name);
    
    if (task_spawn_and_wait(shell_name) != 0) {
        vga_set_color(0, 12);
        printf("PANIC: Failed to load %s. System halt.\n",shell_name);
        for (;;) __asm__ volatile ("hlt");
    }
}