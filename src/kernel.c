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

extern void idt_init(void);
extern void pic_remap(void);
extern uint8_t __kernel_end;

static void ipc_sender(void) {
    uint32_t my_tid = sys_getpid();
    char msg[64];
    
    for (int i = 0; i < 5; i++) {
        snprintf(msg, sizeof(msg), "Hello from task %u, iteration %d", my_tid, i);
        
        uint32_t receiver_tid = (my_tid == 1) ? 2 : 1;
        int ret = sys_send_msg(receiver_tid, msg, strlen_simple(msg) + 1);
        
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
    tasking_init();
    syscall_init();
    timer_enable_scheduling();

    __asm__ volatile ("sti");

    vga_set_color(1, 7);
    printf("osLET Development Kernel\n\n");
    vga_set_color(0, 7);

    char line[128];
    for (;;) {
        printf("oslet> ");

        int n = kbd_getline(line, sizeof(line));
        if (n <= 0) {
            __asm__ volatile ("hlt");
            continue;
        }

        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = '\0';

        if (line[0] == '\0')
            continue;

        if (STREQ(line, "help")) {
            printf("Commands:\n");
            printf("  cls       - Clear screen\n");
            printf("  heap      - Show heap stats\n");
            printf("  mem       - Show memory stats\n");
            printf("  ps        - List tasks\n");
            printf("  rtc       - Show current time/date\n");
            printf("  uptime    - Show uptime\n");
            printf("  test      - Run test tasks\n");
            continue;
        }

        if (STREQ(line, "cls")) {
            vga_clear();
            continue;
        }

        if (STREQ(line, "mem")) {
            pmm_print_stats();
            continue;
        }

        if (STREQ(line, "heap")) {
            heap_print_stats();
            continue;
        }

        if (STREQ(line, "uptime")) {
            uint32_t ticks = timer_get_ticks();
            uint32_t seconds = ticks / 100;
            printf("Uptime: %u ticks (%u seconds)\n", ticks, seconds);
            continue;
        }

        if (STREQ(line, "ps")) {
            task_list_print();
            continue;
        }
        
        if (STREQ(line, "rtc")) {
            rtc_print_time();
            continue;
        }
        
        if (STREQ(line, "test")) {
            printf("Creating IPC test tasks...\n");
            uint32_t t1 = task_create(ipc_sender, "sender", PRIORITY_NORMAL);
            uint32_t t2 = task_create(ipc_receiver, "receiver", PRIORITY_NORMAL);
            printf("Created sender (TID %u) and receiver (TID %u)\n", t1, t2);
            continue;
        }

        printf("Unknown command: %s\n", line);
    }
}