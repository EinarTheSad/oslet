#pragma once
#include <stdint.h>

typedef struct bitmap_s bitmap_t;

typedef struct {
    int x, y;
    char label[64];
    char bitmap_path[64];
    bitmap_t *bitmap;
    uint8_t *saved_bg;
    int selected;
    void *user_data;             /* Optional user data (e.g., pointer to window) */
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
