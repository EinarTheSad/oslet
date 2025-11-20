#include "drivers/graphics.h"
#include "drivers/keyboard.h"

void graphics_demo(void) {
    gfx_enter_mode();

    gfx_fill_vgradient(
        GFX_WIDTH/2, GFX_HEIGHT/2,
        COLOR_BLUE, COLOR_LIGHT_BLUE
    );

    int wx = 100;
    int wy = 80;
    int ww = 440;
    int wh = 280;

    gfx_rect(wx, wy, ww, wh, COLOR_BLACK);
    gfx_rect(wx+1, wy+1, ww-2, wh-2, COLOR_WHITE);

    gfx_fillrect_hgradient_dither(
        wx + 2, wy + 2,
        ww - 4, 24,
        COLOR_BLUE, COLOR_LIGHT_BLUE
    );

    gfx_rect(wx+2, wy+2, ww-4, 24, COLOR_BLACK);

    gfx_print(wx + 10, wy + 10, "Demo Window - osLET Kernel 0.3.3", COLOR_WHITE);

    gfx_fillrect(wx + 2, wy + 26, ww - 4, wh - 28, COLOR_LIGHT_GRAY);

    const char* msg = 
        "This is a VGA 640x480 demo in glorious 16 colours!\n"
        "Shape rendering, gradients & dithering all present.\n\n"
        "Press any key to exit...";

    gfx_print(wx + 12, wy + 40, msg, COLOR_BLACK);
    gfx_swap_buffers();
    kbd_getchar();

    gfx_exit_mode();
}
