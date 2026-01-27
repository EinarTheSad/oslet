#include "../syscall.h"
#include "../lib/stdio.h"

#define CLOCK_RADIUS 45
#define CLOCK_CENTER_X 55
#define CLOCK_CENTER_Y 55
#define WIN_W 110
#define WIN_H 130

/* Sin/cos lookup table (values * 1000) for 0-59 (6 degree steps) */
static const int sin_table[60] = {
       0,  105,  208,  309,  407,  500,  588,  669,  743,  809,
     866,  914,  951,  978,  995, 1000,  995,  978,  951,  914,
     866,  809,  743,  669,  588,  500,  407,  309,  208,  105,
       0, -105, -208, -309, -407, -500, -588, -669, -743, -809,
    -866, -914, -951, -978, -995,-1000, -995, -978, -951, -914,
    -866, -809, -743, -669, -588, -500, -407, -309, -208, -105
};

static inline int get_sin(int index) {
    while (index < 0) index += 60;
    return sin_table[index % 60];
}

static inline int get_cos(int index) {
    return get_sin(index + 15);
}

static void *form = 0;
static int last_second = -1;
static int last_minute = -1;
static int last_hour = -1;

static void draw_hand(int cx, int cy, int length, int angle_idx, uint8_t color) {
    int sin_val = get_sin(angle_idx);
    int cos_val = get_cos(angle_idx);
    int ex = cx + (length * sin_val) / 1000;
    int ey = cy - (length * cos_val) / 1000;
    sys_gfx_line(cx, cy, ex, ey, color);
}

static void draw_face(int cx, int cy, int r) {
    sys_gfx_circle(cx, cy, r, COLOR_BLACK);
    sys_gfx_circle(cx, cy, r - 2, COLOR_DARK_GRAY);

    for (int i = 0; i < 12; i++) {
        int angle_idx = i * 5;
        int sin_val = get_sin(angle_idx);
        int cos_val = get_cos(angle_idx);
        int inner_r = r - 10;
        int outer_r = r - 5;
        int ix = cx + (inner_r * sin_val) / 1000;
        int iy = cy - (inner_r * cos_val) / 1000;
        int ox = cx + (outer_r * sin_val) / 1000;
        int oy = cy - (outer_r * cos_val) / 1000;
        sys_gfx_line(ix, iy, ox, oy, COLOR_LIGHT_GRAY);
    }
}

static void redraw_clock(void) {
    if (!form) return;

    gui_form_t *f = form;
    if (f->win.is_minimized) return;
    int cx = f->win.x + CLOCK_CENTER_X;
    int cy = f->win.y + 20 + CLOCK_CENTER_Y;

    sys_theme_t *t = sys_win_get_theme();

    /* Clear clock area */
    sys_gfx_fillrect(f->win.x + 5, f->win.y + 25, WIN_W - 10, WIN_H - 30, t->bg_color);

    draw_face(cx, cy, CLOCK_RADIUS);

    sys_time_t time = {0};
    sys_get_time(&time);

    int hour_pos = ((time.hour % 12) * 5) + (time.minute / 12);
    int min_pos = time.minute;
    int sec_pos = time.second;

    draw_hand(cx, cy, CLOCK_RADIUS * 5 / 10, hour_pos, COLOR_BLACK);
    draw_hand(cx, cy, CLOCK_RADIUS * 7 / 10, min_pos, COLOR_BLACK);
    draw_hand(cx, cy, CLOCK_RADIUS * 8 / 10, sec_pos, COLOR_LIGHT_RED);

    sys_gfx_fillrect(cx - 2, cy - 2, 4, 4, COLOR_BLUE);

    last_hour = time.hour;
    last_minute = time.minute;
    last_second = time.second;
}

__attribute__((section(".entry"), used))
void _start(void) {

    form = sys_win_create_form("Clock", 530, 0, WIN_W, WIN_H);
    if (!form) {
        sys_exit();
        return;
    }
    sys_win_set_icon(form, "C:/ICONS/CLOCK.ICO");

    sys_win_draw(form);
    redraw_clock();
    sys_win_redraw_all();

    /* Main event loop */
    int running = 1;
    while (running) {

        int event = sys_win_pump_events(form);

        /* Handle close request from window menu */
        if (event == -3) {
            running = 0;
            continue;
        }

        /* Handle window state changes */
        if (event == -1 || event == -2) {
            gui_form_t *f = form;
            if (!f->win.is_minimized) {
                /* Window moved/restored - force redraw */
                last_second = -1;
                sys_win_draw(form);
                redraw_clock();
            }
            sys_win_redraw_all();
        }

        /* Update clock if second changed */
        gui_form_t *f = form;
        if (!f->win.is_minimized) {
            sys_time_t time = {0};
            sys_get_time(&time);
            if (time.second != last_second) {
                redraw_clock();
            }
        }

        sys_yield();
    }

    sys_win_destroy_form(form);
    sys_exit();
}
