#pragma once
#include <stdint.h>
#include "../fonts/bmf.h"
#include "icon.h"

typedef struct bitmap_s bitmap_t;

typedef struct {
    int x, y, w, h;
    char title[64];
    int is_visible;
    int dirty;
    uint8_t *saved_bg;
    int is_minimized;
    icon_t *minimized_icon;
} window_t;

typedef enum { MB_ICON_NONE = 0, MB_ICON_INFO, MB_ICON_WARN, MB_ICON_ERROR } msgbox_icon_t;

typedef struct {
    window_t base;
    char message[256];
    char icon[64]; /* optional icon token or path (e.g., "info", "warn", "error") */
    char buttons[3][32];
    int button_count;
    int default_button; /* 0-based index */
    int button_x[3]; /* relative to window */
    int button_y;    /* relative to window */
    int button_w[3];
    int button_h;    
} msgbox_t;

void win_init_fonts(void);
void win_create(window_t *win, int x, int y, int w, int h, const char *title);
void win_draw(window_t *win);
void win_draw_focused(window_t *win, int is_focused);
void win_destroy(window_t *win);
int win_is_titlebar(window_t *win, int mx, int my);
int win_is_minimize_button(window_t *win, int mx, int my);
void win_minimize(window_t *win, int icon_x, int icon_y, const char *icon_path);
void win_restore(window_t *win);
int win_is_icon_clicked(window_t *win, int mx, int my);
void win_move(window_t *win, int dx, int dy);
void win_save_background(window_t *win);
void win_restore_background(window_t *win);


void win_draw_frame(int x, int y, int w, int h);
void win_draw_titlebar(int x, int y, int w, const char *title, int is_active);
void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label, int pressed);
void win_draw_3d_border_raised(int x, int y, int w, int h);
void win_draw_3d_border_sunken(int x, int y, int w, int h);


void win_msgbox_create(msgbox_t *box, const char *msg, const char *btn, const char *title);
void win_msgbox_draw(msgbox_t *box);
int win_msgbox_handle_click(msgbox_t *box, int mx, int my);


void win_draw_control(window_t *win, void *ctrl);
void win_draw_dropdown_list(window_t *win, void *ctrl);