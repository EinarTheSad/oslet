#pragma once
#include <stdint.h>

typedef struct bitmap_s bitmap_t;

typedef struct {
    int x, y;
    int height;                  /* Actual height (depends on label line count) */
    char label[64];
    char bitmap_path[64];
    bitmap_t *bitmap;
    uint8_t *saved_bg;
    int selected;
    void *user_data;             /* Optional user data (e.g., pointer to window) */
    /* Drag state */
    int dragging;                /* 1 if icon is being dragged */
    int drag_offset_x;           /* Mouse offset from icon origin when drag started */
    int drag_offset_y;
    int original_x, original_y;  /* Position before drag (for cancel/snap-back) */
    int click_start_x, click_start_y;  /* Mouse position at click (for drag threshold detection) */
} icon_t;

/*
 * Simple usage:
 *   icon_t my_icon;
 *   icon_create(&my_icon, 10, 10, "My File", "C:/ICONS/FILE.ICO");
 */
void icon_create(icon_t *icon, int x, int y, const char *label, const char *bitmap_path);
void icon_draw(icon_t *icon);
void icon_hide(icon_t *icon);
void icon_destroy(icon_t *icon);
int icon_is_clicked(icon_t *icon, int mx, int my);
void icon_set_selected(icon_t *icon, int selected);
void icon_move(icon_t *icon, int new_x, int new_y);
void icon_set_label(icon_t *icon, const char *new_label);
void icon_invalidate_bg(icon_t *icon);
int icon_get_height(icon_t *icon);

/* Drag operations */
void icon_start_drag(icon_t *icon, int mouse_x, int mouse_y);
void icon_update_drag(icon_t *icon, int mouse_x, int mouse_y);
void icon_end_drag(icon_t *icon, int new_x, int new_y);
void icon_cancel_drag(icon_t *icon);

/* Shared helper functions for icon drawing (used by controls.c too) */
int icon_count_label_lines(const char *label, int max_line_width);
int icon_calc_total_height(int icon_size, int label_lines);
void icon_draw_label_wrapped(const char *label, int x, int y, int total_width,
                             int max_line_width, uint8_t color);
