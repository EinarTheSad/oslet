/* neofetch-like system information display
 * good for testing early binary loading */

#include "../syscall.h"

void _start(void) {
    sys_setcolor(0,9);
    sys_write("\n  default user\n");
    sys_setcolor(0,7);
    sys_write("  ------------\n");
    sys_setcolor(0,9);
    sys_write("  OS");
    sys_setcolor(0,7);
    sys_write(": Codename osLET\n");
    sys_setcolor(0,9);
    sys_write("  Kernel");
    sys_setcolor(0,7);
    sys_write(": Development Kernel 0.3\n");                                                
    sys_setcolor(0,9);
    sys_write("  Shell");
    sys_setcolor(0,7);
    sys_write(": oslet-v03\n");
    sys_setcolor(0,9);
    sys_write("  CPU");
    sys_setcolor(0,7);
    sys_write(": i386 Compatible\n"); /* placeholder */
    sys_setcolor(0,9);
    sys_write("  Memory");
    sys_setcolor(0,7);
    sys_write(": 32 MiB\n"); /* placeholder */
    sys_setcolor(0,9);
    sys_write("  Author");
    sys_setcolor(0,7);
    sys_write(": EinarTheSad\n");
    sys_setcolor(0,9);
    sys_write("  Year");
    sys_setcolor(0,7);
    sys_write(": 2025\n");
    sys_setcolor(0,9);
    sys_write("  Repository");
    sys_setcolor(0,7);
    sys_write(": https://github.com/einarthesad/oslet\n\n");
    sys_exit();
}