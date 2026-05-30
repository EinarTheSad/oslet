#include "gix_app.h"

static void *gix_windows[GIX_APP_MAX_WINDOWS];
static int gix_window_count = 0;
static void *gix_main_form = NULL;
static int gix_running = 0;

static int gix_find_window(void *form) {
    for (int i = 0; i < gix_window_count; i++) {
        if (gix_windows[i] == form) return i;
    }
    return -1;
}

static int gix_register_window(void *form) {
    if (!form || gix_window_count >= GIX_APP_MAX_WINDOWS) return -1;
    gix_windows[gix_window_count++] = form;
    return 0;
}

static void gix_setup_menus(void *form, const gix_app_menu_t *menus, int menu_count) {
    if (!menus || menu_count <= 0) return;

    sys_win_menubar_enable(form);
    for (int i = 0; i < menu_count; i++) {
        int menu = sys_win_menubar_add_menu(form, menus[i].title);
        for (int j = 0; j < menus[i].item_count; j++) {
            sys_win_menubar_add_item(form, menu,
                                     menus[i].items[j].text,
                                     menus[i].items[j].action_id);
        }
    }
}

void *gix_app_create_window(const gix_app_window_desc_t *desc) {
    void *form;

    if (!desc) return NULL;
    form = sys_win_create_form(desc->title, desc->x, desc->y, desc->w, desc->h);
    if (!form) return NULL;

    if (desc->icon_path && desc->icon_path[0]) {
        sys_win_set_icon(form, desc->icon_path);
    }
    sys_win_set_resizable(form, desc->resizable);

    for (int i = 0; i < desc->control_count; i++) {
        sys_win_add_control(form, (gui_control_t *)&desc->controls[i]);
    }

    gix_setup_menus(form, desc->menus, desc->menu_count);

    if (gix_register_window(form) != 0) {
        sys_win_destroy_form(form);
        return NULL;
    }

    sys_win_draw(form);
    sys_win_force_full_redraw();
    return form;
}

void gix_app_destroy_window(void *form) {
    int idx = gix_find_window(form);
    if (idx < 0) return;

    sys_win_destroy_form(form);
    for (int i = idx; i < gix_window_count - 1; i++) {
        gix_windows[i] = gix_windows[i + 1];
    }
    gix_window_count--;
    gix_windows[gix_window_count] = NULL;
    if (gix_main_form == form) gix_main_form = NULL;
}

void gix_app_request_exit(void) {
    gix_running = 0;
}

void *gix_app_get_main_form(void) {
    return gix_main_form;
}

void gix_app_run(const gix_app_desc_t *desc) {
    void *form;

    if (!desc) {
        sys_exit();
        return;
    }

    form = sys_win_create_form(desc->title, desc->x, desc->y, desc->w, desc->h);
    if (!form) {
        sys_exit();
        return;
    }

    gix_window_count = 0;
    gix_main_form = form;
    gix_running = 1;
    gix_register_window(form);

    if (desc->icon_path && desc->icon_path[0]) {
        sys_win_set_icon(form, desc->icon_path);
    }
    sys_win_set_resizable(form, desc->resizable);

    for (int i = 0; i < desc->control_count; i++) {
        sys_win_add_control(form, (gui_control_t *)&desc->controls[i]);
    }

    gix_setup_menus(form, desc->menus, desc->menu_count);

    if (desc->on_init) {
        desc->on_init(form, desc->userdata);
    }

    sys_win_draw(form);
    sys_win_force_full_redraw();

    while (gix_running) {
        for (int i = 0; i < gix_window_count && gix_running; i++) {
            void *active_form = gix_windows[i];
            int event = sys_win_pump_events(active_form);
            int handled_exit = 0;

            if (event == SYS_WIN_EVENT_REDRAW) {
                sys_win_mark_dirty(active_form);
            }

            if (event == SYS_WIN_EVENT_WINDOW_CHANGED) {
                sys_win_draw(active_form);
                sys_win_force_full_redraw();
                sys_win_invalidate_icons();
            }

            if (event == SYS_WIN_EVENT_RESIZE) {
                if (active_form == gix_main_form && desc->on_resize) {
                    desc->on_resize(active_form, desc->userdata);
                }
                sys_win_draw(active_form);
                sys_win_force_full_redraw();
                sys_win_invalidate_icons();
            }

            if (event != 0 && desc->on_event &&
                desc->on_event(active_form, event, desc->userdata)) {
                gix_running = 0;
                handled_exit = 1;
            }

            if (event == SYS_WIN_EVENT_CLOSE && !handled_exit && gix_running) {
                if (active_form == gix_main_form) {
                    gix_running = 0;
                } else if (gix_find_window(active_form) >= 0) {
                    gix_app_destroy_window(active_form);
                    i--;
                }
            }
        }

        if (desc->on_tick && gix_running) {
            desc->on_tick(gix_main_form, desc->userdata);
        }

        sys_yield();
    }

    if (desc->on_cleanup) {
        desc->on_cleanup(form, desc->userdata);
    }

    while (gix_window_count > 0) {
        gix_app_destroy_window(gix_windows[gix_window_count - 1]);
    }

    sys_exit();
}
