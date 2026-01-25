/* neofetch-like system information display */

#include "../syscall.h"
#include "../lib/stdio.h"

const char *logo[] = {
    "  #  XXXXXXXXXXXX",
    "  #           X  ",
    "  #  *******  X  ",
    "  #  *        X  ",
    "  #  *******  X  ",
    "  #  *        X  ",
    "  #  *******  X  ",
    "  #           X  ",
    "  ##########  X  "
};

void print_logo_line(const char *line) {
    for (int i = 0; line[i]; i++) {
        switch (line[i]) {
            case '#': sys_setcolor(0, 4); break;
            case '*': sys_setcolor(0, 2); break;
            case 'X': sys_setcolor(0, 1);  break;
            default:  sys_setcolor(0, 7);  break;
        }
        putchar(line[i]);
    }
    sys_setcolor(0, 7);
}

void print_label_value(const char *label, const char *value) {
    sys_setcolor(0, 9);
    printf("%s", label);
    sys_setcolor(0, 7);
    printf("%s", value);
}

void print_label_value_int(const char *label, int value) {
    sys_setcolor(0, 9);
    printf("%s", label);
    sys_setcolor(0, 7);
    printf("%d", value);
}

__attribute__((section(".entry"), used))
void _start(void) {   
    
    sys_meminfo_t meminfo;
    sys_get_meminfo(&meminfo);

    printf("\n");

    for (int i = 0; i < 9; i++) {
        print_logo_line(logo[i]);
        printf("   ");

        switch (i) {
            case 1:
                print_label_value("OS", ": osLET");
                break;
            case 2:
                sys_setcolor(0, 9); printf("Kernel"); sys_setcolor(0, 7);
                printf(": %s", sys_version());
                break;
            case 3:
                sys_setcolor(0, 9); printf("Shell"); sys_setcolor(0, 7);
                printf(": %s", sys_info_shell());
                break;
            case 4:
                print_label_value("CPU", ": i386 Compatible");
                break;
            case 5:
                sys_setcolor(0, 9); printf("Memory"); sys_setcolor(0, 7);
                printf(": %d MiB", meminfo.total_kb / 1024);
                break;
            case 6:
                print_label_value("Author", ": EinarTheSad");
                break;
            case 7:
                print_label_value("Repository", ": https://github.com/einarthesad/oslet");
                break;
        }

        printf("\n");
    }
    printf("\n");
    sys_exit();
}