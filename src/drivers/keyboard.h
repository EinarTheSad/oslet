#pragma once
#include <stddef.h>

#define KEY_UP    0x80
#define KEY_DOWN  0x81
#define KEY_LEFT  0x82
#define KEY_RIGHT 0x83

void keyboard_init(void);

char kbd_getchar(void);
size_t kbd_getline(char* buf, size_t maxlen);