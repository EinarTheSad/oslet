#pragma once
#include <stddef.h>

void keyboard_init(void);

char kbd_getchar(void);
size_t kbd_getline(char* buf, size_t maxlen);