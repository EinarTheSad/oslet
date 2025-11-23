#include "../syscall.h"
#include "../lib/stdio.h"

void _start(void) {
    printf("Hello, World!\n");
    sys_exit();
}