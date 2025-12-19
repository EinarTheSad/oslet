#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"
#include "../lib/string.h"

gui_control_t controls[] = {
    {CTRL_BUTTON, 206, 191, 50, 22, 0, COLOR_LIGHT_GRAY, "OK", 1},
    {CTRL_BUTTON, 262, 191, 50, 22, 0, COLOR_LIGHT_GRAY, "Exit", 2}
};

__attribute__((section(".entry"), used))
void _start(void) {

    sys_gfx_enter();

    int mx, my;
    unsigned char mb;

    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);

    void *form = sys_win_create_form("Form1", 100, 100, 320, 240);

    /* Add controls to the form */
    sys_win_add_control(form, &controls[0]);
    sys_gfx_fillrect(0, 0, 0, 0, COLOR_CYAN); // For compiler not to optimise
    sys_win_add_control(form, &controls[1]);

    /* Draw the form (automatically draws all controls) */
    sys_win_draw(form);

    sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);
    sys_gfx_swap();

    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        usr_bmf_printf(0, 0, NULL, 12, COLOR_YELLOW, "");

        /* Pump window events (handles both clicks and dragging) */
        int event = sys_win_pump_events(form);

        if (event > 0) {
            /* Control was clicked - event contains the control ID */
            if (event == 1) {
                /* OK button clicked */
                sys_win_msgbox("You clicked OK!", "Close", "MessageBox");
                /* Redraw after msgbox closes */
                sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
                sys_win_draw(form);
            } else if (event == 2) {
                /* Exit button clicked - exit application */
                break;
            }
        } else if (event < 0) {
            /* Window was dragged - redraw */
            sys_win_draw(form);
            sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);
            sys_gfx_swap();
            continue;
        }

        sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 0);
        sys_gfx_swap();
    }

    sys_gfx_exit();
    sys_exit();
}