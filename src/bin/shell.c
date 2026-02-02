#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

#define COLOR_PROMPT_BG    COLOR_BLACK
#define COLOR_PROMPT_FG    COLOR_LIGHT_GREEN
#define COLOR_NORMAL_BG    COLOR_BLACK
#define COLOR_NORMAL_FG    COLOR_LIGHT_GRAY
#define COLOR_ERROR_BG     COLOR_BLACK
#define COLOR_ERROR_FG     COLOR_LIGHT_RED
#define COLOR_INFO_BG      COLOR_BLACK
#define COLOR_INFO_FG      COLOR_LIGHT_CYAN
#define COLOR_DIR_FG       COLOR_YELLOW 

#define MAX_ARGS 8
#define FAT32_MAX_PATH 256
#define PAGE_LINES 25

#define COMMAND_COUNT (sizeof(commands) / sizeof((commands)[0]))

#define printf paged_printf

const char *shell_version = "oslet-v06";

static int pager_enabled = 0;
static int pager_line_count = 0;

static void paged_write(const char *str) {
    if (!pager_enabled) {
        sys_write(str);
        return;
    }

    for (int i = 0; str[i]; i++) {
        char c[2] = { str[i], '\0' };
        sys_write(c);

        if (str[i] == '\n') {
            pager_line_count++;
            if (pager_line_count >= PAGE_LINES) {
                sys_write("Press any key to continue");
                int key = sys_getchar();
                sys_write("\r                         \r");
                pager_line_count = 0;
                /* ESC or 'q' to quit paging */
                if (key == 27 || key == 'q' || key == 'Q') {
                    pager_enabled = 0;
                    return;
                }
            }
        }
    }
}

static int paged_printf(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    buf[sizeof(buf) - 1] = '\0';
    size_t slen = strlen(buf);
    utf8_to_cp437_string(buf, slen);
    paged_write(buf);
    return len;
}

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

static int compare_entries(const void *a, const void *b) {
    const sys_dirent_t *ea = (const sys_dirent_t*)a;
    const sys_dirent_t *eb = (const sys_dirent_t*)b;

    if (ea->is_directory && !eb->is_directory) return -1;
    if (!ea->is_directory && eb->is_directory) return 1;

    return strcasecmp(ea->name, eb->name);
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
static void cmd_shutdown(int argc, char *argv[]);
static void cmd_reboot(int argc, char *argv[]);
static void cmd_cp(int argc, char *argv[]);
static void cmd_mv(int argc, char *argv[]);
static void cmd_ren(int argc, char *argv[]);
static void cmd_find(int argc, char *argv[]);
static void cmd_ver(int argc, char *argv[]);

static const Command commands[] = {
    { "cat",    "cat <file>",          "Display file contents",      cmd_cat },
    { "cd",     "cd <dir>",            "Change directory",           cmd_cd },
    { "cls",    "cls",                 "Clear screen",               cmd_cls },
    { "cp",     "cp <src> <dst>",      "Alias of 'copy'",            cmd_cp },
    { "copy",   "copy <src> <dst>",    "Copy file",                  cmd_cp },
    { "echo",   "echo <text> <file>",  "Write text to file",         cmd_echo },
    { "heap",   "heap",                "Show heap statistics",       cmd_heap },
    { "help",   "help",                "Show this command list",     cmd_help },
    { "dir",    "dir <path>",          "List directory",             cmd_ls },
    { "ls",     "ls <path>",           "Alias of 'dir'",             cmd_ls },
    { "mem",    "mem",                 "Show memory statistics",     cmd_mem },
    { "mkdir",  "mkdir <dir>",         "Create directory",           cmd_mkdir },
    { "mv",     "mv <src> <dst>",      "Alias of 'move'",            cmd_mv },
    { "move",   "move <src> <dst>",    "Move file",                  cmd_mv },
    { "ps",     "ps",                  "List running tasks",         cmd_ps },
    { "ren",    "ren <old> <new>",     "Rename file",                cmd_ren },
    { "rm",     "rm <file>",           "Alias of 'del'",             cmd_rm },
    { "del",    "del <file>",          "Delete file",                cmd_rm },
    { "find",   "find <pattern>",      "Find files corresponding to pattern", cmd_find },
    { "rmdir",  "rmdir <dir>",         "Remove directory",           cmd_rmdir },
    { "ver",    "ver",                 "Display system version",     cmd_ver },
    { "time",   "time",                "Show current time and date", cmd_rtc },
    { "uptime", "uptime",              "Show system uptime",         cmd_uptime },
    { "shutdown", "shutdown",          "Power off computer",         cmd_shutdown },
    { "reboot",   "reboot",            "Restart computer",           cmd_reboot },
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
        int n = sys_readline(line, sizeof(line));
        if (n <= 0) {
            sys_yield();
            continue;
        }
        
        /* Trim newlines */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        
        if (line[0] == '\0') continue;

        argc = parse_args(line, argv, MAX_ARGS);

        if (argc == 0)
            continue;

        /* Check for /p flag (pager) - can be anywhere in args */
        pager_enabled = 0;
        pager_line_count = 0;
        for (int i = 0; i < argc; i++) {
            if (!strcmp(argv[i], "/p") || !strcmp(argv[i], "/P")) {
                pager_enabled = 1;
                /* Remove /p from argv */
                for (int j = i; j < argc - 1; j++) {
                    argv[j] = argv[j + 1];
                }
                argv[--argc] = NULL;
                break;
            }
        }

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

        pager_enabled = 0;

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
    printf("This action will result in exiting the shell program.\nMake sure you know what you are doing! Proceed? (Y/N) ");
    char conf[80];
    sys_readline(conf, sizeof(conf));
    if (!strcmp(conf,"Y") || !strcmp(conf,"y")) sys_exit();
}

static void cmd_mem(int argc, char *argv[]) {
    (void)argc; (void)argv;
    sys_meminfo_t meminfo;
    sys_get_meminfo(&meminfo);
    double total = (double)meminfo.total_kb;
    double free = (double)meminfo.free_kb;
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

    printf("\nCurrent time: ");
    sys_setcolor(COLOR_INFO_BG,COLOR_INFO_FG);
    printf("%02u/%02u/%04u %02u:%02u:%02u",
       current.day, current.month, current.year,
       current.hour, current.minute, current.second);
    sys_setcolor(COLOR_NORMAL_BG,COLOR_NORMAL_FG); printf("\n\n");
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

static int copy_file(const char *src, const char *dst) {
    int fd_src = sys_open(src, "r");
    if (fd_src < 0) return -1;

    int fd_dst = sys_open(dst, "w");
    if (fd_dst < 0) {
        sys_close(fd_src);
        return -2;
    }

    char buf[512];
    int n;
    int total = 0;

    while ((n = sys_read(fd_src, buf, sizeof(buf))) > 0) {
        if (sys_write_file(fd_dst, buf, n) != n) {
            sys_close(fd_src);
            sys_close(fd_dst);
            sys_unlink(dst);
            return -3;
        }
        total += n;
    }

    sys_close(fd_src);
    sys_close(fd_dst);
    return total;
}

/* Helper: split a path into directory (with trailing '/') and pattern/filename */
static void split_path_pattern(const char *path, char *dir_out, size_t dir_out_size, const char **pattern_out) {
    char *last_slash = strrchr(path, '/');
    if (last_slash) {
        size_t dir_len = last_slash - path + 1;
        if (dir_len >= dir_out_size) {
            dir_out[0] = '\0';
            *pattern_out = path;
            return;
        }
        memcpy(dir_out, path, dir_len);
        dir_out[dir_len] = '\0';
        *pattern_out = last_slash + 1;
    } else {
        sys_getcwd(dir_out, dir_out_size);
        if (dir_out[0] == '\0') strcpy(dir_out, "C:/");
        *pattern_out = path;
    }
}

/* Expand wildcard pattern into full paths (dir + filename). Returns number of matches or -1 on error */
static int expand_pattern(const char *pattern, char matches[][FAT32_MAX_PATH], int max_matches) {
    char dir[FAT32_MAX_PATH];
    const char *pat;
    split_path_pattern(pattern, dir, sizeof(dir), &pat);

    sys_dirent_t entries[128];
    int count = sys_readdir(dir, entries, 128);
    if (count < 0) return -1;

    int found = 0;
    for (int i = 0; i < count && found < max_matches; i++) {
        if (entries[i].is_directory) continue;
        if (str_match_wildcard(pat, entries[i].name)) {
            snprintf(matches[found], FAT32_MAX_PATH, "%s%s", dir, entries[i].name);
            found++;
        }
    }
    return found;
}

static void join_path(char *out, size_t out_size, const char *dir, const char *name) {
    size_t dlen = strlen(dir);
    if (dlen > 0 && dir[dlen - 1] == '/') {
        snprintf(out, out_size, "%s%s", dir, name);
    } else {
        snprintf(out, out_size, "%s/%s", dir, name);
    }
}

static void cmd_cp(int argc, char *argv[]) {
    if (argc < 3) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: cp <source> <destination>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    const char *src = argv[1];
    const char *dst = argv[2];

    int src_has_wild = (strchr(src, '*') != NULL || strchr(src, '?') != NULL);

    /* Single source (no wildcard) -- support copying into directories */
    if (!src_has_wild) {
        sys_dirent_t entry;
        if (sys_stat(src, &entry) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: source file '%s' not found\n", src);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        if (entry.is_directory) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot copy directory '%s'\n", src);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        /* If destination is an existing directory or ends with '/', copy into that directory */
        sys_dirent_t dst_entry;
        int dst_is_dir = 0;
        size_t dst_len = strlen(dst);
        if ((dst_len > 0 && dst[dst_len - 1] == '/') || (sys_stat(dst, &dst_entry) == 0 && dst_entry.is_directory)) {
            dst_is_dir = 1;
        }

        if (dst_is_dir) {
            char dst_dir[FAT32_MAX_PATH];
            if (dst[dst_len - 1] == '/') strcpy(dst_dir, dst);
            else snprintf(dst_dir, sizeof(dst_dir), "%s/", dst);

            const char *base = strrchr(src, '/');
            if (!base) base = src; else base++;

            char dst_path[FAT32_MAX_PATH];
            join_path(dst_path, sizeof(dst_path), dst_dir, base);

            int result = copy_file(src, dst_path);
            if (result < 0) {
                sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
                switch (result) {
                    case -1:
                        printf("Error: cannot open source file '%s'\n", src);
                        break;
                    case -2:
                        printf("Error: cannot create destination file '%s'\n", dst_path);
                        break;
                    case -3:
                        printf("Error: write failed during copy\n");
                        break;
                }
                sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            } else {
                printf("Copied %d bytes\n", result);
            }

            return;
        }

        /* Regular single-file copy (dst is a file path) */
        int result = copy_file(src, dst);
        if (result < 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            switch (result) {
                case -1:
                    printf("Error: cannot open source file '%s'\n", src);
                    break;
                case -2:
                    printf("Error: cannot create destination file '%s'\n", dst);
                    break;
                case -3:
                    printf("Error: write failed during copy\n");
                    break;
            }
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        } else {
            printf("Copied %d bytes\n", result);
        }

        return;
    }

    /* Wildcard source - expand and copy multiple files */
    char matches[128][FAT32_MAX_PATH];
    int found = expand_pattern(src, matches, 128);
    if (found < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot read source directory or pattern invalid\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (found == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("No files match pattern '%s'\n", src);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Verify destination is (or will be treated as) a directory for multiple files */
    sys_dirent_t dst_entry;
    int dst_is_dir = 0;
    size_t dst_len = strlen(dst);
    if ((dst_len > 0 && dst[dst_len - 1] == '/') || (sys_stat(dst, &dst_entry) == 0 && dst_entry.is_directory)) {
        dst_is_dir = 1;
    }

    if (!dst_is_dir && found > 1) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: destination must be a directory when copying multiple files\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Safety: refuse to copy into the same directory (avoids accidental destructive operations) */
    char src_dir[FAT32_MAX_PATH]; const char *pat;
    split_path_pattern(src, src_dir, sizeof(src_dir), &pat);

    char dst_dir_norm[FAT32_MAX_PATH];
    if (dst_is_dir) {
        if (dst[dst_len - 1] == '/') strcpy(dst_dir_norm, dst);
        else snprintf(dst_dir_norm, sizeof(dst_dir_norm), "%s/", dst);

        char a[FAT32_MAX_PATH]; char b[FAT32_MAX_PATH];
        str_toupper(a); str_toupper(b);
        strncpy(a, dst_dir_norm, sizeof(a)-1); a[sizeof(a)-1] = '\0';
        strncpy(b, src_dir, sizeof(b)-1); b[sizeof(b)-1] = '\0';
        str_toupper(a); str_toupper(b);

        if (!strcmp(a, b)) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot copy into the same directory '%s'\n", dst_dir_norm);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }
    }

    int total_bytes = 0;
    int success_count = 0;

    for (int i = 0; i < found; i++) {
        const char *fullsrc = matches[i];
        const char *base = strrchr(fullsrc, '/');
        if (!base) base = fullsrc; else base++;

        char dst_path[FAT32_MAX_PATH];
        if (dst_is_dir) {
            join_path(dst_path, sizeof(dst_path), dst, base);
        } else {
            /* Single file destination: use provided destination path */
            snprintf(dst_path, sizeof(dst_path), "%s", dst);
        }

        int res = copy_file(fullsrc, dst_path);
        if (res < 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error copying '%s' to '%s'\n", fullsrc, dst_path);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        } else {
            total_bytes += res;
            success_count++;
        }
    }
    printf("Copied %d files (%d bytes)\n", success_count, total_bytes);
}

static void cmd_mv(int argc, char *argv[]) {
    if (argc < 3) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: mv <source> <destination>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    const char *src = argv[1];
    const char *dst = argv[2];

    int src_has_wild = (strchr(src, '*') != NULL || strchr(src, '?') != NULL);

    /* Single source (no wildcard) */
    if (!src_has_wild) {
        sys_dirent_t entry;
        if (sys_stat(src, &entry) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: source file '%s' not found\n", src);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        if (entry.is_directory) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot move directory '%s'\n", src);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        /* If destination is a directory, move into that directory */
        sys_dirent_t dst_entry;
        int dst_is_dir = 0;
        size_t dst_len = strlen(dst);
        if ((dst_len > 0 && dst[dst_len - 1] == '/') || (sys_stat(dst, &dst_entry) == 0 && dst_entry.is_directory)) {
            dst_is_dir = 1;
        }

        char dst_path[FAT32_MAX_PATH];
        if (dst_is_dir) {
            const char *base = strrchr(src, '/');
            if (!base) base = src; else base++;
            join_path(dst_path, sizeof(dst_path), dst, base);
        } else {
            snprintf(dst_path, sizeof(dst_path), "%s", dst);
        }

        int result = copy_file(src, dst_path);
        if (result < 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            switch (result) {
                case -1:
                    printf("Error: cannot open source file '%s'\n", src);
                    break;
                case -2:
                    printf("Error: cannot create destination file '%s'\n", dst_path);
                    break;
                case -3:
                    printf("Error: write failed during move\n");
                    break;
            }
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        if (sys_unlink(src) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Warning: copied but failed to delete source '%s'\n", src);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        printf("Moved %d bytes\n", result);
        return;
    }

    /* Wildcard source - expand and move multiple files */
    char matches[128][FAT32_MAX_PATH];
    int found = expand_pattern(src, matches, 128);
    if (found < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot read source directory or pattern invalid\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (found == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("No files match pattern '%s'\n", src);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Destination must be a directory for multiple files */
    sys_dirent_t dst_entry;
    int dst_is_dir = 0;
    size_t dst_len = strlen(dst);
    if ((dst_len > 0 && dst[dst_len - 1] == '/') || (sys_stat(dst, &dst_entry) == 0 && dst_entry.is_directory)) {
        dst_is_dir = 1;
    }

    if (!dst_is_dir) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: destination must be a directory when moving multiple files\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Safety: refuse to move into the same directory */
    char src_dir[FAT32_MAX_PATH]; const char *pat;
    split_path_pattern(src, src_dir, sizeof(src_dir), &pat);

    char dst_dir_norm[FAT32_MAX_PATH];
    if (dst[dst_len - 1] == '/') strcpy(dst_dir_norm, dst);
    else snprintf(dst_dir_norm, sizeof(dst_dir_norm), "%s/", dst);

    char a[FAT32_MAX_PATH]; char b[FAT32_MAX_PATH];
    strncpy(a, dst_dir_norm, sizeof(a)-1); a[sizeof(a)-1] = '\0';
    strncpy(b, src_dir, sizeof(b)-1); b[sizeof(b)-1] = '\0';
    str_toupper(a); str_toupper(b);

    if (!strcmp(a, b)) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot move into the same directory '%s'\n", dst_dir_norm);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    int total_bytes = 0;
    int success_count = 0;

    for (int i = 0; i < found; i++) {
        const char *fullsrc = matches[i];
        const char *base = strrchr(fullsrc, '/');
        if (!base) base = fullsrc; else base++;

        char dst_path[FAT32_MAX_PATH];
        join_path(dst_path, sizeof(dst_path), dst, base);

        int res = copy_file(fullsrc, dst_path);
        if (res < 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error moving '%s' to '%s'\n", fullsrc, dst_path);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            continue;
        }

        if (sys_unlink(fullsrc) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Warning: moved but failed to delete source '%s'\n", fullsrc);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            continue;
        }

        total_bytes += res;
        success_count++;
    }

    printf("Moved %d files, %d bytes\n", success_count, total_bytes);
}

static void cmd_ren(int argc, char *argv[]) {
    if (argc < 3) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: ren <oldname> <newname>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    const char *oldname = argv[1];
    const char *newname = argv[2];

    /* Check if source exists and is a file */
    sys_dirent_t entry;
    if (sys_stat(oldname, &entry) != 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: file '%s' not found\n", oldname);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (entry.is_directory) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot rename directory '%s'\n", oldname);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Check if destination already exists */
    if (sys_stat(newname, NULL) == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: file '%s' already exists\n", newname);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Copy file to new name */
    int result = copy_file(oldname, newname);

    if (result < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        switch (result) {
            case -1:
                printf("Error: cannot open file '%s'\n", oldname);
                break;
            case -2:
                printf("Error: cannot create file '%s'\n", newname);
                break;
            case -3:
                printf("Error: write failed during rename\n");
                break;
        }
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    /* Delete old file */
    if (sys_unlink(oldname) != 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Warning: copied but failed to delete '%s'\n", oldname);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }
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
            paged_write(buf);
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

        if (str_match_wildcard(pattern, entries[i].name)) {
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
                paged_write(buf);
            }

            sys_close(fd);
        }
    }

    if (found == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("No files match pattern '%s'\n", pattern);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    }

    printf("\n");
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
        /* Decode FAT date/time */
        int day = entries[i].mdate & 0x1F;
        int month = (entries[i].mdate >> 5) & 0x0F;
        int year = ((entries[i].mdate >> 9) & 0x7F) + 1980;
        int hour = (entries[i].mtime >> 11) & 0x1F;
        int min = (entries[i].mtime >> 5) & 0x3F;

        if (entries[i].is_directory) {
            sys_setcolor(COLOR_NORMAL_BG, COLOR_DIR_FG);
            printf("  %-32s ", entries[i].name);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
            printf("<DIR>");
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            printf("     %02d/%02d/%04d %02d:%02d\n", day, month, year, hour, min);
            dirs++;
        } else {
            sys_setcolor(COLOR_NORMAL_BG, 15);
            printf("  %-28s ", entries[i].name);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_INFO_FG);
            if (entries[i].size > 1024 * 1024) {
                printf("%6u MB", entries[i].size / (1024 * 1024));
            } else if (entries[i].size > 1024) {
                printf("%6u kB", entries[i].size / 1024);
            } else {
                printf("%6u B ", entries[i].size);
            }
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            printf("     %02d/%02d/%04d %02d:%02d\n", day, month, year, hour, min);
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
        /* Make sure the target exists and is not a directory */
        sys_dirent_t e;
        if (sys_stat(pattern, &e) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: file '%s' not found\n", pattern);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        if (e.is_directory) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot delete directory '%s', use rmdir\n", pattern);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            return;
        }

        if (sys_unlink(pattern) != 0) {
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Error: cannot delete file '%s'\n", pattern);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        } else {
            printf("Deleted '%s'\n", pattern);
        }
        return;
    }

    /* Wildcard path - expand pattern (supports directory in pattern) */
    char matches[128][FAT32_MAX_PATH];
    int found = expand_pattern(pattern, matches, 128);
    if (found < 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: cannot read directory for pattern '%s'\n", pattern);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    if (found == 0) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("No files match pattern '%s'\n", pattern);
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    int deleted = 0;
    int failed = 0;

    for (int i = 0; i < found; i++) {
        sys_dirent_t e;
        if (sys_stat(matches[i], &e) != 0) continue;
        if (e.is_directory) continue;

        if (sys_unlink(matches[i]) == 0) {
            deleted++;
            printf("Deleted: %s\n", matches[i]);
        } else {
            failed++;
            sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
            printf("Failed: %s\n", matches[i]);
            sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
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

/* Recursive search helper for 'find' */
static void find_recursive(const char *dir, const char *pattern, int depth) {
    if (depth > 32) return; /* prevent runaway recursion */

    sys_dirent_t entries[128];
    int count = sys_readdir(dir, entries, 128);
    if (count < 0) return;

    for (int i = 0; i < count; i++) {
        const char *name = entries[i].name;
        /* Skip '.' and '..' if present */
        if (!strcmp(name, ".") || !strcmp(name, "..")) continue;

        char full[FAT32_MAX_PATH];
        join_path(full, sizeof(full), dir, name);

        if (entries[i].is_directory) {
            /* Ensure trailing slash for directories when recursing */
            char subdir[FAT32_MAX_PATH];
            size_t fl = strlen(full);
            if (fl > 0 && full[fl - 1] == '/') {
                snprintf(subdir, sizeof(subdir), "%s", full);
            } else {
                snprintf(subdir, sizeof(subdir), "%s/", full);
            }
            find_recursive(subdir, pattern, depth + 1);
        } else {
            if (str_match_wildcard(pattern, name)) {
                char out[FAT32_MAX_PATH];
                snprintf(out, sizeof(out), "%s", full);
                str_toupper(out);
                printf("%s found in %s\n", pattern, out);
            }
        }
    }
}

static void cmd_find(int argc, char *argv[]) {
    if (argc < 2) {
        sys_setcolor(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Usage: find <pattern>\n");
        sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        return;
    }

    char start_dir[FAT32_MAX_PATH] = {0};
    sys_getcwd(start_dir, sizeof(start_dir));
    if (start_dir[0] == '\0') strcpy(start_dir, "C:/");

    /* Ensure directory has trailing slash */
    size_t len = strlen(start_dir);
    if (len > 0 && start_dir[len - 1] != '/') {
        if (len + 2 < sizeof(start_dir)) strcat(start_dir, "/");
    }

    find_recursive(start_dir, argv[1], 0);
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
    } else {
        printf("Deleted: %s\n", argv[1]);
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

    printf("\nSystem uptime: ");
    sys_setcolor(COLOR_INFO_BG, COLOR_INFO_FG);
    
    if (days > 0) {
        printf("%u day%s, ", days, days == 1 ? "" : "s");
    }
    printf("%02u:%02u:%02u", hours % 24, minutes % 60, seconds % 60);
    
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\n\n");
}

static void cmd_ver(int argc, char *argv[]) {
    (void)argc; (void)argv;
    
    printf("\nosLET ");
    sys_setcolor(COLOR_INFO_BG, COLOR_INFO_FG);
    printf("%s",sys_version());
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\nShell version ");
    sys_setcolor(COLOR_INFO_BG, COLOR_INFO_FG);
    printf("%s",sys_info_shell());
    sys_setcolor(COLOR_NORMAL_BG, COLOR_DARK_GRAY);
    printf("\n\nThis project is currently under development. The author provides this software\n\"as is\" under no warranty of any kind. "
           "Any damages resulting from using osLET\non valuable data or real hardware will be your fault.");
    sys_setcolor(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\n\n");
}

static void cmd_reboot(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("Rebooting...\n");
    sys_reboot();
}

static void cmd_shutdown(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("Shutting down...\n");
    sys_shutdown();
}