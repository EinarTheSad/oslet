#pragma once
#include <stddef.h>

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83
#define KEY_DELETE 0x84
#define KEY_F1     0x85
#define KEY_F2     0x86
#define KEY_F3     0x87
#define KEY_F4     0x88
#define KEY_F5     0x89
#define KEY_F6     0x8A
#define KEY_F7     0x8B
#define KEY_F8     0x8C
#define KEY_F9     0x8D
#define KEY_F10    0x8E

void keyboard_init(void);

char kbd_getchar(void);
size_t kbd_getline(char* buf, size_t maxlen);