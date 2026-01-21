#pragma once
#include <stdint.h>

#define MENU_MAX_ITEMS 16
#define MENU_ITEM_HEIGHT 18
#define MENU_ITEM_PADDING 4
#define MENU_MIN_WIDTH 80

#define MENU_ITEM_ENABLED   0x01
#define MENU_ITEM_SEPARATOR 0x02
#define MENU_ITEM_CHECKED   0x04

#define MENU_ACTION_NONE     0
#define MENU_ACTION_MINIMIZE 1
#define MENU_ACTION_CLOSE    2

typedef struct {
    char text[32];
    uint8_t flags;
    int16_t action_id;
} menu_item_t;

typedef struct {
    int x, y;
    int w, h;
    int visible;
    int hovered_item;
    menu_item_t items[MENU_MAX_ITEMS];
    int item_count;
    uint8_t *saved_bg;
} menu_t;

void menu_init(menu_t *menu);
void menu_destroy(menu_t *menu);
int menu_add_item(menu_t *menu, const char *text, int16_t action_id, uint8_t flags);
int menu_add_separator(menu_t *menu);
void menu_clear(menu_t *menu);
void menu_show(menu_t *menu, int x, int y);
void menu_hide(menu_t *menu);
void menu_draw(menu_t *menu);
int menu_handle_mouse(menu_t *menu, int mx, int my, int button_pressed, int button_released);
int menu_contains_point(menu_t *menu, int mx, int my);