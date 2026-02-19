#include <stdbool.h>
#include "console.h"
#include "drivers/vga.h"
#include "drivers/fat32.h"
#include "drivers/graphics.h"
#include "task.h"
#include "timer.h"
#include "mem/heap.h"

int bootscreen(void) {
    /* Avoid switching to graphics mode if BOOT.BMP does not exist */
    fat32_file_t *f = fat32_open("C:/OSLET/BOOT.BMP", "r");
    if (!f) return 0;
    fat32_close(f);

    gfx_enter_mode();
    gfx_clear(COLOR_BLACK);

    int bmp_w = 0, bmp_h = 0;
    uint8_t *bmp = gfx_load_bmp_to_buffer("C:/OSLET/BOOT.BMP", &bmp_w, &bmp_h);
    if (!bmp) {
        /* Load failed or unsupported format */
        gfx_exit_mode();
        return 0;
    }

    int dest_x = (GFX_WIDTH - bmp_w) / 2;
    int dest_y = (GFX_HEIGHT - bmp_h) / 2;
    gfx_draw_cached_bmp_ex(bmp, bmp_w, bmp_h, dest_x, dest_y, 0);
    gfx_swap_buffers();
    kfree(bmp);

    /* Wait for ~2 seconds */
    timer_wait(timer_get_frequency() * 2);

    if (strcasecmp_s(shell_name, "SHELL.ELF") == 0) {
        gfx_exit_mode();
    }

    return 0;
}

void shell_init(void) {
    if (bootscr == 1) {
        bootscreen();
    }
    vga_set_color(0, 8);
    printf("osLET %s, EinarTheSad 2025-2026\n",kernel_version);
    printf("Loading %s from drive C...\n\n",shell_name);  
    if (task_spawn_and_wait(shell_name) != 0) {
        vga_set_color(0, 12);
        printf("PANIC: Failed to load %s. System halt.\n",shell_name);
        for (;;) __asm__ volatile ("hlt");
    }
}