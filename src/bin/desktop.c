#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"
#include "../lib/string.h"

__attribute__((section(".entry"), used))
void _start(void) {
    
    static usr_bmf_font_t osans;
    static usr_bmf_font_t osans_bi;
    static usr_bmf_font_t osans_b;
    static usr_bmf_font_t osans_i;

    usr_bmf_import(&osans_bi, "C:/FONTS/OSANS_BI.BMF");
    usr_bmf_import(&osans, "C:/FONTS/OSANS.BMF");
    usr_bmf_import(&osans_i, "C:/FONTS/OSANS_I.BMF");
    usr_bmf_import(&osans_b, "C:/FONTS/OSANS_B.BMF");

    sys_gfx_enter();
    
    int mx, my;
    unsigned char mb;
    unsigned char last_mb = 0;
    
    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);

    void *box = sys_win_msgbox("Welcome to the osLET user interface.", "Thank you", "Proton 0.5");
        
    /* Dragging state */
    int dragging = 0;
    int drag_start_x = 0, drag_start_y = 0;

    sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);

    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        usr_bmf_printf(0, 0, &osans_b, 12, COLOR_YELLOW, ""); // Weird that it has to be there or else the mouse breaks
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
                sys_gfx_clear(COLOR_CYAN);
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

    usr_bmf_free(&osans);
    usr_bmf_free(&osans_bi);
    usr_bmf_free(&osans_b);
    
    sys_gfx_exit();
    sys_exit();
}