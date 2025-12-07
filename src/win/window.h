#pragma once
#include <stdint.h>

typedef struct {
    int x, y, w, h;
    char title[64];
    int is_visible;
    int is_modal;
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

/* Drawing primitives */
void win_draw_frame(int x, int y, int w, int h);
void win_draw_titlebar(int x, int y, int w, const char *title);
void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label);

/* MsgBox - specialized window */
void win_msgbox_create(msgbox_t *box, const char *msg, const char *btn, const char *title);
void win_msgbox_draw(msgbox_t *box);
int win_msgbox_handle_click(msgbox_t *box, int mx, int my);