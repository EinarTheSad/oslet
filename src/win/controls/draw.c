#include "internal.h"

void ctrl_draw_with_offset(window_t *win, gui_control_t *control, int y_offset) {
    /* Check visibility - high bit (0x80) indicates hidden */
    if (control->type & 0x80) return;
    
    int abs_x = win->x + control->x;
    int abs_y = win->y + control->y + y_offset;
    
    /* Mask out visibility bit to get actual type */
    uint8_t ctrl_type = control->type & 0x7F;

    if (ctrl_type == 1) { /* CTRL_BUTTON */
        ctrl_draw_button(control, abs_x, abs_y);
    }
    else if (ctrl_type == 2) { /* CTRL_LABEL */
        ctrl_draw_label(control, abs_x, abs_y);
    }
    else if (ctrl_type == 3) { /* CTRL_PICTUREBOX */
        ctrl_draw_picturebox(control, abs_x, abs_y);
    }
    else if (ctrl_type == 4) { /* CTRL_CHECKBOX */
        ctrl_draw_checkbox(control, abs_x, abs_y);
    }
    else if (ctrl_type == 5) { /* CTRL_RADIOBUTTON */
        ctrl_draw_radiobutton(control, abs_x, abs_y);
    }
    else if (ctrl_type == 6) { /* CTRL_TEXTBOX */
        ctrl_draw_textbox(control, abs_x, abs_y);
    }
    else if (ctrl_type == 7) { /* CTRL_FRAME */
        ctrl_draw_frame(control, abs_x, abs_y);
    }
    else if (ctrl_type == 8) { /* CTRL_ICON */
        window_theme_t *theme = theme_get_current();
        ctrl_draw_icon(control, abs_x, abs_y, theme->bg_color);
    }
    else if (ctrl_type == 9) { /* CTRL_DROPDOWN */
        ctrl_draw_dropdown(control, abs_x, abs_y);
    }
    else if (ctrl_type == 10) { /* CTRL_CLOCK */
        ctrl_draw_clock(control, abs_x, abs_y);
    }
    else if (ctrl_type == 11) { /* CTRL_SCROLLBAR */
        ctrl_draw_scrollbar(control, abs_x, abs_y);
    }
    else if (ctrl_type == 12) { /* CTRL_TREEVIEW */
        ctrl_draw_treeview(control, abs_x, abs_y);
    }
    else if (ctrl_type == 13) { /* CTRL_LISTBOX */
        ctrl_draw_listbox(control, abs_x, abs_y);
    }
}

/* Need to keep it that way for backwards compatibility */
void ctrl_draw(window_t *win, gui_control_t *control) {
    ctrl_draw_with_offset(win, control, 20);
}
