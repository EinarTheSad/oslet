#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"
#include "../lib/string.h"

__attribute__((section(".entry"), used))
void _start(void) {
    
    static usr_bmf_font_t osans;
    static usr_bmf_font_t osans_bi;
    static usr_bmf_font_t osans_b;

    if (usr_bmf_import(&osans, "C:/FONTS/OSANS.BMF") != 0) {
        sys_write("Failed to load osans\n");
        sys_exit();
    }
    
    if (usr_bmf_import(&osans_bi, "C:/FONTS/OSANS_BI.BMF") != 0) {
        sys_write("Failed to load osans_bi\n");
        sys_exit();
    }
    
    if (usr_bmf_import(&osans_b, "C:/FONTS/OSANS_B.BMF") != 0) {
        sys_write("Failed to load osans_b\n");
        sys_exit();
    }

    sys_gfx_enter();
    
    int mx, my;
    unsigned char mb;
    unsigned char last_mb = 0;
    
    /* First screen - welcome */
    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        
        sys_gfx_fillrect_gradient(0, 0, 640, 480, COLOR_BLACK, COLOR_BLUE, 1);
        usr_bmf_printf(20, 20, &osans_bi, 32, 15, "Welcome to Codename osLET 0.4");

        const char *msg = "Thank you for cloning this repository. osLET is a hobby project written to show the\n"
                          "author's love and appreciation for the early 1990s operating system aesthetics.\n"
                          "The project is licensed under GPL 2.0. Please use the code to do good for the world.";
        usr_bmf_printf(20, 75, &osans, 12, 15, msg);

        usr_bmf_printf(20, 143, &osans_b, 12, 14, "Click to display a test bitmap...");

        /* Palette test */
        for (int i = 0; i < 16; i++) {
            sys_gfx_fillrect(20+i*36, 175, 32, 16, i);
            sys_gfx_fillrect_gradient(20+i*36, 191, 32, 16, i, (i+8) & 0xF, 1);
        }
        
        sys_mouse_draw_cursor(mx, my, COLOR_WHITE);
        sys_gfx_swap();
        
        /* Detect click - wait for release */
        if ((mb & 1) && last_mb == 0) {
            last_mb = 1;
        } else if (!(mb & 1) && last_mb == 1) {
            break;
        } else {
            last_mb = mb & 1;
        }
    }

    /* Second screen - bitmap */
    sys_gfx_load_bmp("C:/bitmap.bmp", 0, 0);
    
    const char *msg2 = "Click to return to command line";
    int msgx = strlen(msg2)*7.5-strlen(msg2)*7.5/2;
    
    last_mb = 0;
    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        
        sys_gfx_load_bmp("C:/bitmap.bmp", 0, 0);
        sys_gfx_fillrect(msgx+87, 449, strlen(msg2)*7.5, 12, COLOR_GREEN);
        usr_bmf_printf(msgx+87, 450, &osans_b, 12, COLOR_YELLOW, msg2);
        
        sys_mouse_draw_cursor(mx, my, COLOR_WHITE);
        sys_gfx_swap();
        
        /* Detect click - wait for release */
        if ((mb & 1) && last_mb == 0) {
            last_mb = 1;
        } else if (!(mb & 1) && last_mb == 1) {
            break;
        } else {
            last_mb = mb & 1;
        }
        
    }
    
    usr_bmf_free(&osans);
    usr_bmf_free(&osans_bi);
    usr_bmf_free(&osans_b);
    
    sys_gfx_exit();
    sys_exit();
}