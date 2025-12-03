#include "../syscall.h"
#include "../lib/stdio.h"

__attribute__((section(".entry"), used))
void _start(void) {
    
    int x = 0, y = 0;
    unsigned char buttons = 0;
    int last_x = -1, last_y = -1;
    unsigned char last_buttons = 0xFF;
    
    while (1) {
        sys_get_mouse_state(&x, &y, &buttons);
        
        if (x != last_x || y != last_y || buttons != last_buttons) {
            sys_setcur(0, 0);
            
            char buf[64];
            snprintf(buf, sizeof(buf), "Mouse X: %d    ", x);
            sys_write(buf);
            
            snprintf(buf, sizeof(buf), "\nMouse Y: %d    ", y);
            sys_write(buf);
            
            snprintf(buf, sizeof(buf), "\nButtons: L:%d M:%d R:%d    ", 
                    buttons & 1, (buttons >> 2) & 1, (buttons >> 1) & 1);
            sys_write(buf);
            
            last_x = x;
            last_y = y;
            last_buttons = buttons;
        }
        
        for (volatile int i = 0; i < 50000; i++);
    }
}