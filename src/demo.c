#include "drivers/graphics.h"
#include "drivers/keyboard.h"
#include "fonts/bmf.h"
#include "timer.h"
#include "console.h"

void graphics_demo(void) {    
    static bmf_font_t arial;
    bmf_import(&arial, "C:/FONTS/arial.bmf");

    gfx_enter_mode();

    gfx_fillrect_gradient(0,0,GFX_WIDTH,GFX_HEIGHT,COLOR_BLACK,COLOR_BLUE,GRADIENT_V);

    /* Transparentne tło (domyślnie) */
    bmf_printf(10, 10, &arial, 10, COLOR_WHITE,
               "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");
    
    /* Z wybranym tłem */
    bmf_printf_bg(10, 30, &arial, 10, COLOR_YELLOW, COLOR_BLUE,
                  "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");
    
    /* Różne rozmiary */
    bmf_printf(10, 60, &arial, 10, COLOR_GREEN,
               "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");
    
    bmf_printf(10, 80, &arial, 14, COLOR_CYAN,
               "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");
    
    bmf_printf(10, 110, &arial, 18, COLOR_RED,
               "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");

    bmf_printf(10, 130, &arial, 24, COLOR_LIGHT_BLUE,
               "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");

    bmf_printf(10, 160, &arial, 32, COLOR_LIGHT_CYAN,
               "The quick brown fox jumps over the lazy dog. 0123456789 !@#$%^&*()-=");
    
    gfx_swap_buffers();
    kbd_getchar();
    
    bmf_free(&arial);
    gfx_exit_mode();
}
