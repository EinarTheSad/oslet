#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"
//#include "../drivers/fat32.h"

/* Color schemes */
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
#define FAT32_MAX_PATH 256

static char current_path[FAT32_MAX_PATH];

static void print_prompt(void) {
    sys_getcwd(current_path, sizeof(current_path));
    sys_setcolor(COLOR_PROMPT_BG, COLOR_PROMPT_FG);
    printf("%s>", current_path);
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

/* int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    int in_quote = 0;
    char *p = line;

    if (max_args <= 0)
        return 0;

    while (*p) {
        
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0')
            break;

        
        if (argc >= max_args - 1)
            break;

        argv[argc++] = p;

        
        if (*p == '"') {
            in_quote = 1;
            argv[argc - 1] = ++p;  
        }

        
        while (*p) {
            if (in_quote) {
                if (*p == '"') {
                    *p++ = '\0';
                    in_quote = 0;
                    break;
                }
            } else {
                if (*p == ' ' || *p == '\t') {
                    *p++ = '\0';
                    break;
                }
            }
            p++;
        }
    } 

    argv[argc] = NULL;
    return argc;
} */

void _start(void) {
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("Developmental osLET standalone shell %s\n", sys_version());
    printf("Warning: this is highly unstable and under current development.\nPlease do not use it on your important data!\n");
    char line[128];
    
    for (;;) {
        print_prompt();
        int n = sys_readline(line, sizeof(line));
        if (n <= 0) {
            __asm__ volatile ("hlt");
            continue;
        }
        
        /* Trim newlines */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        
        if (line[0] == '\0')
            continue;
        
        /* char *argv[MAX_ARGS] = {0};
        int argc = parse_args(line, argv, MAX_ARGS);
        if (argc == 0)
            continue;

        char *cmd = argv[0]; */

        if (!strcmp(line, "exit")) {
            sys_exit();
        }
        
        if (!strcmp(line, "cls")) {
            sys_clear();
            continue;
        }

        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Unknown command: ");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("'%s'\n", line);
    }
}