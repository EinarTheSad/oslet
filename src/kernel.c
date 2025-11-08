#include "console.h"
#include "vga.h"
#include "keyboard.h"
#include "early_alloc.h"
#include "io.h"
#include "pmm.h"
#include "paging.h"
#include "heap.h"
#include "timer.h"
#include "task.h"
#include "rtc.h"
#include "syscall.h"
#include "ata.h"
#include "fat32.h"
#include "string.h"

extern void idt_init(void);
extern void pic_remap(void);
extern uint8_t __kernel_end;

static void cmd_cat(const char *filename) {
    fat32_file_t *f = fat32_open(filename, "r");
    if (!f) {
        printf("Cannot open file: %s\n", filename);
        return;
    }
    
    char buffer[513];
    int n;
    
    while ((n = fat32_read(f, buffer, 512)) > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }
    
    fat32_close(f);
    printf("\n");
}

static void cmd_echo(const char *text, const char *filename) {
    fat32_file_t *f = fat32_open(filename, "w");
    if (!f) {
        printf("Cannot create file: %s\n", filename);
        return;
    }
    
    fat32_write(f, text, strlen_s(text));
    fat32_close(f);
    printf("Written to %s\n", filename);
}

static void cmd_mkdir(const char *dirname) {
    if (fat32_mkdir(dirname) == 0) {
        printf("Directory created: %s\n", dirname);
    } else {
        printf("Failed to create directory: %s\n", dirname);
    }
}

static void cmd_rm(const char *filename) {
    if (fat32_unlink(filename) == 0) {
        printf("Deleted: %s\n", filename);
    } else {
        printf("Failed to delete: %s\n", filename);
    }
}

static void cmd_rmdir(const char *dirname) {
    if (fat32_rmdir(dirname) == 0) {
        printf("Directory removed: %s\n", dirname);
    } else {
        printf("Failed to remove directory (not empty?): %s\n", dirname);
    }
}

static void cmd_cd(const char *path) {
    if (fat32_chdir(path) == 0) {
        char cwd[256];
        fat32_getcwd(cwd, sizeof(cwd));
        printf("Changed to: %s\n", cwd);
    } else {
        printf("Failed to change directory: %s\n", path);
    }
}

static void cmd_pwd(void) {
    char cwd[256];
    fat32_getcwd(cwd, sizeof(cwd));
    printf("%s\n", cwd);
}

static void ipc_sender(void) {
    uint32_t my_tid = sys_getpid();
    char msg[64];
    
    for (int i = 0; i < 5; i++) {
        snprintf(msg, sizeof(msg), "Hello from task %u, iteration %d", my_tid, i);
        
        uint32_t receiver_tid = (my_tid == 1) ? 2 : 1;
        int ret = sys_send_msg(receiver_tid, msg, strlen_s(msg) + 1);
        
        if (ret == 0) {
            sys_write("[SENDER] Message sent\n");
        } else {
            sys_write("[SENDER] Send failed\n");
        }
        
        sys_sleep(1000);
    }
    
    sys_write("[SENDER] Exiting\n");
    sys_exit();
}

static void ipc_receiver(void) {
    message_t msg;
    
    sys_write("[RECEIVER] Waiting for messages...\n");
    
    for (int i = 0; i < 5; i++) {
        int ret = sys_recv_msg(&msg);
        
        if (ret == 0) {
            char buf[256];
            snprintf(buf, sizeof(buf), "[RECEIVER] Got message from TID %u: %s\n", 
                    msg.from_tid, msg.data);
            sys_write(buf);
        } else {
            sys_write("[RECEIVER] Recv failed\n");
        }
    }
    
    sys_write("[RECEIVER] Exiting\n");
    sys_exit();
}

void kmain(void) {
    vga_use_as_console();
    vga_clear();
    idt_init();
    pic_remap();
    keyboard_init();
    timer_init(100);

    mm_early_init((uintptr_t)&__kernel_end);
    pmm_init_from_multiboot((uint32_t)multiboot_info_ptr);

    __asm__ volatile ("cli");

    uintptr_t kernel_end = (uintptr_t)&__kernel_end;
    uintptr_t map_upto = (kernel_end + 0xFFF) & ~((uintptr_t)0xFFF);
    map_upto += 16 * 1024 * 1024;

    if (paging_identity_enable(map_upto) != 0) {
        vga_set_color(12,15);
        printf("FAILED to enable memory paging\n");
        for (;;) __asm__ volatile ("hlt");
    }
    
    pmm_identity_map_bitmap();
    heap_init();
    rtc_init();

    ata_init();
    fat32_init();
    
    tasking_init();
    syscall_init();
    timer_enable_scheduling();

    __asm__ volatile ("sti");

    vga_clear();
    vga_set_color(1, 7);
    printf("osLET Development Kernel");
    vga_set_color(0, 7);
    printf("                                       EinarTheSad, 2025");

    char line[128];
    for (;;) {
        printf("\noslet> ");

        int n = kbd_getline(line, sizeof(line));
        if (n <= 0) {
            __asm__ volatile ("hlt");
            continue;
        }

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (line[0] == '\0')
            continue;

        if (!strcmp_s(line, "help")) {
            printf("Commands:\n");
            printf("cat <file>           - Display file contents\n");
            printf("cd <dir>             - Change directory\n");
            printf("cls                  - Clear screen\n");
            printf("echo <text> > <file> - Write text to file\n");
            printf("heap                 - Show heap stats\n");
            printf("ls [path]            - List directory\n");
            printf("mem                  - Show memory stats\n");
            printf("mkdir <dir>          - Create directory\n");
            printf("mount <drive> <lba>  - Mount FAT32 drive\n");
            printf("ps                   - List tasks\n");
            printf("pwd                  - Print working directory\n");
            printf("rm <file>            - Delete file\n");
            printf("rmdir <dir>          - Remove empty directory\n");
            printf("rtc                  - Show current time/date\n");
            printf("test                 - Run test tasks\n");
            printf("uptime               - Show uptime\n");
            continue;
        }

        if (!strcmp_s(line, "cls")) {
            vga_clear();
            continue;
        }

        if (!strcmp_s(line, "mem")) {
            pmm_print_stats();
            continue;
        }

        if (!strcmp_s(line, "heap")) {
            heap_print_stats();
            continue;
        }

        if (!strcmp_s(line, "uptime")) {
            uint32_t ticks = timer_get_ticks();
            uint32_t seconds = ticks / 100;
            printf("Uptime: %u ticks (%u seconds)\n", ticks, seconds);
            continue;
        }

        if (!strcmp_s(line, "ps")) {
            task_list_print();
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
            printf("Creating IPC test tasks...\n");
            uint32_t t1 = task_create(ipc_sender, "sender", PRIORITY_NORMAL);
            uint32_t t2 = task_create(ipc_receiver, "receiver", PRIORITY_NORMAL);
            printf("Created sender (TID %u) and receiver (TID %u)\n", t1, t2);
            continue;
        }

        /* Commands with arguments */
        char *cmd = line;
        char *arg1 = NULL;
        char *arg2 = NULL;
        
        /* find first arg */
        for (int i = 0; line[i]; i++) {
            if (line[i] == ' ') {
                line[i] = '\0';
                arg1 = &line[i+1];
                
                /* skip spaces */
                while (*arg1 == ' ') arg1++;
                
                /* find next argument (or >) */
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
        
        if (!strcmp_s(cmd, "ls")) {
            const char *path = arg1 ? arg1 : ".";
            
            fat32_dirent_t entries[64];
            int count = fat32_list_dir(path, entries, 64);
            
            if (count < 0) {
                printf("Failed to list directory: %s\n", path);
            } else if (count == 0) {
                printf("Empty directory\n");
            } else {
                printf("\n");
                for (int i = 0; i < count; i++) {
                    if (entries[i].is_directory) {
                        printf("%-16s <DIR>\n", entries[i].name);
                    } else {
                        printf("%-16s %8u bytes\n", entries[i].name, entries[i].size);
                    }
                }
                printf("\n%d item(s)\n", count);
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "cat")) {
            if (arg1) {
                cmd_cat(arg1);
            } else {
                printf("Usage: cat <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "echo")) {
            if (arg1 && arg2) {
                cmd_echo(arg1, arg2);
            } else {
                printf("Usage: echo <text> > <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "mkdir")) {
            if (arg1) {
                cmd_mkdir(arg1);
            } else {
                printf("Usage: mkdir <dir>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "rm")) {
            if (arg1) {
                cmd_rm(arg1);
            } else {
                printf("Usage: rm <file>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "rmdir")) {
            if (arg1) {
                cmd_rmdir(arg1);
            } else {
                printf("Usage: rmdir <dir>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "cd")) {
            if (arg1) {
                cmd_cd(arg1);
            } else {
                printf("Usage: cd <dir>\n");
            }
            continue;
        }
        
        if (!strcmp_s(cmd, "mount")) {
            if (arg1 && arg2) {
                uint8_t drive = toupper_s(arg1[0]);
                uint32_t lba = 0;
                
                /* number parser */
                for (int i = 0; arg2[i]; i++) {
                    if (arg2[i] >= '0' && arg2[i] <= '9') {
                        lba = lba * 10 + (arg2[i] - '0');
                    }
                }
                
                if (fat32_mount_drive(drive, lba) == 0) {
                    printf("Mounted %c: at LBA %u\n", drive, lba);
                } else {
                    printf("Failed to mount %c:\n", drive);
                }
            } else {
                printf("Usage: mount <drive> <lba>\n");
            }
            continue;
        }

        printf("Unknown command: %s\n", cmd);
        printf("Type 'help' for list of commands\n");
    }
}