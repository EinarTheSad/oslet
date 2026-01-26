/* neofetch-like system information display */

#include "../syscall.h"
#include "../lib/stdio.h"

const char *logo[] = {
    "▀▀▀▀▀▀▀█▀",
    "█ █▀▀▀ █ ",
    "█ █▄▄▄ █ ",
    "█ █    █ ",
    "█ ▀▀▀▀ █ ",
    "▀▀▀▀▀▀ ▀ "
};

const char *colors[] = {
    "BBBBBBBBB",
    "R GGGG B ",
    "R GGGG B ",
    "R G    B ",
    "R GGGG B ",
    "RRRRRR B "
};

#define LOGO_LINES 6
#define LOGO_WIDTH 9
#define INFO_LINES 10
#define TOTAL_LINES (LOGO_LINES > INFO_LINES ? LOGO_LINES : INFO_LINES)

void print_logo_line(const char *line, const char *color) {
    int ci = 0;
    for (int i = 0; line[i]; i++) {
        switch (color[ci]) {
            case 'R': sys_setcolor(0, 4); break;
            case 'G': sys_setcolor(0, 2); break;
            case 'B': sys_setcolor(0, 1); break;
            default:  sys_setcolor(0, 7); break;
        }

        unsigned char c = (unsigned char)line[i];
        if ((c & 0xE0) == 0xE0 && line[i+1] && line[i+2]) {
            /* 3-byte UTF-8 sequence */
            char buf[4] = {line[i], line[i+1], line[i+2], 0};
            printf("%s", buf);
            i += 2;
        } else {
            putchar(line[i]);
        }
        ci++;
    }
    sys_setcolor(0, 7);
}

void print_padding(int logo_line) {
    /* Center logo vertically: offset = (total_lines - logo_lines) / 2 */
    int offset = (INFO_LINES - LOGO_LINES) / 2 - 1;
    int logo_idx = logo_line - offset;

    printf("  ");  /* Left margin */

    if (logo_idx >= 0 && logo_idx < LOGO_LINES) {
        print_logo_line(logo[logo_idx], colors[logo_idx]);
    } else {
        /* Empty space where logo would be */
        for (int i = 0; i < LOGO_WIDTH; i++) {
            putchar(' ');
        }
    }
    printf("   ");  /* Gap between logo and info */
}

void print_label(const char *label) {
    sys_setcolor(0, 9);
    printf("%s", label);
    sys_setcolor(0, 7);
}

void print_color_bar(void) {
    /* 8 basic colors */
    for (int i = 0; i < 8; i++) {
        sys_setcolor(0, i);
        printf("███");
    }
    sys_setcolor(0, 7);
    printf("\n");
    printf("   ");
    for (int i = 0; i < LOGO_WIDTH; i++) putchar(' ');
    printf("  ");
    /* 8 bright colors */
    for (int i = 8; i < 16; i++) {
        sys_setcolor(0, i);
        printf("███");
    }
    sys_setcolor(0, 7);
}

__attribute__((section(".entry"), used))
void _start(void) {
    sys_meminfo_t meminfo;
    sys_get_meminfo(&meminfo);

    uint32_t ticks = sys_uptime();
    uint32_t total_secs = ticks / 100;
    uint32_t hours = total_secs / 3600;
    uint32_t mins = (total_secs % 3600) / 60;
    uint32_t secs = total_secs % 60;

    printf("\n");

    for (int i = 0; i < INFO_LINES; i++) {
        print_padding(i);

        switch (i) {
            case 0:
                print_label("OS");
                printf(": osLET");
                break;
            case 1:
                print_label("Kernel");
                printf(": %s", sys_version());
                break;
            case 2:
                print_label("Shell");
                printf(": %s", sys_info_shell());
                break;
            case 3:
                print_label("Uptime");
                printf(": %dh %dm %ds", hours, mins, secs);
                break;
            case 4:
                print_label("Resolution");
                printf(": 80x25");
                break;
            case 5:
                print_label("CPU");
                printf(": i386 Compatible");
                break;
            case 6:
                print_label("Memory");
                printf(": %d / %d MB", meminfo.used_kb / 1024, meminfo.total_kb / 1024);
                break;
            case 7:
                print_label("Author");
                printf(": EinarTheSad");
                break;
            case 9:
                print_color_bar();
                break;
        }

        printf("\n");
    }
    printf("\n");
    sys_exit();
}