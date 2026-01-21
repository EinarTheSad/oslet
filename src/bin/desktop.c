#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"
#include "progman.h"

#define TASKBAR_HEIGHT 27
#define TASKBAR_Y (480 - TASKBAR_HEIGHT)
#define DESKTOP_COLOR COLOR_CYAN

usr_bmf_font_t font_b;
usr_bmf_font_t font_n;
sys_time_t current = {0};

typedef struct {
    int x, y, w, h;
    int pressed;
    char label[6];
} taskbar_button_t;

static taskbar_button_t start_button;
static int last_clock_hour = -1;
static int last_clock_minute = -1;

extern const progmod_t startman_module;

static void prog_register_all(void) {
    progman_register(&startman_module);
}

void draw_simple_button(int x, int y, int w, int h, const char *label, int pressed) {
    uint8_t shad_a, shad_b;

    sys_gfx_rect(x, y, w, h, COLOR_BLACK);
    sys_gfx_fillrect(x+2, y+2, w-3, h-3, COLOR_LIGHT_GRAY);

    if (pressed) {
        shad_a = COLOR_WHITE;
        shad_b = COLOR_DARK_GRAY;
    } else {
        shad_a = COLOR_DARK_GRAY;
        shad_b = COLOR_WHITE;
    }

    sys_gfx_rect(x+1, y+1, w-2, h-2, shad_a);
    sys_gfx_line(x+1, y+1, x+w-3, y+1, shad_b);
    sys_gfx_line(x+1, y+1, x+1, y+h-3, shad_b);

    sys_gfx_load_bmp("C:/ICONS/LET.ICO", x+3, y+2);

    if (font_b.data && label) {
        int text_x = x + 22;
        int text_y = y + 7;
        usr_bmf_printf(text_x, text_y, &font_b, 12, COLOR_BLACK, "%s", label);
    }
}

static void start_init(void) {
    start_button.x = 3;
    start_button.y = TASKBAR_Y + 3;
    start_button.w = 57;
    start_button.h = 21;
    start_button.pressed = 0;
}

static void clock_draw(void) {
    sys_get_time(&current);
    sys_gfx_rect(640-60, TASKBAR_Y + 3, 57, 21, COLOR_DARK_GRAY);
    sys_gfx_fillrect(640-59, TASKBAR_Y + 4, 55, 19, COLOR_LIGHT_GRAY);
    usr_bmf_printf(640-38, TASKBAR_Y + 10, &font_n, 12, COLOR_BLACK, "%02u:%02u", current.hour, current.minute);
    last_clock_hour = current.hour;
    last_clock_minute = current.minute;
}

static int clock_update(void) {
    sys_get_time(&current);
    if (current.hour != last_clock_hour || current.minute != last_clock_minute) {
        clock_draw();
        return 1;
    }
    return 0;
}

static void taskbar_draw(void) {
    sys_gfx_fillrect(0, TASKBAR_Y, 640, TASKBAR_HEIGHT, COLOR_LIGHT_GRAY);
    sys_gfx_line(0, TASKBAR_Y, 640, TASKBAR_Y, COLOR_WHITE);

    draw_simple_button(start_button.x, start_button.y,
                      start_button.w, start_button.h,
                      "Start", start_button.pressed);
}

static int start_click(int mx, int my, unsigned char mb, int *state_changed) {
    static unsigned char last_mb = 0;
    int old_pressed = start_button.pressed;
    int clicked = 0;

    if (mx >= start_button.x && mx < start_button.x + start_button.w &&
        my >= start_button.y && my < start_button.y + start_button.h) {

        if (mb & 1) {
            start_button.pressed = 1;
        } else if ((last_mb & 1) && !(mb & 1)) {
            start_button.pressed = 0;
            clicked = 1;
        }
    } else {
        if (!(mb & 1)) {
            start_button.pressed = 0;
        }
    }

    if (old_pressed != start_button.pressed) {
        *state_changed = 1;
    }

    last_mb = mb;
    return clicked;
}

static void desktop_redraw(void) {
    sys_gfx_fillrect(0, 0, 640, TASKBAR_Y, DESKTOP_COLOR);
    taskbar_draw();
    clock_draw();
    sys_win_invalidate_icons();
    sys_win_redraw_all();
}

static void pump_all_program_events(int mx, int my) {
    for (int i = 0; i < PROGMAN_INSTANCES_MAX; i++) {
        prog_instance_t *inst = progman_get_instance(i);
        if (!inst || inst->state != PROG_STATE_RUNNING)
            continue;

        for (int j = 0; j < inst->window_count; j++) {
            void *form = inst->windows[j];
            if (!form) continue;

            int event = sys_win_pump_events(form);
            if (event != 0) {
                /* Handle close from window menu (-3) */
                if (event == -3) {
                    progman_close(inst->instance_id);
                    desktop_redraw();
                    break;
                }

                int result = progman_route_event(form, event);

                if (result == PROG_EVENT_CLOSE) {
                    progman_close(inst->instance_id);
                    desktop_redraw();
                    break;
                }

                if (event == -1 || event == -2) {
                    /* Window state changed */
                    desktop_redraw();
                }
            }
        }
    }
    (void)mx;
    (void)my;
}

__attribute__((section(".entry"), used))
void _start(void) {
    int mx = 320;
    int my = 240;
    unsigned char mb;

    sys_gfx_enter();
    sys_gfx_fillrect(0, 0, 640, 480, DESKTOP_COLOR);

    usr_bmf_import(&font_b, "C:/FONTS/LSANS_B.BMF");
    usr_bmf_import(&font_n, "C:/FONTS/LSANS.BMF");

    progman_init();
    prog_register_all();

    start_init();
    taskbar_draw();
    clock_draw();

    int exit_requested = 0;
    int taskbar_needs_redraw = 0;

    while (!exit_requested) {
        clock_update();
        sys_get_mouse_state(&mx, &my, &mb);

        /* Handle Start button */
        int button_state_changed = 0;
        if (start_click(mx, my, mb, &button_state_changed)) {
            /* Start button clicked - launch or focus Start Manager */
            if (!progman_is_running("Start Manager")) {
                progman_launch("Start Manager");
            }
            taskbar_needs_redraw = 1;
        }

        if (button_state_changed) {
            taskbar_needs_redraw = 1;
        }

        if (taskbar_needs_redraw) {
            taskbar_draw();
            clock_draw();
            taskbar_needs_redraw = 0;
        }

        progman_update_all();
        pump_all_program_events(mx, my);

        sys_mouse_draw_cursor(mx, my, 0);
        sys_gfx_swap();
        sys_yield();
    }

    sys_gfx_exit();
    sys_exit();
}
