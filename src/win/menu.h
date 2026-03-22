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
#define MENU_ACTION_MAXIMIZE 2
#define MENU_ACTION_RESTORE  3
#define MENU_ACTION_CLOSE    4

#define MENUBAR_MAX_MENUS 8
#define MENUBAR_HEIGHT 18

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
    int just_opened;
} menu_t;

typedef struct {
    char title[16];
    menu_t menu;
    int title_width;
} menubar_entry_t;

typedef struct {
    menubar_entry_t menus[MENUBAR_MAX_MENUS];
    int menu_count;
    int active_menu;
    int hovered_menu;
    int visible;
} menubar_t;

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

void menubar_init(menubar_t *menubar);
void menubar_destroy(menubar_t *menubar);
int menubar_add_menu(menubar_t *menubar, const char *title);
menu_t* menubar_get_menu(menubar_t *menubar, int index);
void menubar_draw(menubar_t *menubar, int win_x, int win_y, int win_w);
int menubar_handle_mouse(menubar_t *menubar, int win_x, int win_y, int mx, int my, int button_pressed, int button_released);
int menubar_get_height(menubar_t *menubar);
void menubar_close_all(menubar_t *menubar);