#include "shell.h"
#include "console.h"
#include "vga.h"
#include "keyboard.h"
#include "fat32.h"
#include "string.h"
#include "pmm.h"
#include "heap.h"
#include "timer.h"
#include "task.h"
#include "rtc.h"
#include "syscall.h"

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

static char current_path[256];

static void print_prompt(void) {
    fat32_getcwd(current_path, sizeof(current_path));
    vga_set_color(COLOR_PROMPT_BG, COLOR_PROMPT_FG);
    printf("%s", current_path);
    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
    printf("> ");
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
    printf("Kernel 0.3              EinarTheSad 2025\n\n");
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
    
    vga_set_color(COLOR_NORMAL_BG, 15); /* bright white */
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
        vga_set_color(COLOR_SUCCESS_BG, COLOR_SUCCESS_FG);
        printf("OK ");
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
        vga_set_color(COLOR_SUCCESS_BG, COLOR_SUCCESS_FG);
        printf("OK ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Deleted: %s\n", filename);
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot delete '%s'\n", filename);
    }
}

static void cmd_rmdir(const char *dirname) {
    if (fat32_rmdir(dirname) == 0) {
        vga_set_color(COLOR_SUCCESS_BG, COLOR_SUCCESS_FG);
        printf("OK ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Removed directory: %s\n", dirname);
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot remove '%s' (not empty?)\n", dirname);
    }
}

static void cmd_cd(const char *path) {
    if (fat32_chdir(path) == 0) {
        /* Success - prompt will show new path */
    } else {
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Error: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("Cannot access '%s'\n", path);
    }
}

static void cmd_pwd(void) {
    char cwd[256];
    fat32_getcwd(cwd, sizeof(cwd));
    vga_set_color(COLOR_INFO_BG, COLOR_INFO_FG);
    printf("%s\n", cwd);
    vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
}

static void cmd_help(void) {
    vga_set_color(0, 14); /* yellow */
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
    
    printf("  ls [path]            ");
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
    
    printf("  pwd                  ");
    vga_set_color(0, 8);
    printf("Print working directory\n");
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
    
    printf("  test                 ");
    vga_set_color(0, 8);
    printf("Run IPC test tasks\n");
    vga_set_color(0, 7);
    
    printf("  uptime               ");
    vga_set_color(0, 8);
    printf("Show system uptime\n");
    vga_set_color(0, 7);
    
    printf("\n");
}

static void cmd_ls(const char *path) {
    const char *target = (path && path[0]) ? path : ".";
    
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
    for (int i = 0; i < count; i++) {
        if (entries[i].is_directory) {
            vga_set_color(0, COLOR_DIR_FG);
            printf("  %-16s ", entries[i].name);
            vga_set_color(0, 8);
            printf("<DIR>\n");
            vga_set_color(0, 7);
        } else {
            vga_set_color(0, 15); /* bright white */
            printf("  %-16s ", entries[i].name);
            vga_set_color(0, 8);
            printf("%8u bytes\n", entries[i].size);
            vga_set_color(0, 7);
        }
    }
    
    printf("\n");
    vga_set_color(0, 8);
    printf("  %d item(s)\n", count);
    vga_set_color(0, 7);
}

static void ipc_sender(void) {
    uint32_t my_tid = sys_getpid();
    char msg[64];
    
    for (int i = 0; i < 5; i++) {
        snprintf(msg, sizeof(msg), "Message #%d from task %u", i, my_tid);
        
        uint32_t receiver_tid = (my_tid == 1) ? 2 : 1;
        int ret = sys_send_msg(receiver_tid, msg, strlen_s(msg) + 1);
        
        if (ret == 0) {
            sys_write("[SENDER] OK Sent\n");
        } else {
            sys_write("[SENDER] X Failed\n");
        }
        
        sys_sleep(1000);
    }
    
    sys_write("[SENDER] Done\n");
    sys_exit();
}

static void ipc_receiver(void) {
    message_t msg;
    
    sys_write("[RECEIVER] Waiting...\n");
    
    for (int i = 0; i < 5; i++) {
        int ret = sys_recv_msg(&msg);
        
        if (ret == 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[RECEIVER] <- %s\n", msg.data);
            sys_write(buf);
        } else {
            sys_write("[RECEIVER] X Failed\n");
        }
    }
    
    sys_write("[RECEIVER] Done\n");
    sys_exit();
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
        
        if (!strcmp_s(line, "pwd")) {
            cmd_pwd();
            continue;
        }
        
        if (!strcmp_s(line, "test")) {
            vga_set_color(COLOR_INFO_BG, COLOR_INFO_FG);
            printf("Starting IPC test...\n");
            vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
            uint32_t t1 = task_create(ipc_sender, "sender", PRIORITY_NORMAL);
            uint32_t t2 = task_create(ipc_receiver, "receiver", PRIORITY_NORMAL);
            printf("Created TID %u (sender) and TID %u (receiver)\n", t1, t2);
            continue;
        }
        
        /* Parse arguments */
        char *cmd = line;
        char *arg1 = NULL;
        char *arg2 = NULL;
        
        for (int i = 0; line[i]; i++) {
            if (line[i] == ' ') {
                line[i] = '\0';
                arg1 = &line[i+1];
                while (*arg1 == ' ') arg1++;
                
                for (int j = 0; arg1[j]; j++) {
                    if (arg1[j] == ' ' || arg1[j] == '>') {
                        if (arg1[j] == '>') {
                            arg1[j] = '\0';
                            arg2 = &arg1[j+1];
                            while (*arg2 == ' ') arg2++;
                        } else {
                            arg1[j] = '\0';
                            arg2 = &arg1[j+1];
                            while (*arg2 == ' ') arg2++;
                        }
                        break;
                    }
                }
                break;
            }
        }
        
        /* Commands with arguments */
        if (!strcmp_s(cmd, "ls")) {
            cmd_ls(arg1);
            continue;
        }
        
        if (!strcmp_s(cmd, "cat")) {
            if (arg1) {
                cmd_cat(arg1);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("cat <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "echo")) {
            if (arg1 && arg2) {
                cmd_echo(arg1, arg2);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("echo <text> > <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "mkdir")) {
            if (arg1) {
                cmd_mkdir(arg1);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("mkdir <dir>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "rm")) {
            if (arg1) {
                cmd_rm(arg1);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("rm <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "rmdir")) {
            if (arg1) {
                cmd_rmdir(arg1);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("rmdir <dir>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "cd")) {
            if (arg1) {
                cmd_cd(arg1);
            } else {
                vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
                printf("Usage: ");
                vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
                printf("cd <dir>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "mount")) {
            if (arg1 && arg2) {
                uint8_t drive = toupper_s(arg1[0]);
                uint32_t lba = 0;
                
                for (int i = 0; arg2[i]; i++) {
                    if (arg2[i] >= '0' && arg2[i] <= '9') {
                        lba = lba * 10 + (arg2[i] - '0');
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
        
        /* Unknown command */
        vga_set_color(COLOR_ERROR_BG, COLOR_ERROR_FG);
        printf("Unknown command: ");
        vga_set_color(COLOR_NORMAL_BG, COLOR_NORMAL_FG);
        printf("'%s'\n", cmd);
    }
}