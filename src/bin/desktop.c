#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/fonts.h"

#define TASKBAR_HEIGHT 27
#define TASKBAR_Y (480 - TASKBAR_HEIGHT)

typedef struct {
    int x, y, w, h;
    int pressed;
    char label[5];
} taskbar_button_t;

usr_bmf_font_t ui_font;
taskbar_button_t start_button;

void draw_simple_button(int x, int y, int w, int h, const char *label, int pressed) {
    uint8_t shad_a, shad_b;

    sys_gfx_rect(x, y, w, h, COLOR_BLACK);
    sys_gfx_fillrect(x+2, y+2, w-3, h-3, COLOR_LIGHT_GRAY);

    if (pressed) {
        shad_a = COLOR_WHITE;
        shad_b = COLOR_DARK_GRAY;
    } else {
        shad_a = COLOR_DARK_GRAY;
        shad_b = COLOR_WHITE;
    }

    sys_gfx_rect(x+1, y+1, w-2, h-2, shad_a);
    sys_gfx_line(x+1, y+1, x+w-3, y+1, shad_b);
    sys_gfx_line(x+1, y+1, x+1, y+h-3, shad_b);

    sys_gfx_load_bmp("C:/ICONS/LET.ICO", x+3, y+2);

    if (ui_font.data && label) {
        int text_x = x + 22;
        int text_y = y + 7;
        usr_bmf_printf(text_x, text_y, &ui_font, 12, COLOR_BLACK, "%s", label);
    }
}

// Controls for Form1
gui_control_t Form1_controls[] = {
    {CTRL_PICTUREBOX, 5, 5, 108, 208, 0, 7, "SETUP.BMP", 1, 0, 12, 0, 0, NULL, 0, 0},
    {CTRL_BUTTON, 295, 191, 70, 23, 0, 7, "OK", 2, 0, 12, 0, 0, NULL, 0, 0},
    {CTRL_LABEL, 118, 12, 0, 0, 0, 15, "Welcome to osLET!", 3, 1, 12, 0, 0, NULL, 0, 0},
    {CTRL_LABEL, 119, 40, 219, 114, 0, 15, "This window serves as a test of the\ncontrol system in osLET graphical\nuser interface.\n\nYou can drag this window freely using\nthe mouse, or click the button below\nto exit back to shell.", 5, 0, 12, 0, 0, NULL, 0, 0}
};

void* create_Form1(void) {
    void *form = sys_win_create_form("Welcome screen", 50, 50, 370, 240);
    sys_win_set_icon(form, "C:/ICONS/EXE.ICO");

    for (int i = 0; i < 4; i++) {
        sys_win_add_control(form, &Form1_controls[i]);
    }
    sys_win_draw(form);
    return form;
}

// Controls for Form2
gui_control_t Form2_controls[] = {
    {CTRL_FRAME, 10, 7, 181, 165, 0, 7, "Non-interactive", 1, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_LABEL, 17, 34, 0, 0, 0, 15, "Label", 2, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_LABEL, 18, 60, 0, 0, 0, 14, "Label with decoration", 3, 3, 12, 1, 0, NULL, 0, 0, 0},
    {CTRL_PICTUREBOX, 18, 89, 165, 75, 0, 7, "", 4, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_FRAME, 197, 7, 181, 165, 0, 7, "Interactive", 9, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_BUTTON, 209, 28, 70, 23, 0, 7, "Button", 10, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_BUTTON, 295, 28, 70, 23, 0, 12, "Button", 11, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_CHECKBOX, 207, 149, 13, 13, 0, 7, "CheckBox", 12, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_CHECKBOX, 292, 149, 13, 13, 0, 7, "CheckBox", 13, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_RADIOBUTTON, 208, 124, 12, 12, 0, 7, "Radio", 14, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_RADIOBUTTON, 293, 124, 12, 12, 0, 7, "Radio", 15, 0, 12, 0, 0, NULL, 0, 0, 0},
    {CTRL_TEXTBOX, 210, 67, 154, 20, 0, 7, "", 16, 0, 12, 0, 0, NULL, 0, 0, 0}
};

void* create_Form2(void) {
    void *form2 = sys_win_create_form("Control test", 126, 135, 392, 201);
    sys_win_set_icon(form2, "C:/ICONS/EXE.ICO");
    for (int i = 0; i < 12; i++) {
        sys_win_add_control(form2, &Form2_controls[i]);
    }
    sys_win_draw(form2);
    return form2;
}

void init_start_button(void) {
    start_button.x = 3;
    start_button.y = TASKBAR_Y + 3;
    start_button.w = 57;
    start_button.h = 21;
    start_button.pressed = 0;
}

void draw_taskbar(void) {
    int y = TASKBAR_Y;

    // Fill taskbar background
    sys_gfx_fillrect(0, y + 1, 640, TASKBAR_HEIGHT - 1, COLOR_LIGHT_GRAY);

    // Draw 3D border effect
    sys_gfx_line(0, y, 639, y, COLOR_WHITE);
    sys_gfx_line(0, y + 1, 0, 479, COLOR_WHITE);
    sys_gfx_line(639, y + 1, 639, 479, COLOR_DARK_GRAY);
    sys_gfx_line(0, 479, 639, 479, COLOR_DARK_GRAY);

    // Draw Start button
    draw_simple_button(start_button.x, start_button.y,
                      start_button.w, start_button.h,
                      "Start", start_button.pressed);
}

int handle_start_button_click(int mx, int my, unsigned char mb, int *state_changed) {
    static unsigned char last_mb = 0;
    int old_pressed = start_button.pressed;
    int clicked = 0;

    // Check if mouse is over button
    if (mx >= start_button.x && mx < start_button.x + start_button.w &&
        my >= start_button.y && my < start_button.y + start_button.h) {

        if (mb & 1) {  // Left button is pressed
            start_button.pressed = 1;
        } else if ((last_mb & 1) && !(mb & 1)) {
            // Button was pressed and now released (click!)
            start_button.pressed = 0;
            clicked = 1;
        }
    } else {
        // Mouse moved away
        if (!(mb & 1)) {
            start_button.pressed = 0;
        }
    }

    // Check if button state changed
    if (old_pressed != start_button.pressed) {
        *state_changed = 1;
    }

    last_mb = mb;
    return clicked;
}

__attribute__((section(".entry"), used))
void _start(void) {
    int mx = 320;
    int my = 240;
    unsigned char mb;

    sys_gfx_enter();
    sys_gfx_fillrect(0, 0, 640, 480, COLOR_CYAN);

    usr_bmf_import(&ui_font, "C:/FONTS/LSANS_B.BMF");

    init_start_button();
    draw_taskbar();

    void *Form1 = create_Form1();
    void *Form2 = create_Form2();

    int exit_requested = 0;
    int taskbar_needs_redraw = 0;

    while (!exit_requested) {
        sys_get_mouse_state(&mx, &my, &mb);

        // Handle Start button clicks
        int button_state_changed = 0;
        if (handle_start_button_click(mx, my, mb, &button_state_changed)) {
            // Start button was clicked!
            taskbar_needs_redraw = 1;
        }

        // Redraw taskbar if button state changed
        if (button_state_changed) {
            taskbar_needs_redraw = 1;
        }

        if (taskbar_needs_redraw) {
            draw_taskbar();
            taskbar_needs_redraw = 0;
        }

        // Handle Form1 events
        int event0 = sys_win_pump_events(Form1);
        if (event0 > 0) {
            /* Control clicked in Form1 - event = control ID */
            if (event0 == 2) { /* OK */
                exit_requested = 1;
            }
        } else if (event0 == -2 || event0 == -1) {
            sys_win_redraw_all();
        }
        // Handle Form2 events
        int event1 = sys_win_pump_events(Form2);
        if (event1 > 0) {
            /* Control clicked in Form2 - event = control ID */
            if (event1 == 2) {
                continue;
            }
        } else if (event1 == -2 || event1 == -1) {
            sys_win_redraw_all();
        }

        sys_mouse_draw_cursor(mx, my, 0);
        sys_gfx_swap();
    }

    sys_win_destroy_form(Form1);
    sys_win_destroy_form(Form2);
    sys_gfx_exit();
    sys_exit();
}
