#include <stdbool.h>
#include "shell.h"
#include "console.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/fat32.h"
#include "mem/pmm.h"
#include "mem/heap.h"
#include "timer.h"
#include "task.h"
#include "rtc.h"
#include "syscall.h"
#include "exec.h"

/* Color schemes */
#define COLOR_PROMPT_BG    0
#define COLOR_PROMPT_FG    10  /* bright green */
#define COLOR_NORMAL_BG    0
#define COLOR_NORMAL_FG    7   /* light gray */
#define COLOR_ERROR_BG     0
#define COLOR_ERROR_FG     12  /* bright red */
#define COLOR_SUCCESS_BG   0
#define COLOR_SUCCESS_FG   10  /* bright green */
#define COLOR_INFO_BG      0
#define COLOR_INFO_FG      11  /* bright cyan */
#define COLOR_DIR_FG       14  /* yellow */

#define MAX_ARGS 8

static char current_path[FAT32_MAX_PATH];

int parse_args(char *line, char *argv[], int max_args) {
    int argc = 0;
    bool in_quote = false;
    char *p = line;

    while (*p && argc < max_args) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0')
            break;

        argv[argc++] = p;

        if (*p == '"') {
            in_quote = true;
            argv[argc - 1] = ++p;
        }

        while (*p) {
            if (in_quote && *p == '"') {
                *p++ = '\0';
                in_quote = false;
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
}

static void print_prompt(void) {
    fat32_getcwd(current_path, sizeof(current_path));
    vga_set_color(COLOR_PROMPT_BG, COLOR_PROMPT_FG);
    printf("%s>", current_path);
    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

static int compare_entries(const void *a, const void *b) {
    const fat32_dirent_t *ea = (const fat32_dirent_t*)a;
    const fat32_dirent_t *eb = (const fat32_dirent_t*)b;
    
    /* Directories always on top */
    if (ea->is_directory && !eb->is_directory) return -1;
    if (!ea->is_directory && eb->is_directory) return 1;
    
    /* Sort alphabetically */
    return strcmp_s(ea->name, eb->name);
}

static void print_banner(void) {
    vga_set_color(1, 15);
    printf("  _____   _____  .       ______ _______ \n");
    printf(" |     | |_____  |      |______    |    \n");
    printf(" |_____|  _____| |_____ |______    |    \n");
    printf("                                        \n");
    vga_set_color(0, 8);
    printf("Kernel 0.3.2            EinarTheSad 2025\n\n");
    vga_set_color(0, 7);
}

static void cmd_cat(const char *filename) {
    fat32_file_t *f = fat32_open(filename, "r");
    if (!f) {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot open file '%s'\n", filename);
        return;
    }
    
    char buffer[513];
    int n;
    
    vga_set_color(COLOR_NORMAL_BG, 15);
    while ((n = fat32_read(f, buffer, 512)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }
    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("\n");
    
    fat32_close(f);
}

static void cmd_echo(const char *text, const char *filename) {
    fat32_file_t *f = fat32_open(filename, "w");
    if (!f) {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot create file '%s'\n", filename);
        return;
    }
    
    fat32_write(f, text, strlen_s(text));
    fat32_close(f);
    
    vga_set_color(COLOR_SUCCESS_BG, COLOR_SUCCESS_FG);
    printf("OK ");
    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("Written to %s\n", filename);
}

static void cmd_mkdir(const char *dirname) {
    if (fat32_mkdir(dirname) == 0) {
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Created directory: %s\n", dirname);
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Failed to create '%s'\n", dirname);
    }
}

static void cmd_rm(const char *filename) {
    if (fat32_unlink(filename) == 0) {
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Deleted: %s\n", filename);
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot delete '%s'\n", filename);
    }
}

static void cmd_run(const char *path) {
    if (!path || path[0] == '\0') {
        printf("Usage: run <binary>\n");
        return;
    }
    
    exec_image_t image = {};
    
    if (exec_load(path, &image) != 0) {
        printf("Failed to load binary\n");
        return;
    }
    
    if (exec_run(&image) != 0) {
        printf("Could not execute %s\n", path);
    }
    
    exec_free(&image);
}

static void cmd_rmdir(const char *dirname) {
    if (fat32_rmdir(dirname) == 0) {
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Removed directory: %s\n", dirname);
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot remove '%s' (not empty?)\n", dirname);
    }
}

/* First here, later will move to fat32.c */
static char* check_path(const char *path) {
    fat32_getcwd(current_path, sizeof(current_path));

    if (!path || !path[0] || strcmp_s(path, ".") == 0) {
        /* If the current catalog is not main, trim '/' */
        size_t len = strlen_s(current_path);
        if (len > 3 && current_path[len - 1] == '/')
            current_path[len - 1] = '\0';
        path = current_path;
    } 
    else if (strcmp_s(path, "..") == 0) {
        /* Back up one level */
        size_t len = strlen_s(current_path);
        if (len > 3 && current_path[len - 1] == '/') current_path[len - 1] = '\0';
        char *last = strrchr_s(current_path, '/');
        if (last && last > current_path + 2) *(last + 1) = '\0';
        else strcpy_s(current_path, "C:/", 4);
        path = current_path;
    } 
    else {
        /* nothing */
    }
    return path;
}

static void cmd_cd(const char *path) {
    if (fat32_chdir(check_path(path)) == 0) {
        /* Success - prompt will show new path */
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot access '%s'\n", path);
    }
}

static void cmd_help(void) {
    vga_set_color(0, 14);
    printf("\nAvailable commands:\n\n");
    vga_set_color(0, 7);
    
    printf("  cat <file>           ");
    vga_set_color(0, 8);
    printf("Display file contents\n");
    vga_set_color(0, 7);
    
    printf("  cd <dir>             ");
    vga_set_color(0, 8);
    printf("Change directory\n");
    vga_set_color(0, 7);
    
    printf("  clear                ");
    vga_set_color(0, 8);
    printf("Clear screen\n");
    vga_set_color(0, 7);
    
    printf("  echo <text> > <file> ");
    vga_set_color(0, 8);
    printf("Write text to file\n");
    vga_set_color(0, 7);
   
    printf("  help                 ");
    vga_set_color(0, 8);
    printf("Show this help\n");
    vga_set_color(0, 7);
    
    printf("  ls <path>            ");
    vga_set_color(0, 8);
    printf("List directory\n");
    vga_set_color(0, 7);
    
    printf("  mkdir <dir>          ");
    vga_set_color(0, 8);
    printf("Create directory\n");
    vga_set_color(0, 7);
    
    printf("  mount <drive> <lba>  ");
    vga_set_color(0, 8);
    printf("Mount FAT32 drive\n");
    vga_set_color(0, 7);
      
    printf("  rm <file>            ");
    vga_set_color(0, 8);
    printf("Delete file\n");
    vga_set_color(0, 7);
    
    printf("  rmdir <dir>          ");
    vga_set_color(0, 8);
    printf("Remove directory\n");
    vga_set_color(0, 7);
    
    printf("\n");
    vga_set_color(0, 14);
    printf("System commands:\n\n");
    vga_set_color(0, 7);
    
    printf("  heap                 ");
    vga_set_color(0, 8);
    printf("Show heap statistics\n");
    vga_set_color(0, 7);
    
    printf("  mem                  ");
    vga_set_color(0, 8);
    printf("Show memory statistics\n");
    vga_set_color(0, 7);
    
    printf("  ps                   ");
    vga_set_color(0, 8);
    printf("List running tasks\n");
    vga_set_color(0, 7);
    
    printf("  rtc                  ");
    vga_set_color(0, 8);
    printf("Show current time\n");
    vga_set_color(0, 7);

    printf("  run <file>           ");
    vga_set_color(0, 8);
    printf("Execute a binary file\n");
    vga_set_color(0, 7);
    
    printf("  uptime               ");
    vga_set_color(0, 8);
    printf("Show system uptime\n");
    vga_set_color(0, 7);
    
    printf("\n");
}

static void cmd_ls(const char *path) {
    const char *target = check_path(path);
    
    fat32_dirent_t entries[64];
    int count = fat32_list_dir(target, entries, 64);

    for (int i = 0; i < count - 1; ++i) {
        for (int j = 0; j < count - i - 1; ++j) {
            if (compare_entries(&entries[j], &entries[j + 1]) > 0) {
                fat32_dirent_t tmp = entries[j];
                entries[j] = entries[j + 1];
                entries[j + 1] = tmp;
            }
        }
    }
    
    if (count < 0) {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot list '%s'\n", target);
        return;
    }
    
    if (count == 0) {
        vga_set_color(0, 8);
        printf("(empty)\n");
        vga_set_color(0, 7);
        return;
    }
    
    printf("\n");
    int dirs = 0;
    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) {
            vga_set_color(0, COLOR_DIR_FG);
            printf("  %-43s ", entries[i].name);
            vga_set_color(0, 8);
            printf("<DIR>\n");
            vga_set_color(0, 7);
            dirs++;
        } else {
            vga_set_color(0, 15);
            printf("  %-34s ", entries[i].name);
            vga_set_color(0, 8);
            if (entries[i].size > 1024) {
                printf("  %8u KiB\n", entries[i].size / 1024);
            }
            else if (entries[i].size > 1024 * 1024) {
                printf("  %8u MiB\n", entries[i].size / 1024 * 1024);
            }
            else { printf("%8u bytes\n", entries[i].size); }
            vga_set_color(0, 7);
        }
    }
    
    printf("\n");
    vga_set_color(0, 8);
    printf("  %d files, %d catalogues\n  %d items in total\n\n", count-dirs, dirs, count);
    vga_set_color(0, 7);
}

void shell_init(void) {
    print_banner();
    vga_set_color(0, 7);
}

void shell_run(void) {
    char line[128];
    
    for (;;) {
        print_prompt();
        
        int n = kbd_getline(line, sizeof(line));
        if (n <= 0) {
            __asm__ volatile ("hlt");
            continue;
        }
        
        /* Trim newlines */
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';
        
        if (line[0] == '\0')
            continue;
        
        /* Single-word commands */
        if (!strcmp_s(line, "help")) {
            cmd_help();
            continue;
        }
        
        if (!strcmp_s(line, "clear") || !strcmp_s(line, "cls")) {
            print_banner();
            continue;
        }
        
        if (!strcmp_s(line, "mem")) {
            printf("\n");
            vga_set_color(0, 11);
            printf("Memory Statistics:\n\n");
            vga_set_color(0, 7);
            pmm_print_stats();
            printf("\n");
            continue;
        }
        
        if (!strcmp_s(line, "heap")) {
            printf("\n");
            vga_set_color(0, 11);
            printf("Heap Statistics:\n\n");
            vga_set_color(0, 7);
            heap_print_stats();
            printf("\n");
            continue;
        }
        
        if (!strcmp_s(line, "uptime")) {
            uint32_t ticks = timer_get_ticks();
            uint32_t seconds = ticks / 100;
            uint32_t minutes = seconds / 60;
            uint32_t hours = minutes / 60;
            
            vga_set_color(0, 11);
            printf("Uptime: ");
            vga_set_color(0, 7);
            printf("%uh %um %us ", hours, minutes % 60, seconds % 60);
            vga_set_color(0, 8);
            printf("(%u ticks)\n", ticks);
            vga_set_color(0, 7);
            continue;
        }
        
        if (!strcmp_s(line, "ps")) {
            printf("\n");
            vga_set_color(0, 11);
            printf("Running Tasks:\n\n");
            vga_set_color(0, 7);
            task_list_print();
            printf("\n");
            continue;
        }
        
        if (!strcmp_s(line, "rtc")) {
            rtc_print_time();
            continue;
        }
              
        /* Parse arguments */
        char *argv[MAX_ARGS] = {};
        int argc = parse_args(line, argv, MAX_ARGS);
        if (argc == 0)
            continue;

        char *cmd = argv[0];
        
        /* Commands with arguments */
        if (!strcmp_s(cmd, "ls")) {
            cmd_ls(argv[1]);
            continue;
        }
        
        if (!strcmp_s(cmd, "cat")) {
            if (argc >= 2) {
                cmd_cat(argv[1]);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("cat <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "echo")) {
            if (argc >= 4 && strcmp_s(argv[2], ">") == 0) {
                cmd_echo(argv[1], argv[3]);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("echo <text> > <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "mkdir")) {
            if (argc >= 2) {
                cmd_mkdir(argv[1]);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("mkdir <dir>\n");
            }
            continue;
        }

        if (!strcmp_s(cmd, "rm")) {
            if (argc >= 2) {
                cmd_rm(argv[1]);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("rm <file>\n");
            }
            continue;
        }

        if (!strcmp_s(cmd, "rmdir")) {
            if (argc >= 2) {
                cmd_rmdir(argv[1]);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("rmdir <dir>\n");
            }
            continue;
        }

        if (!strcmp_s(cmd, "cd")) {
            if (argc >= 2) {
                cmd_cd(argv[1]);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("cd <dir>\n");
            }
            continue;
        }

        if (!strcmp_s(cmd, "mount")) {
            if (argc >= 3) {
                uint8_t drive = toupper_s(argv[1][0]);
                uint32_t lba = 0;

                for (int i = 0; argv[2][i]; i++) {
                    if (argv[2][i] >= '0' && argv[2][i] <= '9') {
                        lba = lba * 10 + (argv[2][i] - '0');
                    }
                }

                if (fat32_mount_drive(drive, lba) == 0) {
                    vga_set_color(COLOR_SUCCESS_BG, COLOR_SUCCESS_FG);
                    printf("OK ");
                    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                    printf("Mounted %c: at LBA %u\n", drive, lba);
                } else {
                    vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                    printf("Error: ");
                    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                    printf("Cannot mount %c:\n", drive);
                }
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("mount <drive> <lba>\n");
            }
            continue;
        }

        if (!strcmp_s(cmd, "run")) {
            if (argc >= 2) {
                cmd_run(argv[1]);
            } else {
                printf("Usage: run <binary>\n");
            }
            continue;
        }
        
        /* Unknown command */
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Unknown command: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("'%s'\n", cmd);
    }
}