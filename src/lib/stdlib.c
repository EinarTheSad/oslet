#include "stdlib.h"
#include "../syscall.h"

void* malloc(size_t size) {
    return sys_malloc(size);
}

void free(void* ptr) {
    sys_free(ptr);
}

int atoi(const char *s) {
    int result = 0;
    int sign = 1;

    while (*s == ' ') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;

    while (*s >= '0' && *s <= '9') {
        result = result * 10 + (*s - '0');
        s++;
    }
    return result * sign;
}