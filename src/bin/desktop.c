#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"

// Control definitions for main window
gui_control_t controls[] = {
    {CTRL_PICTUREBOX, 5, 5, 108, 208, 0, 7, "SETUP.BMP", 1, 0, 12, 0, 0, NULL, 0},
    {CTRL_BUTTON, 290, 189, 75, 25, 0, 7, "OK", 2, 0, 12, 0, 0, NULL, 0},
    {CTRL_LABEL, 118, 12, 0, 0, 0, 15, "Welcome to osLET!", 3, 1, 12, 0, 0, NULL, 0},
    {CTRL_LABEL, 118, 40, 190, 0, 0, 15, "This window serves as a test of the\ncontrol system in osLET graphical\nuser interface.\n\nYou can drag this window freely\nusing a mouse, or click the button\nbelow to exit back to shell.\n", 5, 0, 12, 0, 0, NULL, 0}
};

// Control definitions for second test window
gui_control_t test_controls[] = {
    {CTRL_LABEL, 10, 10, 230, 0, 0, 15, "Test Window", 1, 1, 14, 0, 0, NULL, 0},
    {CTRL_LABEL, 10, 35, 230, 0, 0, 15, "This demonstrates:\n- Window Manager\n- Z-ordering\n- Multiple windows\n- Click to bring to front", 2, 0, 12, 1, 8, NULL, 0},
    {CTRL_BUTTON, 85, 125, 80, 25, 0, 7, "Close", 3, 0, 12, 0, 0, NULL, 0}
};

void draw_taskbar(void) {
    sys_gfx_fillrect(1, 480-25, 638, 24, COLOR_LIGHT_GRAY);
    sys_gfx_line(0, 480-26, 639, 480-26, COLOR_WHITE);
    sys_gfx_line(0, 480-25, 0, 478, COLOR_WHITE);
    sys_gfx_line(639, 480-25, 639, 479, COLOR_DARK_GRAY);
    sys_gfx_line(0, 479, 639, 479, COLOR_DARK_GRAY);
}

__attribute__((section(".entry"), used))
void _start(void) {

    sys_gfx_enter();

    int mx = 320;
    int my = 240;
    unsigned char mb;

    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
    draw_taskbar();

    // Create main window (original content)
    void *form = sys_win_create_form("Welcome screen", 50, 50, 370, 240);
    sys_win_set_icon(form, "C:/ICONS/EXE.ICO");
    for (int i = 0; i < 4; i++) {
        sys_win_add_control(form, &controls[i]);
    }
    sys_win_draw(form);

    // Create second test window
    void *test_form = sys_win_create_form("Test Window", 350, 200, 250, 180);
    sys_win_set_icon(test_form, "C:/ICONS/EXE.ICO");
    for (int i = 0; i < 3; i++) {
        sys_win_add_control(test_form, &test_controls[i]);
    }
    sys_win_draw(test_form);

    int exit_requested = 0;
    int test_closed = 0;

    while (!exit_requested) {
        sys_get_mouse_state(&mx, &my, &mb);

        // Process main window events
        int event = sys_win_pump_events(form);
        if (event > 0) {
            /* Control was clicked - event contains the control ID */
            if (event == 2) {
                /* OK button clicked */
                exit_requested = 1;
            }
        } else if (event == -2) {
            /* Drag ended - full redraw needed */
            sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
            draw_taskbar();
            sys_win_redraw_all();
        } else if (event == -1) {
            /* Window needs redraw (button state changed, brought to front, minimized) */
            sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
            draw_taskbar();
            sys_win_redraw_all();
        }

        // Process test window events (if not closed)
        if (!test_closed) {
            int test_event = sys_win_pump_events(test_form);
            if (test_event > 0) {
                if (test_event == 3) {
                    /* Close button clicked */
                    sys_win_destroy_form(test_form);
                    test_closed = 1;
                    /* Redraw everything after closing window */
                    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
                    draw_taskbar();
                    sys_win_redraw_all();
                    /* Update mouse position and redraw cursor */
                    sys_get_mouse_state(&mx, &my, &mb);
                    sys_mouse_draw_cursor(mx, my, 0);
                    sys_gfx_swap();
                }
            } else if (test_event == -2) {
                /* Drag ended - full redraw needed */
                sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
                draw_taskbar();
                sys_win_redraw_all();
            } else if (test_event == -1) {
                /* Test window needs redraw (button state, brought to front) */
                sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);
                draw_taskbar();
                sys_win_redraw_all();
            }
        }

        sys_mouse_draw_cursor(mx, my, 0);
        sys_gfx_swap();
    }

    if (!test_closed) {
        sys_win_destroy_form(test_form);
    }
    sys_win_destroy_form(form);
    sys_gfx_exit();
    sys_exit();
}
