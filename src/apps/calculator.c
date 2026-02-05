#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

/* Calculator state */
static double first_num = 0.0;
static double second_num = 0.0;
static char op = 0;
static int entering_second = 0;
static int has_result = 0;

/* Decimal input state */
static int first_has_dot = 0;
static int second_has_dot = 0;
static double first_dot_mul = 1.0; /* multiplier for fractional digits: digit / first_dot_mul */
static double second_dot_mul = 1.0;

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
#define ID_BTN_DOT  26
#define ID_BTN_SQRT 27

// Controls for calculator
static gui_control_t calc_controls[] = {
    {CTRL_LABEL, 10, 7, 135, 30, 10, 2, "0", 1, 1, 24, 1, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 10, 70, 30, 30, 0, -1, "7", 17, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 45, 70, 30, 30, 0, -1, "8", 18, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 80, 70, 30, 30, 0, -1, "9", 19, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 80, 45, 30, 20, 0, -1, "/", 23, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 10, 105, 30, 30, 0, -1, "4", 14, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 45, 105, 30, 30, 0, -1, "5", 15, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 80, 105, 30, 30, 0, -1, "6", 16, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 115, 45, 30, 20, 0, -1, "*", 22, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 10, 140, 30, 30, 0, -1, "1", 11, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 45, 140, 30, 30, 0, -1, "2", 12, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 80, 140, 30, 30, 0, -1, "3", 13, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 115, 105, 30, 30, 0, -1, "-", 21, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 10, 45, 30, 20, 15, 12, "C", 25, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 10, 175, 65, 30, 0, -1, "0", 10, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 115, 140, 30, 65, 15, -1, "=", 24, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 115, 70, 30, 30, 0, -1, "+", 20, 0, 14, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 80, 175, 30, 30, 0, -1, ".", 26, 0, 12, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0},
    {CTRL_BUTTON, 45, 45, 30, 20, 0, -1, "sq", 27, 0, 12, 0, 0, NULL, 0, 0, 0, 0, 0, 0, 0, -1, -1, 0, 0}
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

static void update_display(double value, int force_dot) {
    char buf[32];
    format_number(buf, sizeof(buf), value);
    /* If caller requests, ensure there's a trailing decimal point shown */
    if (force_dot) {
        /* Only append '.' if not already present */
        if (!strchr(buf, '.')) {
            size_t l = strlen(buf);
            if (l < sizeof(buf) - 1) {
                buf[l] = '.';
                buf[l + 1] = '\0';
            }
        }
    }
    ctrl_set_text(form, ID_DISPLAY, buf);
    sys_win_draw(form);
}

static double calc_sqrt(double x) {
    if (x == 0.0) return 0.0;
    double r = x;
    for (int i = 0; i < 50; i++) {
        r = 0.5 * (r + x / r);
    }
    return r;
}

static void append_digit(int digit) {
    if (has_result) {
        /* Start fresh after result */
        first_num = (double)digit;
        second_num = 0.0;
        op = 0;
        entering_second = 0;
        has_result = 0;
        /* Reset decimal input state */
        first_has_dot = 0; first_dot_mul = 1.0;
        second_has_dot = 0; second_dot_mul = 1.0;
        update_display(first_num, 0);
        return;
    }

    if (entering_second) {
        if (second_has_dot) {
            second_dot_mul *= 10.0;
            second_num += (double)digit / second_dot_mul;
        } else {
            second_num = second_num * 10.0 + (double)digit;
        }
        update_display(second_num, second_has_dot);
    } else {
        if (first_has_dot) {
            first_dot_mul *= 10.0;
            first_num += (double)digit / first_dot_mul;
        } else {
            first_num = first_num * 10.0 + (double)digit;
        }
        update_display(first_num, first_has_dot);
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
        /* update decimal flag based on whether result is fractional */
        first_has_dot = (first_num != (long)first_num) ? 1 : 0;
        first_dot_mul = 1.0;
        update_display(first_num, first_has_dot);
    }

    op = new_op;
    entering_second = 1;
    second_num = 0.0; /* start fresh for second number */
    second_has_dot = 0; second_dot_mul = 1.0;
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

    /* Update decimal flag based on whether result is fractional */
    first_has_dot = (result != (long)result) ? 1 : 0;
    first_dot_mul = 1.0;
    second_has_dot = 0; second_dot_mul = 1.0;
    update_display(result, first_has_dot);
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
    first_has_dot = 0; second_has_dot = 0;
    first_dot_mul = 1.0; second_dot_mul = 1.0;
    update_display(0.0, 0);
}

static void press_dot(void) {
    if (has_result) {
        /* Start a new number "0." after a result */
        first_num = 0.0;
        second_num = 0.0;
        op = 0;
        entering_second = 0;
        has_result = 0;
        first_has_dot = 1; first_dot_mul = 1.0;
        second_has_dot = 0; second_dot_mul = 1.0;
        update_display(first_num, first_has_dot);
        return;
    }

    if (entering_second) {
        if (!second_has_dot) { second_has_dot = 1; second_dot_mul = 1.0; }
        /* show current second number with dot (even if zero) */
        update_display(second_num, second_has_dot);
    } else {
        if (!first_has_dot) { first_has_dot = 1; first_dot_mul = 1.0; }
        update_display(first_num, first_has_dot);
    }
}

static int sqrt(void) {
    double val = entering_second ? second_num : first_num;
    if (val < 0.0) {
        ctrl_set_text(form, ID_DISPLAY, "Error");
        sys_win_draw(form);
        first_num = 0.0; second_num = 0.0; op = 0; entering_second = 0; has_result = 0;
        return 0;
    }
    double res = calc_sqrt(val);
    if (entering_second) {
        second_num = res;
        second_has_dot = (res != (long)res) ? 1 : 0;
        second_dot_mul = 1.0;
        update_display(second_num, second_has_dot);
    } else {
        first_num = res;
        first_has_dot = (res != (long)res) ? 1 : 0;
        first_dot_mul = 1.0;
        update_display(first_num, first_has_dot);
        /* Treat sqrt as a result when applied to the first/only operand */
        has_result = 1;
        op = 0;
        entering_second = 0;
    }
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

        /* Decimal point */
        case ID_BTN_DOT: press_dot(); break;

        /* Square root */
        case ID_BTN_SQRT: sqrt(); break;
    }
    
    return 0;
}

__attribute__((section(".entry"), used))
void _start(void) {
    form = sys_win_create_form("Calculator", 330, 122, 159, 234);
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
