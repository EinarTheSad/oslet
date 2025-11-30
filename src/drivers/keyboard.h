#pragma once
#include <stddef.h>

/* 
 * Special key codes - using values 0x80-0xFF range
 * These are returned as unsigned chars but interpreted as special keys
 * Values chosen to not conflict with printable ASCII (0x20-0x7E)
 */

/* Arrow keys: 0x80-0x83 */
#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83

/* Navigation: 0x84-0x89 */
#define KEY_HOME    0x84
#define KEY_END     0x85
#define KEY_PGUP    0x86
#define KEY_PGDN    0x87
#define KEY_INSERT  0x88
#define KEY_DELETE  0x89

/* Function keys: 0x8A-0x95 (F1-F12) */
#define KEY_F1      0x8A
#define KEY_F2      0x8B
#define KEY_F3      0x8C
#define KEY_F4      0x8D
#define KEY_F5      0x8E
#define KEY_F6      0x8F
#define KEY_F7      0x90
#define KEY_F8      0x91
#define KEY_F9      0x92
#define KEY_F10     0x93
#define KEY_F11     0x94
#define KEY_F12     0x95

/* Special: 0x96 */
#define KEY_ESC     0x1B  /* Keep ESC as standard ASCII escape */

/* Alt + letter combinations: 0xA0-0xB9 (Alt+A through Alt+Z) */
#define KEY_ALT_A   0xA0
#define KEY_ALT_B   0xA1
#define KEY_ALT_C   0xA2
#define KEY_ALT_D   0xA3
#define KEY_ALT_E   0xA4
#define KEY_ALT_F   0xA5
#define KEY_ALT_G   0xA6
#define KEY_ALT_H   0xA7
#define KEY_ALT_I   0xA8
#define KEY_ALT_J   0xA9
#define KEY_ALT_K   0xAA
#define KEY_ALT_L   0xAB
#define KEY_ALT_M   0xAC
#define KEY_ALT_N   0xAD
#define KEY_ALT_O   0xAE
#define KEY_ALT_P   0xAF
#define KEY_ALT_Q   0xB0
#define KEY_ALT_R   0xB1
#define KEY_ALT_S   0xB2
#define KEY_ALT_T   0xB3
#define KEY_ALT_U   0xB4
#define KEY_ALT_V   0xB5
#define KEY_ALT_W   0xB6
#define KEY_ALT_X   0xB7
#define KEY_ALT_Y   0xB8
#define KEY_ALT_Z   0xB9

void keyboard_init(void);

int kbd_getchar(void);  /* Returns int to support extended keys */
size_t kbd_getline(char* buf, size_t maxlen);