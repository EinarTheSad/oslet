#pragma once
#include <stdint.h>
#include "../fonts/bmf.h"

typedef struct {
    int x, y, w, h;
    char title[64];
    int is_visible;
    int is_modal;
    int dirty;
    int prev_x, prev_y;
    uint8_t *saved_bg;
} window_t;

typedef struct {
    window_t base;
    char message[256];
    char button_text[32];
    int button_x, button_y;
    int button_w, button_h;
} msgbox_t;

typedef struct {
    int x, y;
    char text[128];
    uint8_t fg_color;
    bmf_font_t *font;
    int font_size;
} label_t;

typedef struct {
    int x, y, w, h;
    char bitmap_path[64];
    uint8_t *image_data;
    int loaded;
} picturebox_t;

/* System initialization */
void win_init_fonts(void);

/* Core window operations */
void win_create(window_t *win, int x, int y, int w, int h, const char *title);
void win_draw(window_t *win);
void win_destroy(window_t *win);
int win_is_titlebar(window_t *win, int mx, int my);
void win_move(window_t *win, int dx, int dy);
void win_mark_dirty(window_t *win);
void win_clear_dirty(window_t *win);
int win_needs_redraw(window_t *win);
void win_save_background(window_t *win);
void win_restore_background(window_t *win);

/* Drawing primitives */
void win_draw_frame(int x, int y, int w, int h);
void win_draw_titlebar(int x, int y, int w, const char *title);
void win_draw_button(int x, int y, int w, int h, uint8_t color, const char *label);

/* MsgBox - specialized window */
void win_msgbox_create(msgbox_t *box, const char *msg, const char *btn, const char *title);
void win_msgbox_draw(msgbox_t *box);
int win_msgbox_handle_click(msgbox_t *box, int mx, int my);

/* Control drawing */
void win_draw_control(window_t *win, void *ctrl);
void win_draw_label(window_t *win, label_t *label);
void win_draw_picturebox(window_t *win, picturebox_t *pbox);
int win_picturebox_load(picturebox_t *pbox, const char *path);
void win_picturebox_free(picturebox_t *pbox);