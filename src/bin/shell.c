#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

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

const char *shell_version = "oslet-v04";

static int sys_stat(const char *path, sys_dirent_t *entry) {
    sys_dirent_t entries[64];
    char *last_slash = strrchr(path, '/');
    char dir_path[FAT32_MAX_PATH];
    const char *filename;
    
    if (last_slash) {
        size_t dir_len = last_slash - path + 1;
        if (dir_len >= sizeof(dir_path)) return -1;
        memcpy(dir_path, path, dir_len);
        dir_path[dir_len] = '\0';
        filename = last_slash + 1;
    } else {
        sys_getcwd(dir_path, sizeof(dir_path));
        filename = path;
    }
    
    int count = sys_readdir(dir_path, entries, 64);
    if (count < 0) return -1;
    
    for (int i = 0; i < count; i++) {
        if (!strcasecmp(entries[i].name, filename)) {
            if (entry) *entry = entries[i];
            return 0;
        }
    }
    
    return -1;
}

static void str_toupper(char *str) {
    while (*str) {
        if (*str >= 'a' && *str <= 'z')
            *str = *str - 32;
        str++;
    }
}

static int compare_entries(const void *a, const void *b) {
    const sys_dirent_t *ea = (const sys_dirent_t*)a;
    const sys_dirent_t *eb = (const sys_dirent_t*)b;
    
    if (ea->is_directory && !eb->is_directory) return -1;
    if (!ea->is_directory && eb->is_directory) return 1;
    
    return strcmp(ea->name, eb->name);
}

static int match_wildcard(const char *pattern, const char *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1;
            
            if (*pattern == '.') {
                const char *dot = strchr(str, '.');
                if (!dot) return 0;
                str = dot;
                continue;
            }
            
            while (*str) {
                if (match_wildcard(pattern, str))
                    return 1;
                str++;
            }
            return 0;
        }
        else if (*pattern == '?' || *pattern == *str) {
            pattern++;
            str++;
        }
        else {
            return 0;
        }
    }
    
    while (*pattern == '*') pattern++;
    
    return (*pattern == '\0' && *str == '\0');
}

typedef void (*CommandFunc)(int argc, char *argv[]);

typedef struct {
    const char* comm;
    const char* name;
    const char* desc;
    CommandFunc func;
} Command;

static void cmd_help(int argc, char *argv[]);
static void cmd_exit(int argc, char *argv[]);
static void cmd_mem(int argc, char *argv[]);
static void cmd_rtc(int argc, char *argv[]);
static void cmd_heap(int argc, char *argv[]);
static void cmd_cat(int argc, char *argv[]);
static void cmd_cd(int argc, char *argv[]);
static void cmd_echo(int argc, char *argv[]);
static void cmd_ls(int argc, char *argv[]);
static void cmd_mkdir(int argc, char *argv[]);
static void cmd_rm(int argc, char *argv[]);
static void cmd_rmdir(int argc, char *argv[]);
static void cmd_ps(int argc, char *argv[]);
static void cmd_uptime(int argc, char *argv[]);
static void cmd_cls(int argc, char *argv[]);

static const Command commands[] = {
    { "cat",    "cat <file>",          "Display file contents",      cmd_cat },
    { "cd",     "cd <dir>",            "Change directory",           cmd_cd },
    { "cls",    "cls",                 "Clear screen",               cmd_cls },
    { "echo",   "echo <text> <file>",  "Write text to file",         cmd_echo },
    { "heap",   "heap",                "Show heap statistics",       cmd_heap },
    { "help",   "help",                "Show this command list",     cmd_help },
    { "ls",     "ls <path>",           "List directory",             cmd_ls },
    { "mem",    "mem",                 "Show memory statistics",     cmd_mem },
    { "mkdir",  "mkdir <dir>",         "Create directory",           cmd_mkdir },
    { "ps",     "ps",                  "List running tasks",         cmd_ps },
    { "rm",     "rm <file>",           "Delete file",                cmd_rm },
    { "rmdir",  "rmdir <dir>",         "Remove directory",           cmd_rmdir },
    { "time",   "time",                "Show current time and date", cmd_rtc },
    { "uptime", "uptime",              "Show system uptime",         cmd_uptime },
    { "exit",   "exit",                "Exit shell",                 cmd_exit }
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
    memset(current_path, 0, sizeof(current_path));
    char *result = sys_getcwd(current_path, sizeof(current_path));
    
    if (!result || current_path[0] == '\0') {
        strcpy(current_path, "C:/");
    }
    
    str_toupper(current_path);
    sys_setcolor(COLOR_PROMPT_BG, COLOR_PROMPT_FG);
    printf("%s>", current_path);
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

__attribute__((section(".entry"), used))
void _start(void) {
    sys_shell_set(shell_version);
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    char line[128];
    char *argv[MAX_ARGS];
    memset(argv, 0, sizeof(argv));
    int argc;

    for (;;) {
        print_prompt();
        int n = sys_readline_edit(line, sizeof(line));
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
                    commands[i].func(argc, argv);
                }
                break;
            }
        }

        if (!found) {
            char bin_path[FAT32_MAX_PATH];
            int bin_found = 0;
            
            int has_path = 0;
            int has_ext = 0;
            for (int i = 0; cmd[i]; i++) {
                if (cmd[i] == '/' || cmd[i] == ':') has_path = 1;
                if (cmd[i] == '.') has_ext = 1;
            }
            
            /* Try original name first */
            strcpy(bin_path, cmd);
            sys_dirent_t test;
            if (sys_stat(bin_path, &test) == 0 && !test.is_directory) {
                bin_found = 1;
            }
            
            /* If not found and no extension, try adding .elf */
            if (!bin_found && !has_ext) {
                snprintf(bin_path, sizeof(bin_path), "%s.elf", cmd);
                if (sys_stat(bin_path, &test) == 0 && !test.is_directory) {
                    bin_found = 1;
                }
            }
            
            /* If not found and no path, try current directory */
            if (!bin_found && !has_path) {
                char cwd[FAT32_MAX_PATH];
                sys_getcwd(cwd, sizeof(cwd));
                
                if (!has_ext) {
                    snprintf(bin_path, sizeof(bin_path), "%s%s.elf", cwd, cmd);
                    if (sys_stat(bin_path, &test) == 0 && !test.is_directory) {
                        bin_found = 1;
                    }
                }
                
                if (!bin_found) {
                    snprintf(bin_path, sizeof(bin_path), "%s%s", cwd, cmd);
                    if (sys_stat(bin_path, &test) == 0 && !test.is_directory) {
                        bin_found = 1;
                    }
                }
            }
            
            if (bin_found) {
                if (sys_spawn(bin_path) != 0) {
                    sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
                    printf("Could not execute %s\n", bin_path);
                    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                }
                continue;
            }
            
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

static void cmd_help(int argc, char *argv[]) {
    (void)argc; (void)argv;
    Command sorted[sizeof(commands) / sizeof(commands[0])];
    for (size_t i = 0; i < COMMAND_COUNT; i++)
        sorted[i] = commands[i];

    sort_commands(sorted, COMMAND_COUNT);

    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\nAvailable commands:\n\n");

    for (size_t i = 0; i < COMMAND_COUNT; i++) {
        sys_setcolor(COLOR_INFO_BG, COLOR_INFO_FG);
        printf("  %-22s", sorted[i].name);

        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("%s\n", sorted[i].desc);
    }

    printf("\n");
}

static void cmd_exit(int argc, char *argv[]) {
    (void)argc; (void)argv;
    printf("This action will return you to the kernel shell.\nMake sure you know what you are doing! Proceed? (Y/N) ");
    char conf[80];
    sys_readline(conf,strlen(conf));
    if (!strcmp(conf,"Y") || !strcmp(conf,"y")) sys_exit();
    else {};
}

static void cmd_mem(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sys_meminfo_t meminfo = {0};
    sys_get_meminfo(&meminfo);
    const double total = (double)meminfo.total_kb;
    const double free = (double)meminfo.free_kb;
    printf("\nMemory statistics:\n\n");
    printf("  Total installed:");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                    printf(" %.2f MiB", (total / 1024));
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);
    printf("\n  Free:");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                    printf(" %.2f MiB (%.1f%)", (free / 1024), (free / total)*100);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG); printf("\n\n");
}

static void cmd_rtc(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sys_time_t current;
    sys_get_time(&current);

    printf("Current time: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
    printf("%02u/%02u/%04u %02u:%02u:%02u",
       current.day, current.month, current.year,
       current.hour, current.minute, current.second);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG); printf("\n");
}

static void cmd_heap(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sys_heapinfo_t info;
    if (sys_get_heapinfo(&info) != 0) {
        printf("Error getting heap info\n");
        return;
    }
    printf("\nHeap statistics:\n\n");
    printf("  Total heap size: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                        printf("%u KiB\n", info.total_kb);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);
    printf("  Used: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                        printf("%u KiB\n", info.used_bytes / 1024);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);
    printf("  Free: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                        printf("%u KiB\n", info.free_bytes / 1024);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);                    
    printf("  Total allocated: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                        printf("%u KiB\n", info.total_allocated / 1024);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG);                    
    printf("  Total freed: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
                        printf("%u KiB\n", info.total_freed / 1024);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG); printf("\n");
}

static void cmd_cls(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sys_clear();
}

static void cmd_cat(int argc, char *argv[]) {
    if (argc < 2) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: cat <file>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    const char *pattern = argv[1];
    int has_wildcard = (strchr(pattern, '*') != NULL || strchr(pattern, '?') != NULL);
    
    if (!has_wildcard) {
        int fd = sys_open(pattern, "r");
        if (fd < 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot open file '%s'\n", pattern);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        char buf[512];
        int n;
        while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
            buf[n] = '\0';
            sys_write(buf);
        }

        sys_close(fd);
        return;
    }
    
    char path_buf[FAT32_MAX_PATH];
    sys_getcwd(path_buf, sizeof(path_buf));
    size_t len = strlen(path_buf);
    if (len > 3 && path_buf[len - 1] == '/')
        path_buf[len - 1] = '\0';
    
    sys_dirent_t entries[64];
    int count = sys_readdir(path_buf, entries, 64);
    
    if (count < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot read directory\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }
    
    int found = 0;
    
    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) continue;
        
        if (match_wildcard(pattern, entries[i].name)) {
            found++;
            
            if (count > 1) {
                sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
                printf("\n==> %s <==\n", entries[i].name);
                sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            }
            
            int fd = sys_open(entries[i].name, "r");
            if (fd < 0) {
                sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Error: cannot open '%s'\n", entries[i].name);
                sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                continue;
            }

            char buf[512];
            int n;
            while ((n = sys_read(fd, buf, sizeof(buf) - 1)) > 0) {
                buf[n] = '\0';
                sys_write(buf);
            }

            sys_close(fd);
        }
    }
    
    if (found == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("No files match pattern '%s'\n", pattern);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }
}

static void cmd_cd(int argc, char *argv[]) {
    char path_buf[FAT32_MAX_PATH];
    const char *path = NULL;

    if (argc < 2) {
        path = "/";
    } else {
        sys_getcwd(path_buf, sizeof(path_buf));
        
        if (!strcmp(argv[1], ".")) {
            size_t len = strlen(path_buf);
            if (len > 3 && path_buf[len - 1] == '/')
                path_buf[len - 1] = '\0';
            path = path_buf;
        } 
        else if (!strcmp(argv[1], "..")) {
            size_t len = strlen(path_buf);
            if (len > 3 && path_buf[len - 1] == '/') 
                path_buf[len - 1] = '\0';
            
            char *last = strrchr(path_buf, '/');
            if (last && last > path_buf + 2) 
                *(last + 1) = '\0';
            else 
                strcpy(path_buf, "C:/");
            
            path = path_buf;
        } 
        else {
            path = argv[1];
        }
    }
    
    if (sys_chdir(path) != 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot change directory to '%s'\n", path);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }
}

static void cmd_echo(int argc, char *argv[]) {
    if (argc < 3) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: echo <text> <file>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    int fd = sys_open(argv[2], "w");
    if (fd < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot create file '%s'\n", argv[2]);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    int len = strlen(argv[1]);
    if (sys_write_file(fd, argv[1], len) != len) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error writing to file\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }

    sys_close(fd);
}

static void cmd_ls(int argc, char *argv[]) {
    char path_buf[FAT32_MAX_PATH] = {0};
    const char *path = NULL;

    if (argc < 2) {
        sys_getcwd(path_buf, sizeof(path_buf));
        size_t len = strlen(path_buf);
        if (len > 3 && path_buf[len - 1] == '/')
            path_buf[len - 1] = '\0';
        path = path_buf;
    } 
    else if (!strcmp(argv[1], ".")) {
        sys_getcwd(path_buf, sizeof(path_buf));
        size_t len = strlen(path_buf);
        if (len > 3 && path_buf[len - 1] == '/')
            path_buf[len - 1] = '\0';
        path = path_buf;
    }
    else if (!strcmp(argv[1], "..")) {
        sys_getcwd(path_buf, sizeof(path_buf));
        size_t len = strlen(path_buf);
        if (len > 3 && path_buf[len - 1] == '/') 
            path_buf[len - 1] = '\0';
        
        char *last = strrchr(path_buf, '/');
        if (last && last > path_buf + 2) 
            *(last + 1) = '\0';
        else 
            strcpy(path_buf, "C:/");
        
        path = path_buf;
    }
    else {
        path = argv[1];
    }
    
    sys_dirent_t entries[64];
    int count = sys_readdir(path, entries, 64);
    
    if (count < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot list '%s'\n", path);
        return;
    }

    if (count == 0) {
        sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
        printf("(empty)\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (count > 1) {
        for (int i = 0; i < count - 1; i++) {
            for (int j = 0; j < count - i - 1; j++) {
                if (compare_entries(&entries[j], &entries[j + 1]) > 0) {
                    sys_dirent_t tmp = entries[j];
                    entries[j] = entries[j + 1];
                    entries[j + 1] = tmp;
                }
            }
        }
    }
    
    printf("\n");
    int dirs = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) {
            sys_setcolor(COLOR_NORMAL_BG, COLOR_DIR_FG);
            printf("  %-43s ", entries[i].name);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
            printf("<DIR>\n");
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            dirs++;
        } else {
            sys_setcolor(COLOR_NORMAL_BG, 15);
            printf("  %-34s ", entries[i].name);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
            if (entries[i].size > 1024 * 1024) {
                printf("  %8u MiB\n", entries[i].size / (1024 * 1024));
            } else if (entries[i].size > 1024) {
                printf("  %8u KiB\n", entries[i].size / 1024);
            } else {
                printf("%8u bytes\n", entries[i].size);
            }
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        }
    }
    
    printf("\n");
    sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
    printf("  %d files, %d catalogues\n  %d items in total\n\n", count - dirs, dirs, count);
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

static void cmd_mkdir(int argc, char *argv[]) {
    if (argc < 2) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: mkdir <dir>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (sys_mkdir(argv[1]) != 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot create directory '%s'\n", argv[1]);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }
}

static void cmd_rm(int argc, char *argv[]) {
    if (argc < 2) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: rm <file>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    const char *pattern = argv[1];
    int has_wildcard = (strchr(pattern, '*') != NULL || strchr(pattern, '?') != NULL);
    
    if (!has_wildcard) {
        if (sys_unlink(pattern) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot delete file '%s'\n", pattern);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        }
        return;
    }
    
    char path_buf[FAT32_MAX_PATH];
    sys_getcwd(path_buf, sizeof(path_buf));
    size_t len = strlen(path_buf);
    if (len > 3 && path_buf[len - 1] == '/')
        path_buf[len - 1] = '\0';
    
    sys_dirent_t entries[64];
    int count = sys_readdir(path_buf, entries, 64);
    
    if (count < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot read directory\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }
    
    int deleted = 0;
    int failed = 0;
    
    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) continue;
        
        if (match_wildcard(pattern, entries[i].name)) {
            if (sys_unlink(entries[i].name) == 0) {
                deleted++;
                printf("Deleted: %s\n", entries[i].name);
            } else {
                failed++;
                sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Failed: %s\n", entries[i].name);
                sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            }
        }
    }
    
    if (deleted == 0 && failed == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("No files match pattern '%s'\n", pattern);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    } else {
        printf("\nDeleted %d file(s)", deleted);
        if (failed > 0) {
            printf(", %d failed", failed);
        }
        printf("\n");
    }
}

static void cmd_rmdir(int argc, char *argv[]) {
    if (argc < 2) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: rmdir <dir>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (sys_rmdir(argv[1]) != 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot remove directory '%s'\n", argv[1]);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }
}

static void cmd_ps(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sys_taskinfo_t tasks[32];
    int count = sys_get_tasks(tasks, 32);
    
    if (count <= 0) {
        printf("No tasks running\n");
        return;
    }

    printf("\n");
    sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
    printf("PID  NAME              STATE        PRIORITY\n");
    sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
    printf("---  ----------------  -----------  ---------\n");
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    
    for (int i = 0; i < count; i++) {
        const char *state_str;
        uint8_t state_color;
        
        switch (tasks[i].state) {
            case 0:
                state_str = "READY";
                state_color = 14;
                break;
            case 1:
                state_str = "RUNNING";
                state_color = 10;
                break;
            case 2:
                state_str = "SLEEPING";
                state_color = 11;
                break;
            case 3:
                state_str = "BLOCKED";
                state_color = 12;
                break;
            default:
                state_str = "TERMINATED";
                state_color = 8;
                break;
        }
        
        const char *prio_str;
        switch (tasks[i].priority) {
            case 0: prio_str = "HIGH"; break;
            case 1: prio_str = "NORMAL"; break;
            case 2: prio_str = "LOW"; break;
            case 3: prio_str = "IDLE"; break;
            default: prio_str = "UNKNOWN"; break;
        }
        
        sys_setcolor(COLOR_NORMAL_BG, 15);
        printf("%-4u ", tasks[i].tid);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("%-16s  ", tasks[i].name);
        
        sys_setcolor(COLOR_NORMAL_BG, state_color);
        printf("%-11s  ", state_str);
        
        sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
        printf("%-9s\n", prio_str);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }
    
    printf("\n");
}

static void cmd_uptime(int argc, char *argv[]) {
    (void)argc; (void)argv;
    uint32_t ticks = sys_uptime();
    uint32_t seconds = ticks / 100;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;

    printf("System uptime: ");
    sys_setcolor(COLOR_INFO_BG, COLOR_INFO_FG);
    
    if (days > 0) {
        printf("%u day%s, ", days, days == 1 ? "" : "s");
    }
    printf("%02u:%02u:%02u", hours % 24, minutes % 60, seconds % 60);
    
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\n");
}