#include "../syscall.h"

#define WIN_W 120
#define WIN_H 140
#define ID_CLOCK 1

static void update_clock_size(void *form) {
    gui_form_t *f = form;
    int clock_w = f->win.w - 10;
    int clock_h = f->win.h - 30;
    
    if (clock_w < 60) clock_w = 60;
    if (clock_h < 60) clock_h = 60;
    
    sys_ctrl_set_prop(form, ID_CLOCK, PROP_W, clock_w);
    sys_ctrl_set_prop(form, ID_CLOCK, PROP_H, clock_h);
}

static int clock_handle_event(void *form, int event, void *userdata) {
    int *last_second = userdata;

    gui_form_t *f = form;
    if (!f->win.is_minimized) {
        /* Handle resize event */
        if (event == -4) {
            update_clock_size(form);
            sys_win_mark_dirty(form);
            return 0;
        }
        
        sys_time_t time = {0};
        sys_get_time(&time);
        if (time.second != *last_second) {
            *last_second = time.second;
            sys_win_mark_dirty(form);
        }
    }
    return 0;
}

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
    clock_ctrl.id = ID_CLOCK;
    clock_ctrl.x = 5;
    clock_ctrl.y = 5;
    clock_ctrl.w = 110;
    clock_ctrl.h = 110;
    clock_ctrl.bg = -1;
    sys_win_add_control(form, &clock_ctrl);

    sys_win_mark_dirty(form);

    int last_second = -1;
    sys_win_run_event_loop(form, clock_handle_event, &last_second);

    sys_win_destroy_form(form);
    sys_exit();
}
