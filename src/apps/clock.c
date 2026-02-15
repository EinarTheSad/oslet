#include "../syscall.h"

#define WIN_W 120
#define WIN_H 140

__attribute__((section(".entry"), used))
void _start(void) {
    void *form = sys_win_create_form("Clock", 520, 0, WIN_W, WIN_H);
    if (!form) {
        sys_exit();
        return;
    }
    sys_win_set_icon(form, "C:/ICONS/CLOCK.ICO");

    /* Add clock control that fills most of the form */
    gui_control_t clock_ctrl = {0};
    clock_ctrl.type = CTRL_CLOCK;
    clock_ctrl.x = 5;
    clock_ctrl.y = 5;
    clock_ctrl.w = 110;
    clock_ctrl.h = 110;
    clock_ctrl.bg = -1;
    sys_win_add_control(form, &clock_ctrl);

    sys_win_mark_dirty(form);

    /* Main event loop */
    int running = 1;
    int last_second = -1;
    while (running) {
        int event = sys_win_pump_events(form);

        /* Handle close request from window menu */
        if (event == -3) {
            running = 0;
            continue;
        }

        /* Handle window state changes - redraw needed */
        if (event == -1 || event == -2) {
            sys_win_mark_dirty(form);
        }

        /* Check if second changed and trigger redraw */
        gui_form_t *f = form;
        if (!f->win.is_minimized) {
            sys_time_t time = {0};
            sys_get_time(&time);
            if (time.second != last_second) {
                last_second = time.second;
                sys_win_mark_dirty(form);
            }
        }

        sys_yield();
    }

    sys_win_destroy_form(form);
    sys_exit();
}
