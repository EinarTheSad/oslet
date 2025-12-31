#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"

gui_control_t controls[] = {
    {CTRL_PICTUREBOX, 5, 5, 108, 208, 0, 7, "SETUP.BMP", 1, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_BUTTON, 290, 189, 75, 25, 0, 7, "OK", 2, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_LABEL, 118, 12, 0, 0, 0, 15, "Welcome to osLET!", 3, 1, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_LABEL, 118, 40, 190, 0, 0, 15, "This window serves as a test of the\ncontrol system in osLET graphical\nuser interface.\n\nYou can drag this window freely\nusing a mouse, or click the button\nbelow to exit back to shell.\n", 5, 0, 12, 0, 0, NULL, 0, 0, 0}
};

void draw_taskbar(void) {
    sys_gfx_fillrect(1, 480-25, 638, 24, COLOR_LIGHT_GRAY);
    sys_gfx_line(0, 480-26, 639, 480-26, COLOR_WHITE);
    sys_gfx_line(0, 480-25, 0, 478, COLOR_WHITE);
    sys_gfx_line(639, 480-25, 639, 479, COLOR_DARK_GRAY);
    sys_gfx_line(0, 479, 639, 479, COLOR_DARK_GRAY);
}

__attribute__((section(".entry"), used))
void _start(void) {

    sys_gfx_enter();

    int mx = 320;
    int my = 240;
    unsigned char mb;

    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
    draw_taskbar();

    void *form = sys_win_create_form("Welcome screen", 50, 50, 370, 240);
    sys_win_set_icon(form, "C:/ICONS/EXE.ICO");
    for (int i = 0; i < 4; i++) {
        sys_win_add_control(form, &controls[i]);
    }
    sys_win_draw(form);

    while (1) {
        sys_get_mouse_state(&mx, &my, &mb);
        int event = sys_win_pump_events(form);

        if (event > 0) {
            /* Control was clicked - event contains the control ID */
            if (event == 2) {
                /* OK button clicked */
                break;
            }
        } else if (event < 0) {
            /* Window needs redraw (dragged or button state changed) */
            sys_win_draw(form);
        }

        sys_mouse_draw_cursor(mx, my, 0);
        sys_gfx_swap();
    }

    sys_win_destroy_form(form);
    sys_gfx_exit();
    sys_exit();
}