#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

/* Calculator state */
static double first_num = 0.0;
static double second_num = 0.0;
static char op = 0;
static int entering_second = 0;
static int has_result = 0;

/* Control IDs */
#define ID_DISPLAY  1
#define ID_BTN_0    10
#define ID_BTN_1    11
#define ID_BTN_2    12
#define ID_BTN_3    13
#define ID_BTN_4    14
#define ID_BTN_5    15
#define ID_BTN_6    16
#define ID_BTN_7    17
#define ID_BTN_8    18
#define ID_BTN_9    19
#define ID_BTN_ADD  20
#define ID_BTN_SUB  21
#define ID_BTN_MUL  22
#define ID_BTN_DIV  23
#define ID_BTN_EQ   24
#define ID_BTN_CLR  25

/* Button layout: 4 columns x 5 rows */
#define BTN_W  40
#define BTN_H  30
#define BTN_GAP 5
#define START_X 10
#define START_Y 40

// Controls for calculator
static gui_control_t calc_controls[] = {
    {CTRL_LABEL, 10, 8, 175, 0, 10, 2, "0", 1, 1, 16, 1, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 10, 40, 40, 30, 0, 7, "7", 17, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 55, 40, 40, 30, 0, 7, "8", 18, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 100, 40, 40, 30, 0, 7, "9", 19, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 145, 40, 40, 30, 0, 7, "/", 23, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 10, 75, 40, 30, 0, 7, "4", 14, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 55, 75, 40, 30, 0, 7, "5", 15, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 100, 75, 40, 30, 0, 7, "6", 16, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 145, 75, 40, 30, 0, 7, "*", 22, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 10, 110, 40, 30, 0, 7, "1", 11, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 55, 110, 40, 30, 0, 7, "2", 12, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 100, 110, 40, 30, 0, 7, "3", 13, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 145, 110, 40, 30, 0, 7, "-", 21, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 10, 145, 40, 30, 15, 12, "C", 25, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 55, 145, 40, 30, 0, 7, "0", 10, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 100, 145, 40, 30, 15, 2, "=", 24, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1},
    {CTRL_BUTTON, 145, 145, 40, 30, 0, 7, "+", 20, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1}
};

#define NUM_CONTROLS (sizeof(calc_controls) / sizeof(calc_controls[0]))

static void *form = 0;

/* Format a double value into a string.
   If the value is integral it prints without decimal point, otherwise up to 6 decimals trimmed.
*/
static void format_number(char *buf, size_t len, double value) {
    long iv = (long)value;
    if (value == (double)iv) {
        snprintf(buf, len, "%ld", iv);
    } else {
        snprintf(buf, len, "%.6f", value);
        /* Trim trailing zeros */
        char *p = buf + strlen(buf) - 1;
        while (p > buf && *p == '0') { *p = '\0'; p--; }
        if (p > buf && *p == '.') *p = '\0';
    }
}

static void update_display(double value) {
    char buf[32];
    format_number(buf, sizeof(buf), value);
    ctrl_set_text(form, ID_DISPLAY, buf);
    sys_win_draw(form);
}

static void append_digit(int digit) {
    if (has_result) {
        /* Start fresh after result */
        first_num = (double)digit;
        second_num = 0.0;
        op = 0;
        entering_second = 0;
        has_result = 0;
        update_display(first_num);
        return;
    }

    if (entering_second) {
        second_num = second_num * 10.0 + (double)digit;
        update_display(second_num);
    } else {
        first_num = first_num * 10.0 + (double)digit;
        update_display(first_num);
    }
}

static void set_operator(char new_op) {
    if (has_result) {
        /* Use result as first number */
        has_result = 0;
    }

    /* If there is already a pending operator and we were entering the second
       number, evaluate the pending operation so chains like 2+2+3 work. */
    if (op != 0 && entering_second) {
        double res = 0.0;
        switch (op) {
            case '+': res = first_num + second_num; break;
            case '-': res = first_num - second_num; break;
            case '*': res = first_num * second_num; break;
            case '/':
                if (second_num != 0.0) {
                    res = first_num / second_num;
                } else {
                    ctrl_set_text(form, ID_DISPLAY, "Error");
                    sys_win_draw(form);
                    first_num = 0.0; second_num = 0.0; op = 0; entering_second = 0; has_result = 0;
                    return;
                }
                break;
            default: res = first_num;
        }
        first_num = res;
        second_num = 0.0;
        update_display(first_num);
    }

    op = new_op;
    entering_second = 1;
    second_num = 0.0;
}

static void calculate(void) {
    double result = first_num;

    if (op != 0) {
        switch (op) {
            case '+': result = first_num + second_num; break;
            case '-': result = first_num - second_num; break;
            case '*': result = first_num * second_num; break;
            case '/':
                if (second_num != 0.0) {
                    result = first_num / second_num;
                } else {
                    ctrl_set_text(form, ID_DISPLAY, "Error");
                    sys_win_draw(form);
                    return;
                }
                break;
        }
    }

    update_display(result);
    first_num = result;
    second_num = 0.0;
    op = 0;
    entering_second = 0;
    has_result = 1;
}

static void clear_all(void) {
    first_num = 0.0;
    second_num = 0.0;
    op = 0;
    entering_second = 0;
    has_result = 0;
    update_display(0.0);
}

static int handle_event(void *f, int event, void *userdata) {
    (void)f; (void)userdata;
    
    switch (event) {
        /* Digit buttons */
        case ID_BTN_0: append_digit(0); break;
        case ID_BTN_1: append_digit(1); break;
        case ID_BTN_2: append_digit(2); break;
        case ID_BTN_3: append_digit(3); break;
        case ID_BTN_4: append_digit(4); break;
        case ID_BTN_5: append_digit(5); break;
        case ID_BTN_6: append_digit(6); break;
        case ID_BTN_7: append_digit(7); break;
        case ID_BTN_8: append_digit(8); break;
        case ID_BTN_9: append_digit(9); break;

        /* Operator buttons */
        case ID_BTN_ADD: set_operator('+'); break;
        case ID_BTN_SUB: set_operator('-'); break;
        case ID_BTN_MUL: set_operator('*'); break;
        case ID_BTN_DIV: set_operator('/'); break;

        /* Equals */
        case ID_BTN_EQ: calculate(); break;

        /* Clear */
        case ID_BTN_CLR: clear_all(); break;
    }
    
    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    form = sys_win_create_form("Calculator", 220, 140, 194, 206);
    if (!form) {
        sys_exit();
        return;
    }
    sys_win_set_icon(form, "C:/ICONS/CALC.ICO");

    for (int i = 0; i < (int)NUM_CONTROLS; i++) {
        sys_win_add_control(form, &calc_controls[i]);
    }
    sys_win_draw(form);
    sys_win_redraw_all();

    sys_win_run_event_loop(form, handle_event, NULL);

    sys_win_destroy_form(form);
    sys_exit();
}
