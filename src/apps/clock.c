#include "../syscall.h"
#include "../lib/stdio.h"

#define CLOCK_RADIUS 45
#define CLOCK_CENTER_X 55
#define CLOCK_CENTER_Y 55
#define WIN_W 110
#define WIN_H 130

/* Client area (where the clock face lives) */
#define CL_W (WIN_W - 10)
#define CL_H (WIN_H - 30)

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

static uint8_t *client_buf = NULL;

static inline void buf_putpixel(int x, int y, uint8_t color) {
    if (!client_buf) return;
    if (x < 0 || x >= CL_W || y < 0 || y >= CL_H) return;
    client_buf[y * CL_W + x] = color;
}

static void buf_fillrect(int x, int y, int w, int h, uint8_t color) {
    if (!client_buf) return;
    if (w <= 0 || h <= 0) return;
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w - 1;
    int y1 = y + h - 1;
    if (x1 >= CL_W) x1 = CL_W - 1;
    if (y1 >= CL_H) y1 = CL_H - 1;
    for (int py = y0; py <= y1; py++) {
        for (int px = x0; px <= x1; px++) {
            client_buf[py * CL_W + px] = color;
        }
    }
}

static inline int iabs(int v) { return v < 0 ? -v : v; }

/* Bresenham line into client buffer */
static void buf_line(int x0, int y0, int x1, int y1, uint8_t color) {
    int dx = iabs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -iabs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    while (1) {
        buf_putpixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

/* Midpoint circle into client buffer */
static void buf_circle(int cx, int cy, int r, uint8_t color) {
    if (r < 0) return;
    int x = r;
    int y = 0;
    int err = 0;
    while (x >= y) {
        buf_putpixel(cx + x, cy + y, color);
        buf_putpixel(cx - x, cy + y, color);
        buf_putpixel(cx + x, cy - y, color);
        buf_putpixel(cx - x, cy - y, color);
        buf_putpixel(cx + y, cy + x, color);
        buf_putpixel(cx - y, cy + x, color);
        buf_putpixel(cx + y, cy - x, color);
        buf_putpixel(cx - y, cy - x, color);
        if (err <= 0) {
            y += 1; err += 2*y + 1;
        }
        if (err > 0) {
            x -= 1; err -= 2*x + 1;
        }
    }
}

static void draw_hand(int cx, int cy, int length, int angle_idx, uint8_t color) {
    int sin_val = get_sin(angle_idx);
    int cos_val = get_cos(angle_idx);
    int ex = cx + (length * sin_val) / 1000;
    int ey = cy - (length * cos_val) / 1000;
    buf_line(cx, cy, ex, ey, color);
}

static void draw_face(int cx, int cy, int r) {
    buf_circle(cx, cy, r, COLOR_BLACK);
    buf_circle(cx, cy, r - 2, COLOR_DARK_GRAY);

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
        buf_line(ix, iy, ox, oy, COLOR_LIGHT_GRAY);
    }
}

static void redraw_clock(void) {
    if (!form) return;

    gui_form_t *f = form;
    if (f->win.is_minimized) return;

    sys_theme_t *t = sys_win_get_theme();

    /* Ensure client buffer exists */
    if (!client_buf) return;

    /* Fill background (client area coordinates 0..CL_W-1,0..CL_H-1) */
    buf_fillrect(0, 0, CL_W, CL_H, t->bg_color);

    /* Center inside client */
    int cx = CL_W / 2; /* equals CLOCK_CENTER_X - 5 */
    int cy = CL_H / 2; /* equals CLOCK_CENTER_Y - 5 */

    draw_face(cx, cy, CLOCK_RADIUS);

    sys_time_t time = {0};
    sys_get_time(&time);

    int hour_pos = ((time.hour % 12) * 5) + (time.minute / 12);
    int min_pos = time.minute;
    int sec_pos = time.second;

    draw_hand(cx, cy, CLOCK_RADIUS * 5 / 10, hour_pos, COLOR_BLACK);
    draw_hand(cx, cy, CLOCK_RADIUS * 7 / 10, min_pos, COLOR_BLACK);
    draw_hand(cx, cy, CLOCK_RADIUS * 8 / 10, sec_pos, COLOR_LIGHT_RED);

    /* Center dot */
    int dotx = cx - 2;
    int doty = cy - 2;
    for (int dy = 0; dy < 4; dy++) {
        for (int dx = 0; dx < 4; dx++) buf_putpixel(dotx + dx, doty + dy, COLOR_BLUE);
    }

    /* Blit client buffer into the form at (5,5) relative to client area; kernel will clip to topmost regions */
    sys_win_draw_buffer(form, client_buf, CL_W, CL_H, 0, 0, CL_W, CL_H, 5, 5, 0);

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

    /* Allocate client buffer (100x100) */
    client_buf = sys_malloc(CL_W * CL_H);
    if (!client_buf) {
        sys_win_destroy_form(form);
        sys_exit();
        return;
    }

    sys_win_draw(form);
    redraw_clock();

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
                redraw_clock();
            }
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

    if (client_buf) {
        sys_free(client_buf);
        client_buf = NULL;
    }

    sys_win_destroy_form(form);
    sys_exit();
}
