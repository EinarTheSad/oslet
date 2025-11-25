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

typedef struct {
    const char* name;
    const char* desc;
} Command;

static const Command commands[] = {
    { "cat <file>",          "Display file contents" },
    { "cd <dir>",            "Change directory" },
    { "cls",                 "Clear screen" },
    { "echo <text> <file>",  "Write text to file" },
    { "gfx",                 "Run a VGA graphics demo" },
    { "heap",                "Show heap statistics" },
    { "help",                "Show this help" },
    { "ls <path>",           "List directory" },
    { "mem",                 "Show memory statistics" },
    { "mkdir <dir>",         "Create directory" },
    { "mount <drive> <lba>", "Mount FAT32 drive" },
    { "ps",                  "List running tasks" },
    { "rm <file>",           "Delete file" },
    { "rmdir <dir>",         "Remove directory" },
    { "rtc",                 "Show current time" },
    { "run <file>",          "Execute a binary file" },
    { "uptime",              "Show system uptime" }
};

static char current_path[FAT32_MAX_PATH];
int parse_args(char *line, char *argv[], int max_args);
int command_count = sizeof(commands) / sizeof(commands[0]);

static void cmd_help(void);

static void sort_commands(Command* arr, int count) {
    for (int i = 0; i < count - 1; i++) {
        for (int j = 0; j < count - i - 1; j++) {
            if (strcmp(arr[j].name, arr[j+1].name) > 0) {
                Command tmp = arr[j];
                arr[j] = arr[j+1];
                arr[j+1] = tmp;
            }
        }
    }
}

static void print_prompt(void) {
    sys_getcwd(current_path, sizeof(current_path));
    sys_setcolor(COLOR_PROMPT_BG, COLOR_PROMPT_FG);
    printf("%s>", current_path);
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

void _start(void) {
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("Developmental osLET standalone shell %s\n", sys_version());
    printf("Warning: this is highly unstable and under current development.\nPlease do not use it on your important data!\n");

    char line[128];
    char *argv[MAX_ARGS];
    memset(argv, 0, sizeof(argv));
    int argc;

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
        
        if (line[0] == '\0') continue;

        argc = parse_args(line, argv, MAX_ARGS);

        if (argc == 0)
            continue;

        char *cmd = argv[0];

        if (!strcmp(cmd, "exit")) {
            sys_exit();
        } else if (!strcmp(cmd, "cls")) {
            sys_clear();
            continue;
        } else if (!strcmp(cmd, "help")) {
            cmd_help();
            continue;
        } else {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Unknown command: ");
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            printf("'%s'\n", cmd);
        }
    }
}

int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    
    if (!line || max_args <= 0)
        return 0;

    char *p = line;

    while (*p && argc < max_args - 1) {

        if (!*p)
            break;

        argv[argc++] = p;

        while (*p && *p != ' ' && *p != '\t')
            p++;

        if (*p)
            *p++ = '\0';
    }

    argv[argc] = NULL;
    return argc;
}

static void cmd_help(void) {
    Command sorted[sizeof(commands) / sizeof(commands[0])];
    for (int i = 0; i < command_count; i++)
        sorted[i] = commands[i];

    sort_commands(sorted, command_count);

    sys_setcolor(COLOR_PROMPT_BG, COLOR_DIR_FG);
    printf("\nAvailable commands:\n\n");

    for (int i = 0; i < command_count; i++) {
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("  %-22s", sorted[i].name);

        sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
        printf("%s\n", sorted[i].desc);
    }

    printf("\n");
}
