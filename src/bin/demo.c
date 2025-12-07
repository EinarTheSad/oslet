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
    sys_gfx_fillrect_gradient(0, 0, 640, 480, COLOR_BLACK, COLOR_BLUE, 1);
    usr_bmf_printf(20, 20, &osans_bi, 32, 15, "Welcome to Codename osLET %s", sys_version());

    const char *msg = "Thank you for cloning this repository. osLET is a hobby project written to show the\n"
                      "author's love and appreciation for the early 1990s operating system aesthetics.\n"
                      "The project is licensed under GPL 2.0. Please use the code to do good for the world.";
    usr_bmf_printf(20, 75, &osans, 12, 15, msg);
    usr_bmf_printf(20, 143, &osans_b, 12, 14, "Drag the window or click OK...");

    /* Palette test */
    for (int i = 0; i < 16; i++) {
        sys_gfx_fillrect(20+i*36, 175, 32, 16, i);
        sys_gfx_fillrect_gradient(20+i*36, 191, 32, 16, i, (i+8) & 0xF, 1);
    }

    void *box = sys_win_msgbox("This is a test message box.", "OK", "Message Box");
    
    /* Dragging state */
    int dragging = 0;
    int drag_start_x = 0, drag_start_y = 0;

    sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);

    while (1) {
    sys_get_mouse_state(&mx, &my, &mb);
    
    usr_bmf_printf(20, 143, &osans_b, 12, 14, "");

    /* Handle mouse press */
    if ((mb & 1) && last_mb == 0) {
        if (sys_win_is_titlebar(box, mx, my)) {
            dragging = 1;
            drag_start_x = mx;
            drag_start_y = my;
        }
        else if (sys_win_msgbox_click(box, mx, my)) {
            /* Mark button as clicked */
            last_mb = 1;
        }
        else {
            /* Click outside - consume it */
            last_mb = 1;
        }
    }
    /* Handle mouse release */
    else if (!(mb & 1) && last_mb == 1) {
        if (dragging) {
            dragging = 0;
        } else if (sys_win_msgbox_click(box, mx, my)) {
            /* Only exit if released over OK button */
            break;
        }
        last_mb = 0;  /* Reset for next click */
    }
    
    /* Handle dragging */
    if (dragging) {
        int dx = mx - drag_start_x;
        int dy = my - drag_start_y;
        
        if (dx != 0 || dy != 0) {
            sys_win_move(box, dx, dy);
            drag_start_x = mx;
            drag_start_y = my;
            
            /* Redraw everything */
            sys_gfx_fillrect_gradient(0, 0, 640, 480, COLOR_BLACK, COLOR_BLUE, 1);
            usr_bmf_printf(20, 20, &osans_bi, 32, 15, "Welcome to Codename osLET %s", sys_version());
            usr_bmf_printf(20, 75, &osans, 12, 15, msg);
            for (int i = 0; i < 16; i++) {
                sys_gfx_fillrect(20+i*36, 175, 32, 16, i);
                sys_gfx_fillrect_gradient(20+i*36, 191, 32, 16, i, (i+8) & 0xF, 1);
            }
            usr_bmf_printf(20, 143, &osans_b, 12, 14, "Drag the window or click OK...");
            sys_win_msgbox_draw(box);
            
            /* Force full redraw of cursor (invalidate buffer) */
            sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);
            sys_gfx_swap();
            continue;  /* Skip normal cursor handling */
        }
    }
    
    sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 0);
    sys_gfx_swap();
    
    last_mb = mb & 1;
}

    /* Second screen - bitmap */
    sys_gfx_load_bmp("C:/bitmap.bmp", 0, 0);

    const char *msg2 = "Click to return to command line";
    int msgx = strlen(msg2)*7.5-strlen(msg2)*7.5/2;
    sys_gfx_fillrect(msgx+87, 449, strlen(msg2)*7.5, 12, COLOR_GREEN);
    sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);

    last_mb = 0;
    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        usr_bmf_printf(msgx+87, 450, &osans_b, 12, COLOR_YELLOW, msg2);
        sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 0);
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