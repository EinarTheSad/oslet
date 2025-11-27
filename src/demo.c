#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "fonts/bmf.h"
#include "timer.h"
#include "console.h"

void graphics_demo(void) {    
    static bmf_font_t osans;
    static bmf_font_t osans_bi;
    static bmf_font_t osans_b;

    bmf_import(&osans, "C:/FONTS/OSANS.BMF");
    bmf_import(&osans_bi, "C:/FONTS/OSANS_BI.BMF");
    bmf_import(&osans_b, "C:/FONTS/OSANS_B.BMF");

    gfx_enter_mode();

    gfx_fillrect_gradient(0,0,GFX_WIDTH,GFX_HEIGHT,COLOR_BLACK,COLOR_BLUE,GRADIENT_V);

    bmf_printf(20, 20, &osans_bi, 32, COLOR_WHITE, "Welcome to %s", "Codename osLET 0.4");

    const char *msg = "Thank you for cloning this repository. osLET is a hobby project written to show the\n"
                      "author's love and appreciation for the early 1990s operating system aesthetics.\n"
                      "The project is licensed under GPL 2.0. Please use the code to do good for the world.";
    bmf_printf(20, 75, &osans, 12, COLOR_WHITE, msg);

    bmf_printf(20, 143, &osans_b, 12, COLOR_YELLOW, "Please press any key to go back to the command prompt...");

    for (int i = 0; i < 16; i++) { /* palette test */
        gfx_fillrect(20+i*36, 175, 32, 16, i);
        gfx_fillrect(20+i*36, 191, 32, 16, i+8);
        gfx_fillrect_gradient(20+i*36, 207, 32, 16, i, i+8, GRADIENT_H);
    }
    
    gfx_swap_buffers();
    kbd_getchar();

    gfx_clear(COLOR_BROWN);
    gfx_load_bmp_4bit("C:/bitmap.bmp",0,0);
    gfx_swap_buffers();
    
    kbd_getchar();
    
    bmf_free(&osans);
    bmf_free(&osans_bi);
    bmf_free(&osans_b);
    gfx_exit_mode();
}
