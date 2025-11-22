#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "fonts/ttf.h"
#include "timer.h"

void graphics_demo(void) {
    gfx_enter_mode();

    gfx_fillrect_gradient(0,0,GFX_WIDTH,GFX_HEIGHT,COLOR_BLACK,COLOR_BLUE,GRADIENT_V);

    int wx = 100;
    int wy = 80;
    int ww = 440;
    int wh = 280;

    
    ttf_init();
    ttf_font_t* font = ttf_load("C:/FONTS/osans.ttf");
    ttf_font_t* font2 = ttf_load("C:/FONTS/arial.ttf");

    gfx_rect(wx, wy, ww, wh, COLOR_BLACK);
    gfx_rect(wx+1, wy+1, ww-2, wh-2, COLOR_WHITE);

    gfx_fillrect(wx + 2, wy + 2,ww - 4, 24, COLOR_CYAN);

    gfx_rect(wx+2, wy+2, ww-4, 24, COLOR_BLACK);

    ttf_print(wx + 10, wy + 6, "Demo Window - osLET Kernel 0.3.3", font, 12, COLOR_WHITE);

    gfx_fillrect(wx + 2, wy + 26, ww - 4, wh - 28, COLOR_LIGHT_GRAY);

    const char* msg = 
        "This is a VGA 640x480 demo in glorious 16 colours!\n"
        "Shape rendering, gradients & dithering all present.\n\n"
        "Press any key to exit...";

    ttf_print(wx + 12, wy + 40, msg, font, 12, COLOR_BLACK);
    ttf_print(wx + 12, wy + 100, "This is a TEST of size 16.", font, 16, COLOR_RED);
    ttf_print(wx + 12, wy + 120, "This is a TEST of size 24.", font, 24, COLOR_BLUE);
    ttf_print(wx + 12, wy + 160, msg, font2, 12, COLOR_BLACK);
    gfx_swap_buffers();
    kbd_getchar();
    ttf_free(font);
    ttf_free(font2);
    gfx_exit_mode();
}
