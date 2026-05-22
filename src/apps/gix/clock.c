#include "../../syscall.h"
#include "../../lib/app.h"
#include "../../lib/gix_app.h"

OSLET_APP("Clock", OSLET_KIND_GIX, "C:/ICONS/CLOCK.ICO", OSLET_APP_FLAG_NONE);

#define WIN_W 120
#define WIN_H 140
#define ID_CLOCK 1

static gui_control_t clock_controls[] = {
    { .type = CTRL_CLOCK, .id = ID_CLOCK, .x = 5, .y = 5, .w = 110, .h = 110, .bg = -1 }
};

static int last_second = -1;

static void update_clock_size(void *form, void *userdata) {
    (void)userdata;
    gui_form_t *f = form;
    int clock_w = f->win.w - 10;
    int clock_h = f->win.h - 30;
    
    if (clock_w < 60) clock_w = 60;
    if (clock_h < 60) clock_h = 60;
    
    sys_ctrl_set_prop(form, ID_CLOCK, PROP_W, clock_w);
    sys_ctrl_set_prop(form, ID_CLOCK, PROP_H, clock_h);
}

static void clock_tick(void *form, void *userdata) {
    (void)userdata;
    gui_form_t *f = form;
    if (!f->win.is_minimized) {
        sys_time_t time = {0};
        sys_get_time(&time);
        if (time.second != last_second) {
            last_second = time.second;
            sys_win_mark_dirty_rect(f->win.x, f->win.y, f->win.w, f->win.h);
        }
    }
}

__attribute__((section(".entry"), used))
void _start(void) {
    static gix_app_desc_t app = {
        .title = "Clock",
        .icon_path = "C:/ICONS/CLOCK.ICO",
        .x = 520,
        .y = 0,
        .w = WIN_W,
        .h = WIN_H,
        .resizable = 1,
        .controls = clock_controls,
        .control_count = 1,
        .on_init = update_clock_size,
        .on_resize = update_clock_size,
        .on_tick = clock_tick
    };

    gix_app_run(&app);
}
