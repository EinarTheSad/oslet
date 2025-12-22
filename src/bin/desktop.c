#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"

gui_control_t controls[] = {
    {CTRL_PICTUREBOX, 5, 5, 108, 208, 0, 7, "SETUP.BMP", 1, 0, 12, 0, 0},
    {CTRL_BUTTON, 290, 189, 75, 25, 0, 7, "OK", 2, 0, 12, 0, 0},
    {CTRL_LABEL, 118, 12, 0, 0, 0, 15, "Welcome to osLET!", 3, 1, 12, 0, 0},
    {CTRL_LABEL, 118, 40, 190, 0, 0, 15, "This window serves as a test of the\ncontrol system in osLET graphical\nuser interface.\n\nYou can drag this window freely\nusing a mouse, or click the button\nbelow to exit back to shell.\n", 5, 0, 12, 0, 0}
};

__attribute__((section(".entry"), used))
void _start(void) {

    sys_gfx_enter();

    int mx = 320;
    int my = 240;
    unsigned char mb;

    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);

    void *form = sys_win_create_form("Welcome screen", 100, 100, 370, 240);

    sys_win_add_control(form, &controls[0]);
    sys_gfx_fillrect(0, 0, 0, 0, COLOR_CYAN); // I don't know why I need to keep this, or else it breaks after drawing one control
    sys_win_add_control(form, &controls[1]);
    sys_gfx_fillrect(0, 0, 0, 0, COLOR_CYAN);
    sys_win_add_control(form, &controls[2]);
    sys_gfx_fillrect(0, 0, 0, 0, COLOR_CYAN);
    sys_win_add_control(form, &controls[3]);
    sys_gfx_fillrect(0, 0, 0, 0, COLOR_CYAN);

    /* Draw the form (automatically draws all controls) */
    sys_win_draw(form);

    sys_mouse_draw_cursor(mx, my, COLOR_WHITE, 1);
    sys_gfx_swap();

    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        usr_bmf_printf(0,0,NULL,0,0,""); // This has to be here or else the mouse won't work, don't ask why

        /* Pump window events (handles both clicks and dragging) */
        int event = sys_win_pump_events(form);

        if (event > 0) {
            /* Control was clicked - event contains the control ID */
            if (event == 2) {
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