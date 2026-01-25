#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/stdlib.h"
#include "../lib/string.h"
#include "../lib/ini.h"
#include "../lib/fonts.h"
#include "progman.h"

#define TASKBAR_HEIGHT 27
#define TASKBAR_Y (480 - TASKBAR_HEIGHT)
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"

typedef struct {
    uint8_t color;
    char wallpaper[128];
} desktop_settings_t;

static desktop_settings_t settings;

/* Cached wallpaper for fast redraw */
static gfx_cached_bmp_t cached_wallpaper = {0};
static int wallpaper_x = 0;
static int wallpaper_y = 0;

/* Parse INI file and load desktop settings */
static void load_settings(void) {
    /* Set defaults */
    settings.color = 7;
    settings.wallpaper[0] = '\0';

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) return;

    char buffer[512];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);

    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buffer);

    const char *val;

    val = ini_get(&ini, "DESKTOP", "COLOR");
    if (val) {
        int c = atoi(val);
        if (c >= 0 && c <= 15) {
            settings.color = (uint8_t)c;
        }
    }

    val = ini_get(&ini, "DESKTOP", "WALLPAPER");
    if (val && val[0] != '\0') {
        strncpy(settings.wallpaper, val, sizeof(settings.wallpaper) - 1);
        settings.wallpaper[sizeof(settings.wallpaper) - 1] = '\0';
    }
}

/* Load wallpaper into memory cache (call once at startup) */
static void cache_wallpaper(void) {
    if (settings.wallpaper[0] == '\0') return;

    if (sys_gfx_cache_bmp(settings.wallpaper, &cached_wallpaper) != 0) {
        cached_wallpaper.data = 0;
        return;
    }

    /* Calculate centered position */
    wallpaper_x = (SCREEN_WIDTH - cached_wallpaper.width) / 2;
    wallpaper_y = (SCREEN_HEIGHT - cached_wallpaper.height) / 2;

    if (wallpaper_x < 0) wallpaper_x = 0;
    if (wallpaper_y < 0) wallpaper_y = 0;
}

/* Draw wallpaper from cache (fast) */
static void draw_wallpaper(void) {
    if (!cached_wallpaper.data) return;
    sys_gfx_draw_cached(&cached_wallpaper, wallpaper_x, wallpaper_y, 0);
}

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
    sys_gfx_fillrect(0, 0, SCREEN_WIDTH, TASKBAR_Y, settings.color);
    draw_wallpaper();
    taskbar_draw();
    clock_draw();
    sys_win_invalidate_icons();
    sys_win_redraw_all();
}

/* Fast redraw - only windows, no wallpaper (for minor visual changes) */
static void desktop_redraw_fast(void) {
    sys_win_redraw_all();
}

/* Partial redraw - only dirty rectangle area */
static void desktop_redraw_rect(int x, int y, int w, int h) {
    /* Clip to desktop area (above taskbar) */
    if (y + h > TASKBAR_Y) h = TASKBAR_Y - y;
    if (y < 0) { h += y; y = 0; }
    if (x < 0) { w += x; x = 0; }
    if (w <= 0 || h <= 0) return;

    /* Fill background color in dirty area */
    sys_gfx_fillrect(x, y, w, h, settings.color);

    /* Redraw wallpaper portion if we have one */
    if (cached_wallpaper.data) {
        /* Calculate overlap between dirty rect and wallpaper */
        int wp_x2 = wallpaper_x + cached_wallpaper.width;
        int wp_y2 = wallpaper_y + cached_wallpaper.height;
        int dx2 = x + w;
        int dy2 = y + h;

        if (x < wp_x2 && dx2 > wallpaper_x && y < wp_y2 && dy2 > wallpaper_y) {
            /* There's overlap - redraw whole wallpaper for simplicity */
            draw_wallpaper();
        }
    }

    /* Redraw all windows (they clip themselves) */
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

                if (event == -1) {
                    /* Minor visual change (icon selection) - fast redraw */
                    desktop_redraw_fast();
                } else if (event == -2) {
                    /* Major window state change - full redraw */
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

    /* Load desktop settings from INI file */
    load_settings();

    /* Cache wallpaper in memory for fast redraws */
    cache_wallpaper();

    /* Draw desktop background and wallpaper */
    sys_gfx_fillrect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, settings.color);
    draw_wallpaper();

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

        /* Check if redraw is needed */
        int redraw_type = sys_win_check_redraw();
        if (redraw_type == 1) {
            /* Full redraw (window destroyed) */
            desktop_redraw();
        } else if (redraw_type == 2) {
            /* Partial redraw (window moved) */
            int dirty[4];
            sys_win_get_dirty_rect(dirty);
            desktop_redraw_rect(dirty[0], dirty[1], dirty[2], dirty[3]);
        }

        sys_mouse_draw_cursor(mx, my, 0);
        sys_gfx_swap();
        sys_yield();
    }

    sys_gfx_exit();
    sys_exit();
}
