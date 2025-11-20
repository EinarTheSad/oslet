/* neofetch-like system information display */

#include "../syscall.h"
#include "../lib/stdio.h"

#define YEAR 2025

static int mem = 32;

void _start(void) {
    sys_setcolor(0, 9);
    printf("\n  default user\n");
    
    sys_setcolor(0, 7);
    printf("  ------------\n");
    
    sys_setcolor(0, 9);
    printf("  OS");
    sys_setcolor(0, 7);
    printf(": Codename osLET\n");
    
    sys_setcolor(0, 9);
    printf("  Kernel");
    sys_setcolor(0, 7);
    printf(": Development Kernel 0.3.3\n");
    
    sys_setcolor(0, 9);
    printf("  Shell");
    sys_setcolor(0, 7);
    printf(": oslet-v03\n");
    
    sys_setcolor(0, 9);
    printf("  CPU");
    sys_setcolor(0, 7);
    printf(": i386 Compatible\n");
    
    sys_setcolor(0, 9);
    printf("  Memory");
    sys_setcolor(0, 7);
    printf(": %d MiB\n", mem);
    
    sys_setcolor(0, 9);
    printf("  Author");
    sys_setcolor(0, 7);
    printf(": EinarTheSad\n");
    
    sys_setcolor(0, 9);
    printf("  Year");
    sys_setcolor(0, 7);
    printf(": %d\n", YEAR);
    
    sys_setcolor(0, 9);
    printf("  Repository");
    sys_setcolor(0, 7);
    printf(": https://github.com/einarthesad/oslet\n\n");
    
    sys_exit();
}