#pragma once

#include <stddef.h>
#include "../syscall.h"

typedef int (*gix_app_event_fn)(void *form, int event, void *userdata);
typedef void (*gix_app_callback_fn)(void *form, void *userdata);

typedef struct {
    const char *text;
    int action_id;
} gix_app_menu_item_t;

typedef struct {
    const char *title;
    const gix_app_menu_item_t *items;
    int item_count;
} gix_app_menu_t;

typedef struct {
    const char *title;
    const char *icon_path;
    int x;
    int y;
    int w;
    int h;
    int resizable;
    const gui_control_t *controls;
    int control_count;
    const gix_app_menu_t *menus;
    int menu_count;
    gix_app_event_fn on_event;
    gix_app_callback_fn on_init;
    gix_app_callback_fn on_resize;
    gix_app_callback_fn on_tick;
    gix_app_callback_fn on_cleanup;
    void *userdata;
} gix_app_desc_t;

#define GIX_APP_MAX_WINDOWS 4

typedef struct {
    const char *title;
    const char *icon_path;
    int x;
    int y;
    int w;
    int h;
    int resizable;
    const gui_control_t *controls;
    int control_count;
    const gix_app_menu_t *menus;
    int menu_count;
} gix_app_window_desc_t;

void gix_app_run(const gix_app_desc_t *desc);
void *gix_app_create_window(const gix_app_window_desc_t *desc);
void gix_app_destroy_window(void *form);
void gix_app_request_exit(void);
void *gix_app_get_main_form(void);
