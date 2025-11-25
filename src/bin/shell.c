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

#define COMMAND_COUNT (sizeof(commands) / sizeof((commands)[0]))

typedef void (*CommandFunc)(void);

typedef struct {
    const char* comm;
    const char* name;
    const char* desc;
    CommandFunc func;
} Command;

static void cmd_help(void);
static void cmd_exit(void);
static void cmd_mem(void);

static const Command commands[] = {
    { "cat",    "cat <file>",          "Display file contents",      NULL },
    { "cd",     "cd <dir>",            "Change directory",           NULL },
    { "cls",    "cls",                 "Clear screen",               sys_clear },
    { "echo",   "echo <text> <file>",  "Write text to file",         NULL },
    { "gfx",    "gfx",                 "Run a VGA graphics demo",    NULL },
    { "heap",   "heap",                "Show heap statistics",       NULL },
    { "help",   "help",                "Show this help",             cmd_help },
    { "ls",     "ls <path>",           "List directory",             NULL },
    { "mem",    "mem",                 "Show memory statistics",     cmd_mem },
    { "mkdir",  "mkdir <dir>",         "Create directory",           NULL },
    { "mount",  "mount <drive> <lba>", "Mount FAT32 drive",          NULL },
    { "ps",     "ps",                  "List running tasks",         NULL },
    { "rm",     "rm <file>",           "Delete file",                NULL },
    { "rmdir",  "rmdir <dir>",         "Remove directory",           NULL },
    { "rtc",    "rtc",                 "Show current time",          NULL },
    { "run",    "run <file>",          "Execute a binary file",      NULL },
    { "uptime", "uptime",              "Show system uptime",         NULL },
    { "exit",   "exit",                "Exit system",                cmd_exit }
};

static char current_path[FAT32_MAX_PATH];
static int parse_args(char *line, char *argv[], int max_args);

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

__attribute__((section(".entry"), used))
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

        int found = 0;

        for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            if (!strcmp(cmd, commands[i].comm)) {
                found = 1;
                if (commands[i].func) {
                    commands[i].func();
                } else {
                    /* This is for the NULL-pointed non-implemented functions */
                }
                break;
            }
        }

        if (!found) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Unknown command: ");
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            printf("'%s'\n", cmd);
        }
    }
}

static int parse_args(char *line, char *argv[], int max_args) {
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
    for (int i = 0; i < COMMAND_COUNT; i++)
        sorted[i] = commands[i];

    sort_commands(sorted, COMMAND_COUNT);

    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\nAvailable commands:\n\n");

    for (int i = 0; i < COMMAND_COUNT; i++) {
        sys_setcolor(COLOR_INFO_BG, COLOR_INFO_FG);
        printf("  %-22s", sorted[i].name);

        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("%s\n", sorted[i].desc);
    }

    printf("\n");
}

static void cmd_exit(void) {
    printf("This action will return you to the kernel shell.\nMake sure you know what you are doing! Proceed? (Y/N) ");
    char conf[2];
    sys_readline(conf,2);
    if (!strcmp(conf,"Y") || !strcmp(conf,"y")) sys_exit();
    else {};
}

static void cmd_mem(void) {
    sys_meminfo_t meminfo;
    sys_get_meminfo(&meminfo);
    printf("\nMemory statistics:\n\n");
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);
    printf("  Total installed:");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                    printf(" %.2f MiB (%u KiB)", meminfo.total_kb / 1024, meminfo.total_kb);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);
    printf("\n  Free:");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                    printf(" %.2f MiB (%u KiB)", meminfo.free_kb / 1024, meminfo.free_kb);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG); printf("\n\n");
}
