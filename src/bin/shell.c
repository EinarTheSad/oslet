#include "../syscall.h"
#include "../lib/stdio.h"
//#include "../drivers/fat32.h"

/* Color schemes
#define COLOR_PROMPT_BG    COLOR_BLACK
#define COLOR_PROMPT_FG    COLOR_LIGHT_GREEN
#define COLOR_NORMAL_BG    COLOR_BLACK
#define COLOR_NORMAL_FG    COLOR_LIGHT_GRAY
#define COLOR_ERROR_BG     COLOR_BLACK
#define COLOR_ERROR_FG     COLOR_LIGHT_RED
#define COLOR_SUCCESS_BG   COLOR_BLACK
#define COLOR_SUCCESS_FG   COLOR_LIGHT_GREEN
#define COLOR_INFO_BG      COLOR_BLACK
#define COLOR_INFO_FG      COLOR_LIGHT_CYAN
#define COLOR_DIR_FG       COLOR_YELLOW 

#define MAX_ARGS 8
#define FAT32_MAX_PATH = 256

static char current_path[256];

static void print_prompt(void) {
    sys_getcwd(current_path, sizeof(current_path));
    sys_setcolor(COLOR_PROMPT_BG, COLOR_PROMPT_FG);
    printf("%s>", current_path);
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    int in_quote = 0;
    char *p = line;

    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0')
            break;

        argv[argc++] = p;

        if (*p == '"') {
            in_quote = 1;
            argv[argc - 1] = ++p;
        }

        while (*p) {
            if (in_quote && *p == '"') {
                *p++ = '\0';
                in_quote = 0;
                break;
            } else if (!in_quote && (*p == ' ' || *p == '\t')) {
                *p++ = '\0';
                break;
            }
            p++;
        }
    }

    argv[argc] = NULL;
    return argc;
} */

static void print_banner(void) {
    sys_setcolor(1, 15);
    printf("  _____   _____  .       ______ _______ \n");
    printf(" |     | |_____  |      |______    |    \n");
    printf(" |_____|  _____| |_____ |______    |    \n");
    printf("                                        \n");
    sys_setcolor(0, 8);
    printf("Kernel 0.4              EinarTheSad 2025\n\n");
    sys_setcolor(0, 7);
}

void _start(void) {
    print_banner();
    sys_exit();
}