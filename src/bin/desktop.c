#include "../syscall.h"
#include "../win/wm_config.h"
#include "../lib/wallpaper.h"
#include "../lib/rect.h"
#include "../lib/stdio.h"
#include "../lib/stdlib.h"
#include "../lib/string.h"
#include "../lib/ini.h"
#include "../lib/fonts.h"
#include "progman.h"
#define TASKBAR_Y (WM_SCREEN_HEIGHT - WM_TASKBAR_HEIGHT)
#define SETTINGS_PATH "C:/OSLET/SYSTEM.INI"
#define MAX_TASKBAR_BUTTONS 16
#define KEY_ALT_TAB 0xC0
#define KEY_ALT_RELEASE 0xC1

typedef struct {
    uint8_t color;
    char wallpaper[128];
    uint8_t wallpaper_mode; /* 0=center, 1=stretch */
    uint8_t volume; /* 0..100, imported from [SOUND] VOL */
} desktop_settings_t;

typedef struct {
    int x, y, w, h;
    int pressed;
    char label[6];
    char icon_path[128];
    char action[64]; /* progman module name to launch */
} taskbar_button_t;

static taskbar_button_t taskbar_buttons[MAX_TASKBAR_BUTTONS];
static int taskbar_button_count = 0; 

extern const progmod_t startman_module;
extern const progmod_t textmode_module;
extern const progmod_t cpl_theme_module;
extern const progmod_t cpl_screen_module;
extern const progmod_t cpl_boot_module;
extern const progmod_t shutdown_module;
extern const progmod_t volume_module;

desktop_settings_t settings;  /* Global - cpanel needs access */
usr_bmf_font_t font_b;
usr_bmf_font_t font_n;
sys_time_t current = {0};

/* Cached wallpaper for fast redraw (center mode only) */
static gfx_cached_bmp_t cached_wallpaper = {0};
static int wallpaper_x = 0;
static int wallpaper_y = 0;

static int taskbar_add_button(const char *icon_path, const char *action) {
    if (taskbar_button_count >= MAX_TASKBAR_BUTTONS) return -1;
    int idx = taskbar_button_count++;
    taskbar_buttons[idx].w = 21;
    taskbar_buttons[idx].h = 21;
    taskbar_buttons[idx].pressed = 0;
    taskbar_buttons[idx].label[0] = '\0';
    if (icon_path) {
        strncpy(taskbar_buttons[idx].icon_path, icon_path, sizeof(taskbar_buttons[idx].icon_path)-1);
        taskbar_buttons[idx].icon_path[sizeof(taskbar_buttons[idx].icon_path)-1] = '\0';
    } else {
        taskbar_buttons[idx].icon_path[0] = '\0';
    }
    if (action) {
        strncpy(taskbar_buttons[idx].action, action, sizeof(taskbar_buttons[idx].action)-1);
        taskbar_buttons[idx].action[sizeof(taskbar_buttons[idx].action)-1] = '\0';
    } else {
        taskbar_buttons[idx].action[0] = '\0';
    }

    /* Layout buttons horizontally after the start button */
    if (taskbar_button_count > 1) {
        int x = taskbar_buttons[0].x + taskbar_buttons[0].w + 3;
        for (int i = 1; i < taskbar_button_count; i++) {
            taskbar_buttons[i].x = x;
            taskbar_buttons[i].y = TASKBAR_Y + 3;
            x += taskbar_buttons[i].w + 3;
        }
    }
    return idx;
} 
static int last_clock_hour = -1;
static int last_clock_minute = -1;
/* Sound icon state for the little speaker left of the clock */
static int sound_icon_pressed = 0;

/* Parse INI file and load desktop settings */
static void load_settings(void) {
    /* Set defaults */
    settings.color = 7;
    settings.wallpaper[0] = '\0';
    settings.wallpaper_mode = 0; /* center by default */
    settings.volume = 66; /* match default used by volume control */

    int fd = sys_open(SETTINGS_PATH, "r");
    if (fd < 0) return;

    char buffer[1024];
    int bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
    sys_close(fd);

    if (bytes <= 0) return;
    buffer[bytes] = '\0';

    ini_parser_t ini;
    ini_init(&ini, buffer);

    settings.color = (uint8_t)ini_get_color(&ini, "DESKTOP", "COLOR", settings.color);

    const char *val = ini_get(&ini, "DESKTOP", "WALLPAPER");
    if (val && val[0] != '\0') {
        strncpy(settings.wallpaper, val, sizeof(settings.wallpaper) - 1);
        settings.wallpaper[sizeof(settings.wallpaper) - 1] = '\0';
    }

    val = ini_get(&ini, "DESKTOP", "MODE");
    if (val) {
        int mode = atoi(val);
        settings.wallpaper_mode = (mode == 1) ? 1 : 0;
    }

    sys_theme_t *theme = sys_win_get_theme();
    theme->bg_color = (uint8_t)ini_get_color(&ini, "THEME", "BG_COLOR", theme->bg_color);
    theme->titlebar_color = (uint8_t)ini_get_color(&ini, "THEME", "TITLEBAR_COLOR", theme->titlebar_color);
    theme->titlebar_inactive = (uint8_t)ini_get_color(&ini, "THEME", "TITLEBAR_INACTIVE", theme->titlebar_inactive);
    theme->frame_dark = (uint8_t)ini_get_color(&ini, "THEME", "FRAME_DARK", theme->frame_dark);
    theme->frame_light = (uint8_t)ini_get_color(&ini, "THEME", "FRAME_LIGHT", theme->frame_light);
    theme->text_color = (uint8_t)ini_get_color(&ini, "THEME", "TEXT_COLOR", theme->text_color);
    theme->button_color = (uint8_t)ini_get_color(&ini, "THEME", "BUTTON_COLOR", theme->button_color);
    theme->taskbar_color = (uint8_t)ini_get_color(&ini, "THEME", "TASKBAR_COLOR", theme->taskbar_color);
    theme->start_button_color = (uint8_t)ini_get_color(&ini, "THEME", "START_BUTTON_COLOR", theme->start_button_color);
    settings.volume = (uint8_t)ini_get_int(&ini, "SOUND", "VOL", settings.volume);
}

static void cache_wallpaper(void) {
    if (settings.wallpaper[0] == '\0') return;

    int x = 0, y = 0;
    if (wallpaper_cache(settings.wallpaper, &cached_wallpaper, &x, &y, settings.wallpaper_mode) != 0) {
        cached_wallpaper.data = 0;
        return;
    }
    wallpaper_x = x;
    wallpaper_y = y;
}

static void draw_wallpaper(void) {
    if (settings.wallpaper[0] == '\0') return;
    if (!cached_wallpaper.data) return;
    wallpaper_draw(&cached_wallpaper, wallpaper_x, wallpaper_y);
}

static void prog_register_all(void) {
    progman_register(&startman_module);
    progman_register(&textmode_module);
    progman_register(&cpl_theme_module);
    progman_register(&cpl_screen_module);
    progman_register(&cpl_boot_module);
    progman_register(&shutdown_module);
    progman_register(&volume_module);
}

static void taskbar_button_draw(int x, int y, int w, int h, uint8_t btn_color, const char *label, const char *icon, int pressed) {
    sys_theme_t *theme = sys_win_get_theme();
    uint8_t shad_a, shad_b;

    sys_gfx_rect(x, y, w, h, COLOR_BLACK);
    sys_gfx_fillrect(x+2, y+2, w-3, h-3, btn_color);

    if (pressed) {
        shad_a = COLOR_WHITE;
        shad_b = theme->frame_dark;
    } else {
        shad_a = theme->frame_dark;
        shad_b = COLOR_WHITE;
    }

    sys_gfx_rect(x+1, y+1, w-2, h-2, shad_a);
    sys_gfx_line(x+1, y+1, x+w-3, y+1, shad_b);
    sys_gfx_line(x+1, y+1, x+1, y+h-3, shad_b);

    /* If this is a labeled button (Start), draw icon on the left and text to the right
       otherwise center the 16x16 icon for icon-only buttons */
    if (label && label[0] != '\0' && icon && icon[0] != '\0') {
        sys_gfx_load_bmp(icon, x+3, y+2);
        if (font_b.data) {
            int text_x = x + 22;
            int text_y = y + 7;
            usr_bmf_printf(text_x, text_y, &font_b, 12, theme->text_color, "%s", label);
        }
    } else if (icon && icon[0] != '\0') {
        /* Draw 16x16 icon centered */
        int ix = x + (w - 16) / 2 + 1;
        int iy = y + (h - 16) / 2 + 1;
        sys_gfx_load_bmp(icon, ix, iy);
    } else if (font_b.data && label) {
        int text_x = x + 22;
        int text_y = y + 7;
        usr_bmf_printf(text_x, text_y, &font_b, 12, theme->text_color, "%s", label);
    }
} 

static void start_init(void) {
    /* Initialize start button as index 0 */
    taskbar_button_count = 1;
    taskbar_buttons[0].x = 3;
    taskbar_buttons[0].y = TASKBAR_Y + 3;
    taskbar_buttons[0].w = 57;
    taskbar_buttons[0].h = 21;
    taskbar_buttons[0].pressed = 0;
    strncpy(taskbar_buttons[0].label, "Start", sizeof(taskbar_buttons[0].label)-1);
    taskbar_buttons[0].label[sizeof(taskbar_buttons[0].label)-1] = '\0';
    taskbar_buttons[0].action[0] = '\0';
    strncpy(taskbar_buttons[0].icon_path, "C:/ICONS/LET.ICO", sizeof(taskbar_buttons[0].icon_path)-1);
    taskbar_buttons[0].icon_path[sizeof(taskbar_buttons[0].icon_path)-1] = '\0';
} 

static void clock_draw(void) {
    sys_theme_t *theme = sys_win_get_theme();
    sys_get_time(&current);
    sys_gfx_fillrect(WM_SCREEN_WIDTH-39, TASKBAR_Y + 9, 34, 11, theme->taskbar_color);
    usr_bmf_printf(WM_SCREEN_WIDTH-38, TASKBAR_Y + 10, &font_n, 12, theme->text_color, "%02u:%02u", current.hour, current.minute);
    last_clock_hour = current.hour;
    last_clock_minute = current.minute;
}

static int clock_update(void) {
    sys_get_time(&current);
    if (current.hour != last_clock_hour || current.minute != last_clock_minute) {
        clock_draw();
        sys_win_mark_dirty_rect(WM_SCREEN_WIDTH-60, TASKBAR_Y + 3, 57, 21);
        return 1;
    }
    return 0;
}

static void taskbar_draw(void) {
    sys_theme_t *theme = sys_win_get_theme();
    sys_gfx_fillrect(0, TASKBAR_Y, WM_SCREEN_WIDTH, WM_TASKBAR_HEIGHT, theme->taskbar_color);
    sys_gfx_line(0, TASKBAR_Y, WM_SCREEN_WIDTH, TASKBAR_Y, COLOR_WHITE);

    sys_gfx_rect(WM_SCREEN_WIDTH-60, TASKBAR_Y + 3, 57, 21, theme->frame_dark);
    sys_gfx_line(WM_SCREEN_WIDTH-59, TASKBAR_Y + 23, WM_SCREEN_WIDTH-4, TASKBAR_Y + 23, COLOR_WHITE);
    sys_gfx_line(WM_SCREEN_WIDTH-4, TASKBAR_Y + 4, WM_SCREEN_WIDTH-4, TASKBAR_Y + 23, COLOR_WHITE);

    for (int i = 0; i < taskbar_button_count; i++) {
        taskbar_button_t *b = &taskbar_buttons[i];
        const char *label = b->label[0] ? b->label : NULL;
        const char *icon = b->icon_path[0] ? b->icon_path : NULL;
        uint8_t color = (i == 0) ? theme->start_button_color : theme->button_color;
        taskbar_button_draw(b->x, b->y, b->w, b->h, color, label, icon, b->pressed);
    }

    /* Volume control applet icon */
    int sx = WM_SCREEN_WIDTH - 61;
    int sy = TASKBAR_Y + 3;
    int sw = 21;
    int sh = 21;
    int ix = sx + (sw - 16) / 2 + 1;
    int iy = sy + (sh - 16) / 2 + 1;
    sys_gfx_load_bmp("C:/icons/snd.ico", ix, iy);
} 

static int taskbar_click(int mx, int my, unsigned char mb, int *state_changed) {
    static unsigned char last_mb = 0;
    int clicked_idx = -1;
    int change = 0;

    for (int i = 0; i < taskbar_button_count; i++) {
        taskbar_button_t *b = &taskbar_buttons[i];
        int old_pressed = b->pressed;

        if (mx >= b->x && mx < b->x + b->w &&
            my >= b->y && my < b->y + b->h) {

            if (mb & 1) {
                b->pressed = 1;
            } else if ((last_mb & 1) && !(mb & 1)) {
                b->pressed = 0;
                clicked_idx = i;
            }
        } else {
            if (!(mb & 1)) {
                b->pressed = 0;
            }
        }

        if (old_pressed != b->pressed) change = 1;
    }

    /* Sound icon (right side, left of clock) - separate from taskbar_buttons[] */
    {
        int sx = WM_SCREEN_WIDTH - 61;
        int sy = TASKBAR_Y + 3;
        int sw = 21;
        int sh = 21;
        int old = sound_icon_pressed;
        int sound_clicked = 0;

        if (mx >= sx && mx < sx + sw && my >= sy && my < sy + sh) {
            if (mb & 1) {
                sound_icon_pressed = 1;
            } else if ((last_mb & 1) && !(mb & 1)) {
                sound_icon_pressed = 0;
                sound_clicked = 1;
            }
        } else {
            if (!(mb & 1)) sound_icon_pressed = 0;
        }

        if (old != sound_icon_pressed) change = 1;

        if (sound_clicked) {
            /* Launch or restore the Volume module (same behavior as other taskbar items) */
            if (!progman_is_running("Volume")) {
                progman_launch("Volume");
            } else {
                int found = 0;
                int count = progman_get_running_count();
                for (int i = 0; i < count; i++) {
                    prog_instance_t *other = progman_get_instance(i);
                    if (!other || !other->module) continue;
                    if (strcmp(other->module->name, "Volume") != 0) continue;

                    if (other->window_count > 0 && other->windows[0]) {
                        sys_win_restore_form(other->windows[0]);
                        found = 1;
                        break;
                    }
                }

                if (!found) progman_launch("Volume");
            }
        }
    }

    if (change) *state_changed = 1;
    last_mb = mb;
    return clicked_idx;
} 

static void desktop_redraw(void) {
    sys_gfx_fillrect(0, 0, WM_SCREEN_WIDTH, TASKBAR_Y, settings.color);
    draw_wallpaper();
    taskbar_draw();
    clock_draw();
    sys_win_invalidate_icons();
    sys_win_redraw_all();
}

/* Called by cpanel */
void desktop_apply_settings(uint8_t color, const char *wallpaper, uint8_t wallpaper_mode) {
    int wallpaper_changed = strcmp(settings.wallpaper, wallpaper) != 0;
    int mode_changed = (settings.wallpaper_mode != wallpaper_mode);

    settings.color = color;
    settings.wallpaper_mode = wallpaper_mode;
    strncpy(settings.wallpaper, wallpaper, sizeof(settings.wallpaper) - 1);

    /* Recache if wallpaper path or mode changed */
    if (wallpaper_changed || mode_changed) {
        if (cached_wallpaper.data) {
            sys_gfx_free_cached(&cached_wallpaper);
            cached_wallpaper.data = 0;
        }
        cache_wallpaper();
        /* Force full redraw to prevent stale window backgrounds */
        sys_win_force_full_redraw();
    }

    desktop_redraw();
}

static void desktop_redraw_rect(int x, int y, int w, int h) {
    /* Keep original rect to detect taskbar overlap */
    int orig_y = y, orig_h = h;
    int taskbar_dirty = 0;

    /* If the dirty rect intersects the taskbar area, mark taskbar as dirty */
    if (orig_y < WM_SCREEN_HEIGHT && (orig_y + orig_h) > TASKBAR_Y) {
        taskbar_dirty = 1;
    }

    /* Clip to desktop area (above taskbar) */
    if (y + h > TASKBAR_Y) h = TASKBAR_Y - y;
    if (y < 0) { h += y; y = 0; }
    if (x < 0) { w += x; x = 0; }

    /* Redraw desktop portion (if any) */
    if (w > 0 && h > 0) {
        sys_gfx_fillrect(x, y, w, h, settings.color);

        /* Redraw wallpaper portion */
        if (settings.wallpaper[0] != '\0' && cached_wallpaper.data) {
            if (settings.wallpaper_mode == 1) {
                /* Stretch mode: redraw entire cached wallpaper (640x480) */
                draw_wallpaper();
                /* Redraw taskbar since wallpaper covers entire screen */
                taskbar_draw();
                clock_draw();
                taskbar_dirty = 0; /* already handled */
            } else {
                /* Center mode: draw partial wallpaper if it intersects the dirty rect */
                int ix, iy, iw, ih;
                if (rect_intersect(x, y, w, h,
                                   wallpaper_x, wallpaper_y, cached_wallpaper.width, cached_wallpaper.height,
                                   &ix, &iy, &iw, &ih)) {
                    int src_x = ix - wallpaper_x;
                    int src_y = iy - wallpaper_y;
                    wallpaper_draw_partial(&cached_wallpaper, ix, iy, src_x, src_y, iw, ih);
                }
            }
        }
    }

    /* If the original dirty rect touched the taskbar, redraw it now so the
       desktop/taskbar (including the clock) is painted before windows are
       composited on top. */
    if (taskbar_dirty) {
        taskbar_draw();
        clock_draw();
    }

    sys_win_invalidate_icons();

    /* Redraw windows overlapping the desktop dirty rect */
    if (w > 0 && h > 0) {
        sys_win_mark_dirty_rect(x, y, w, h);
    }

    /* Also ensure windows overlapping the taskbar area are redrawn */
    if (taskbar_dirty) {
        sys_win_mark_dirty_rect(0, TASKBAR_Y, WM_SCREEN_WIDTH, WM_TASKBAR_HEIGHT);
    }
}


static void pump_all_program_events(int mx, int my) {
    prog_instance_t *inst;
    PROGMAN_FOREACH_RUNNING(inst) {
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

                if (event == -2) {
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
    load_settings();
    cache_wallpaper();

    sys_gfx_fillrect(0, 0, WM_SCREEN_WIDTH, WM_SCREEN_HEIGHT, settings.color);
    draw_wallpaper();

    usr_bmf_import(&font_b, "C:/FONTS/LSANS_B.BMF");
    usr_bmf_import(&font_n, "C:/FONTS/LSANS.BMF");

    progman_init();
    prog_register_all();

    start_init();
    taskbar_add_button("C:/ICONS/OFF.ICO", "Shutdown");
    sys_gfx_swap();

    if (sys_sound_detected()) {
        /* apply configured volume (0..100 -> 0..31) */
        uint8_t vol = settings.volume;
        uint8_t hw_vol;
        if (vol == 0) {
            hw_vol = 0;
        } else {
            hw_vol = (vol * 31 + 50) / 100;
            if (hw_vol == 0) hw_vol = 1;
        }
        sys_sound_set_volume(hw_vol, hw_vol);
        sys_sound_play_wav("C:/SOUNDS/boot.wav");
    }

    taskbar_draw();
    clock_draw();

    int exit_requested = 0;
    int taskbar_needs_redraw = 0;
    int gk = 0;

    while (!exit_requested) {
        clock_update();
        sys_get_mouse_state(&mx, &my, &mb);

        gk = sys_get_alt_key();
        if (gk == KEY_ALT_TAB) {
            sys_win_cycle_preview();
            desktop_redraw();
        }
        if (gk == KEY_ALT_RELEASE) {
            sys_win_cycle_commit();
            desktop_redraw();
        }

        /* Handle taskbar buttons */
        int button_state_changed = 0;
        int clicked_idx = taskbar_click(mx, my, mb, &button_state_changed);
        if (clicked_idx >= 0) {
            taskbar_button_t *btn = &taskbar_buttons[clicked_idx];
            if (clicked_idx == 0) {
                if (!progman_is_running("Start Manager")) {
                    progman_launch("Start Manager");
                } else {
                    /* Try to restore/focus an existing Start Manager window (prefer main instance if present) */
                    int found = 0;
                    int count = progman_get_running_count();
                    for (int i = 0; i < count; i++) {
                        prog_instance_t *other = progman_get_instance(i);
                        if (!other || !other->module) continue;
                        if (strcmp(other->module->name, "Start Manager") != 0) continue;

                        /* Prefer an instance that has an existing window */
                        if (other->window_count > 0 && other->windows[0]) {
                            sys_win_restore_form(other->windows[0]);
                            found = 1;
                            break;
                        }
                    }

                    if (!found) {
                        /* No suitable instance window found; start a new one */
                        progman_launch("Start Manager");
                    }
                }
            } else if (btn->action[0]) {
                /* Launch or restore the module named in btn->action */
                if (!progman_is_running(btn->action)) {
                    progman_launch(btn->action);
                } else {
                    int found = 0;
                    int count = progman_get_running_count();
                    for (int i = 0; i < count; i++) {
                        prog_instance_t *other = progman_get_instance(i);
                        if (!other || !other->module) continue;
                        if (strcmp(other->module->name, btn->action) != 0) continue;

                        if (other->window_count > 0 && other->windows[0]) {
                            sys_win_restore_form(other->windows[0]);
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        progman_launch(btn->action);
                    }
                }
            }
            taskbar_needs_redraw = 1;
        }

        if (button_state_changed) {
            taskbar_needs_redraw = 1;
        }

        if (taskbar_needs_redraw) {
            taskbar_draw();
            clock_draw();
            sys_win_mark_dirty_rect(0, TASKBAR_Y, WM_SCREEN_WIDTH, WM_TASKBAR_HEIGHT);
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
