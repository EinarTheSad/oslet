#pragma once
#include "wm.h"

void compositor_draw_all(window_manager_t *wm);
void compositor_draw_single(window_manager_t *wm, gui_form_t *form);
void compositor_draw_control_by_id(window_manager_t *wm, gui_form_t *form, int16_t ctrl_id);
void compositor_draw_dropdown_list_only(window_manager_t *wm, gui_form_t *form, int16_t ctrl_id);
void compositor_invalidate_icon_backgrounds(window_manager_t *wm);
void compositor_set_dirty_rect(window_manager_t *wm, int x, int y, int w, int h);
