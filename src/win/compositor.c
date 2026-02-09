#include "compositor.h"
#include "window.h"
#include "icon.h"
#include "menu.h"
#include "bitmap.h"
#include "controls.h"
#include "theme.h"
#include "wm_config.h"
#include "wm.h"
#include "../drivers/mouse.h"

static void compositor_draw_controls(gui_form_t *form) {
    if (!form || form->win.is_minimized || !form->controls) return;
    for (int j = 0; j < form->ctrl_count; j++) {
        form->controls[j].is_focused =
            (form->controls[j].id == form->focused_control_id) ? 1 : 0;
        win_draw_control(&form->win, &form->controls[j]);
    }
}

static void compositor_draw_dropdowns(gui_form_t *form) {
    if (!form || form->win.is_minimized || !form->controls) return;
    for (int j = 0; j < form->ctrl_count; j++) {
        if (form->controls[j].type == CTRL_DROPDOWN && form->controls[j].dropdown_open) {
            win_draw_dropdown_list(&form->win, &form->controls[j]);
        }
    }
}

static gui_control_t *compositor_get_control_by_id(gui_form_t *form, int16_t id) {
    if (!form || !form->controls) return NULL;
    for (int j = 0; j < form->ctrl_count; j++) {
        if (form->controls[j].id == id) return &form->controls[j];
    }
    return NULL;
}

static int rects_intersect(int ax, int ay, int aw, int ah,
                             int bx, int by, int bw, int bh) {
    if (aw <= 0 || ah <= 0 || bw <= 0 || bh <= 0) return 0;
    if (ax + aw <= bx) return 0;
    if (bx + bw <= ax) return 0;
    if (ay + ah <= by) return 0;
    if (by + bh <= ay) return 0;
    return 1;
}

void compositor_draw_all(window_manager_t *wm) {
    mouse_restore();

    /* If a full redraw was requested, draw everything and clear the flag */
    if (wm->needs_full_redraw) {
        wm->needs_full_redraw = 0;
        /* Fall back to full draw */
    } else if (wm->dirty_w > 0 && wm->dirty_h > 0) {
        /* Partial redraw: only draw items overlapping the dirty rect */
        int dx = wm->dirty_x;
        int dy = wm->dirty_y;
        int dw = wm->dirty_w;
        int dh = wm->dirty_h;

        /* Draw back-to-front only windows that intersect the dirty rect */
        for (int i = 0; i < wm->count; i++) {
            gui_form_t *form = wm->windows[i];
            if (!form || !form->win.is_visible) continue;

            if (form->win.is_minimized) {
                if (form->win.minimized_icon) {
                    int ix = form->win.minimized_icon->x;
                    int iy = form->win.minimized_icon->y;
                    int iw = WM_ICON_TOTAL_WIDTH;
                    int ih = icon_get_height(form->win.minimized_icon);
                    if (rects_intersect(dx, dy, dw, dh, ix, iy, iw, ih)) {
                        icon_draw(form->win.minimized_icon);
                    }
                }
            } else {
                int wx = form->win.x - WM_BG_MARGIN;
                int wy = form->win.y - WM_BG_MARGIN;
                int ww = form->win.w + (WM_BG_MARGIN * 2);
                int wh = form->win.h + (WM_BG_MARGIN * 2);

                if (rects_intersect(dx, dy, dw, dh, wx, wy, ww, wh)) {
                    int is_focused = (i == wm->focused_index);
                    win_draw_focused(&form->win, is_focused);
                    compositor_draw_controls(form);
                    if (form->window_menu.visible) {
                        menu_draw(&form->window_menu);
                    }
                } else {
                    /* Even if window doesn't intersect, its open dropdown list may */
                    if (form->controls) {
                        for (int j = 0; j < form->ctrl_count; j++) {
                            gui_control_t *ctrl = &form->controls[j];
                            if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown_open) {
                                int abs_x = form->win.x + ctrl->x;
                                int abs_y = form->win.y + ctrl->y + 20;
                                int list_y = abs_y + ctrl->h;
                                int list_h = ctrl->item_count * 16;
                                if (rects_intersect(dx, dy, dw, dh, abs_x, list_y, ctrl->w, list_h)) {
                                    win_draw_dropdown_list(&form->win, ctrl);
                                }
                            }
                        }
                    }
                }
            }
        }

        /* Also check dropdowns on all windows (for overlapping cases) */
        for (int i = 0; i < wm->count; i++) {
            gui_form_t *form = wm->windows[i];
            if (!form || !form->win.is_visible || form->win.is_minimized) continue;
            if (!form->controls) continue;

            for (int j = 0; j < form->ctrl_count; j++) {
                gui_control_t *ctrl = &form->controls[j];
                if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown_open) {
                    int abs_x = form->win.x + ctrl->x;
                    int abs_y = form->win.y + ctrl->y + 20;
                    int list_y = abs_y + ctrl->h;
                    int list_h = ctrl->item_count * 16;
                    if (rects_intersect(dx, dy, dw, dh, abs_x, list_y, ctrl->w, list_h)) {
                        win_draw_dropdown_list(&form->win, ctrl);
                    }
                }
            }
        }

        /* Clear the dirty rect after drawing */
        wm->dirty_x = wm->dirty_y = wm->dirty_w = wm->dirty_h = 0;

        mouse_invalidate_buffer();
        return;
    }

    /* Full redraw path (or when needs_full_redraw was set) */
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *form = wm->windows[i];
        if (!form || !form->win.is_visible) continue;

        int is_focused = (i == wm->focused_index);
        win_draw_focused(&form->win, is_focused);

        /* Draw controls if not minimized */
        compositor_draw_controls(form);

        /* Draw window menu if visible (must be on top of controls) */
        if (!form->win.is_minimized && form->window_menu.visible) {
            menu_draw(&form->window_menu);
        }
    }

    /* Draw open dropdown lists on top of ALL windows (last pass) */
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *form = wm->windows[i];
        if (!form || !form->win.is_visible || form->win.is_minimized) continue;
        if (!form->controls) continue;

        compositor_draw_dropdowns(form);
    }
    mouse_invalidate_buffer();
}

void compositor_draw_single(window_manager_t *wm, gui_form_t *form) {
    if (!form || !form->win.is_visible) return;
    mouse_restore();

    int is_focused = 0;
    for (int i = 0; i < wm->count; i++) {
        if (wm->windows[i] == form) {
            is_focused = (i == wm->focused_index);
            break;
        }
    }

    /* Draw the window */
    win_draw_focused(&form->win, is_focused);

    /* Draw controls if not minimized */
    compositor_draw_controls(form);

    /* Draw window menu if visible */
    if (!form->win.is_minimized && form->window_menu.visible) {
        menu_draw(&form->window_menu);
    }

    /* Draw open dropdown lists on top */
    compositor_draw_dropdowns(form);

    mouse_invalidate_buffer();
}

void compositor_draw_control_by_id(window_manager_t *wm, gui_form_t *form, int16_t ctrl_id) {
    (void)wm;  /* Unused but kept for API consistency */
    if (!form || !form->win.is_visible || form->win.is_minimized) return;
    if (!form->controls) return;

    /* Restore mouse cursor before drawing to avoid artifacts */
    mouse_restore();

    /* Find and redraw the specific control */
    gui_control_t *ctrl = compositor_get_control_by_id(form, ctrl_id);
    if (ctrl) {
        /* Set focus state before drawing */
        ctrl->is_focused = (ctrl->id == form->focused_control_id) ? 1 : 0;
        win_draw_control(&form->win, ctrl);
        /* If it's a dropdown with an open list, redraw that too */
        if (ctrl->type == CTRL_DROPDOWN && ctrl->dropdown_open) {
            win_draw_dropdown_list(&form->win, ctrl);
        }
    }

    mouse_invalidate_buffer();
}

void compositor_draw_dropdown_list_only(window_manager_t *wm, gui_form_t *form, int16_t ctrl_id) {
    (void)wm;  /* Unused but kept for API consistency */
    if (!form || !form->win.is_visible || form->win.is_minimized) return;
    if (!form->controls) return;

    /* Restore mouse cursor before drawing to avoid artifacts */
    mouse_restore();

    /* Find and redraw only the dropdown list */
    gui_control_t *ctrl = compositor_get_control_by_id(form, ctrl_id);
    if (ctrl && ctrl->type == CTRL_DROPDOWN && ctrl->dropdown_open) {
        win_draw_dropdown_list(&form->win, ctrl);
    }

    mouse_invalidate_buffer();
}

void compositor_invalidate_icon_backgrounds(window_manager_t *wm) {
    for (int i = 0; i < wm->count; i++) {
        gui_form_t *form = wm->windows[i];
        if (form && form->win.is_minimized && form->win.minimized_icon) {
            icon_invalidate_bg(form->win.minimized_icon);
        }
    }
}

void compositor_set_dirty_rect(window_manager_t *wm, int x, int y, int w, int h) {
    wm->dirty_x = x;
    wm->dirty_y = y;
    wm->dirty_w = w;
    wm->dirty_h = h;
}
