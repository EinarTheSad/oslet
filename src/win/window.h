#pragma once
#include <stdint.h>
#include "../fonts/bmf.h"
#include "icon.h"

// Forward declarations
typedef struct bitmap_s bitmap_t;

typedef struct {
    int x, y, w, h;
    char title[64];
    int is_visible;
    int dirty;
    uint8_t *saved_bg;
    int is_minimized;
    icon_t *minimized_icon;  // Icon when minimized (NULL if not minimized)
} window_t;

typedef struct {
    window_t base;
    char message[256];
    char button_text[32];
    int button_x, button_y;
    int button_w, button_h;
} msgbox_t;

/* System initialization */
void win_init_fonts(void);

/* Core window operations */
void win_create(window_t *win, int x, int y, int w, int h, const char *title);
void win_draw(window_t *win);
void win_destroy(window_t *win);
int win_is_titlebar(window_t *win, int mx, int my);
int win_is_minimize_button(window_t *win, int mx, int my);
void win_minimize(window_t *win, int icon_x, int icon_y, const char *icon_path);
void win_restore(window_t *win);
int win_is_icon_clicked(window_t *win, int mx, int my);
void win_move(window_t *win, int dx, int dy);
void win_save_background(window_t *win);
void win_restore_background(window_t *win);

/* Drawing primitives */
void win_draw_frame(int x, int y, int w, int h);
void win_draw_titlebar(int x, int y, int w, const char *title);
void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label, int pressed);

/* MsgBox - specialized window */
void win_msgbox_create(msgbox_t *box, const char *msg, const char *btn, const char *title);
void win_msgbox_draw(msgbox_t *box);
int win_msgbox_handle_click(msgbox_t *box, int mx, int my);

/* Control drawing */
void win_draw_control(window_t *win, void *ctrl);