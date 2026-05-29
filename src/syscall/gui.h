#pragma once

#include "internal.h"

#define TEXTBOX_MULTILINE_SIZE 8125
#define DROPDOWN_HOVER_SCROLLBAR (-2)

extern window_manager_t global_wm;
extern int wm_initialized;

void pump_deselect_all_icons(window_manager_t *wm);
int pump_handle_icon_click(gui_form_t *form, int mx, int my);
int pump_handle_minimize(gui_form_t *form, int mx, int my);
int pump_handle_titlebar_click(gui_form_t *form, int mx, int my);
int pump_handle_resize_corner_click(gui_form_t *form, int mx, int my);
int pump_update_dropdown_hover(gui_form_t *form, int mx, int my, int ctrl_y_offset);
int pump_handle_control_press(gui_form_t *form, int mx, int my, int ctrl_y_offset);
int pump_handle_keyboard(gui_form_t *form);
uint32_t sys_win_pump_events_kernel(gui_form_t *form);

gui_control_t *find_control_by_id(gui_form_t *form, int16_t id);
char *textbox_get_text(gui_control_t *ctrl);
int textbox_run_edit_command(gui_control_t *ctrl, int command);

void treeview_layout_sys(gui_control_t *ctrl, int *out_content_w, int *out_row_area_h,
                         int *out_visible_rows, int *out_show_vscroll,
                         int *out_show_hscroll, int *out_max_vscroll,
                         int *out_max_hscroll);
int treeview_max_scroll_sys(gui_control_t *ctrl);
void treeview_keep_selected_visible_sys(gui_control_t *ctrl);
int listbox_max_scroll_sys(gui_control_t *ctrl);
