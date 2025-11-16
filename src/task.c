#include "task.h"
#include "mem/heap.h"
#include "console.h"
#include "timer.h"
#include <stddef.h>
#include "syscall.h"
#include "gdt.h"

static task_t *task_list = NULL;
static task_t *current_task = NULL;
static uint32_t next_tid = 0;
static volatile int tasking_enabled = 0;

extern void vga_set_color(uint8_t background, uint8_t foreground);
extern void enter_usermode(uint32_t entry, uint32_t user_stack);

static const uint32_t QUANTUM_HIGH   = 10;
static const uint32_t QUANTUM_NORMAL = 5;
static const uint32_t QUANTUM_LOW    = 2;
static const uint32_t QUANTUM_IDLE   = 1;

void tasking_init(void) {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) {
        vga_set_color(0,12);
        printf("FAILED to allocate kernel task\n");
        vga_set_color(0,7);
        return;
    }
    
    memset_s(current_task, 0, sizeof(task_t));
    current_task->msg_queue.head = 0;
    current_task->msg_queue.tail = 0;
    current_task->msg_queue.count = 0;
    current_task->tid = next_tid++;
    strcpy_s(current_task->name, "kernel", sizeof(current_task->name));
    current_task->state = TASK_RUNNING;
    current_task->priority = PRIORITY_HIGH;
    current_task->sleep_until_ticks = 0;
    current_task->quantum_remaining = QUANTUM_HIGH;
    current_task->stack = NULL;
    current_task->kernel_stack = NULL;
    current_task->user_mode = 0;
    current_task->next = current_task;
    
    task_list = current_task;
    tasking_enabled = 1;
}

__attribute__((naked)) void task_trampoline(void) {
    __asm__ volatile(
        "pop %eax\n\t"
        "call *%eax\n\t"
        "call task_exit\n\t"
        "hlt\n\t"
        "jmp .-2\n\t"
    );
}

/* User mode trampoline - switches to ring 3 */
__attribute__((naked)) void user_task_trampoline(void) {
    __asm__ volatile(
        "pop %eax\n\t"        /* entry point */
        "pop %ebx\n\t"        /* user stack */
        "call enter_usermode\n\t"
        "hlt\n\t"
        "jmp .-2\n\t"
    );
}

uint32_t task_create(void (*entry)(void), const char *name, task_priority_t priority) {
    if (!tasking_enabled) return 0;
    
    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return 0;
    memset_s(task, 0, sizeof(task_t));

    task->stack = kmalloc(TASK_STACK_SIZE);
    if (!task->stack) { kfree(task); return 0; }
    memset_s(task->stack, 0, TASK_STACK_SIZE);

    task->msg_queue.head = 0;
    task->msg_queue.tail = 0;
    task->msg_queue.count = 0;
    
    task->tid = next_tid++;
    strcpy_s(task->name, name ? name : "unnamed", sizeof(task->name));
    task->state = TASK_READY;
    task->priority = priority;
    task->sleep_until_ticks = 0;
    task->user_mode = 0;
    task->kernel_stack = NULL;
    
    switch (priority) {
        case PRIORITY_HIGH:   task->quantum_remaining = QUANTUM_HIGH; break;
        case PRIORITY_NORMAL: task->quantum_remaining = QUANTUM_NORMAL; break;
        case PRIORITY_LOW:    task->quantum_remaining = QUANTUM_LOW; break;
        case PRIORITY_IDLE:   task->quantum_remaining = QUANTUM_IDLE; break;
    }
    
    uint32_t *sp = (uint32_t *)((uint8_t*)task->stack + TASK_STACK_SIZE);
    
    *--sp = (uint32_t)entry;
    *--sp = (uint32_t)task_trampoline;
    *--sp = 0;
    
    task->esp = (uint32_t)sp;
    
    __asm__ volatile ("cli");
    task_t *t = task_list;
    while (t->next != task_list) t = t->next;
    t->next = task;
    task->next = task_list;
    __asm__ volatile ("sti");
    
    return task->tid;
}

uint32_t task_create_user(void (*entry)(void), const char *name, 
                          task_priority_t priority, uint32_t user_stack) {
    if (!tasking_enabled) return 0;
    
    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return 0;
    memset_s(task, 0, sizeof(task_t));

    /* Kernel stack for syscalls and interrupts */
    task->kernel_stack = kmalloc(TASK_STACK_SIZE);
    if (!task->kernel_stack) { kfree(task); return 0; }
    memset_s(task->kernel_stack, 0, TASK_STACK_SIZE);

    /* User stack is managed by exec/paging */
    task->stack = NULL;

    task->msg_queue.head = 0;
    task->msg_queue.tail = 0;
    task->msg_queue.count = 0;
    
    task->tid = next_tid++;
    strcpy_s(task->name, name ? name : "userapp", sizeof(task->name));
    task->state = TASK_READY;
    task->priority = priority;
    task->sleep_until_ticks = 0;
    task->user_mode = 1;
    task->user_stack = user_stack;
    
    switch (priority) {
        case PRIORITY_HIGH:   task->quantum_remaining = QUANTUM_HIGH; break;
        case PRIORITY_NORMAL: task->quantum_remaining = QUANTUM_NORMAL; break;
        case PRIORITY_LOW:    task->quantum_remaining = QUANTUM_LOW; break;
        case PRIORITY_IDLE:   task->quantum_remaining = QUANTUM_IDLE; break;
    }
    
    /* Setup kernel stack to switch to user mode */
    uint32_t *sp = (uint32_t *)((uint8_t*)task->kernel_stack + TASK_STACK_SIZE);
    
    *--sp = 0;                    /* Alignment */
    *--sp = user_stack;           /* User stack pointer */
    *--sp = (uint32_t)entry;      /* Entry point */
    *--sp = (uint32_t)user_task_trampoline;
    
    task->esp = (uint32_t)sp;
    
    __asm__ volatile ("cli");
    task_t *t = task_list;
    while (t->next != task_list) t = t->next;
    t->next = task;
    task->next = task_list;
    __asm__ volatile ("sti");
    
    return task->tid;
}

void task_exit(void) {
    __asm__ volatile ("cli");
    
    if (!current_task || current_task == task_list) {
        printf("Cannot exit kernel task\n");
        __asm__ volatile ("sti");
        return;
    }
    
    current_task->state = TASK_TERMINATED;
    
    __asm__ volatile ("sti");
    task_yield();
    for (;;) __asm__ volatile ("hlt");
}

void task_sleep(uint32_t milliseconds) {
    if (!tasking_enabled || !current_task) return;
    
    __asm__ volatile ("cli");
    
    uint32_t current_ticks = timer_get_ticks();
    uint32_t sleep_ticks = (milliseconds * 100) / 1000;
    
    current_task->state = TASK_SLEEPING;
    current_task->sleep_until_ticks = current_ticks + sleep_ticks;
    
    __asm__ volatile ("sti");
    
    while (current_task->state == TASK_SLEEPING) {
        __asm__ volatile ("hlt");
    }
}

static void wakeup_sleeping_tasks(void) {
    uint32_t current_ticks = timer_get_ticks();
    task_t *t = task_list;
    
    do {
        if (t->state == TASK_SLEEPING && current_ticks >= t->sleep_until_ticks) {
            t->state = TASK_READY;
        }
        t = t->next;
    } while (t != task_list);
}

static void cleanup_terminated_tasks(void) {
    if (!task_list || !current_task) return;
    
    task_t *prev = task_list;
    task_t *curr = task_list->next;
    
    while (curr != task_list) {
        if (curr->state == TASK_TERMINATED && curr != current_task) {
            task_t *to_free = curr;
            prev->next = curr->next;
            curr = curr->next;
            
            if (to_free->stack) kfree(to_free->stack);
            if (to_free->kernel_stack) kfree(to_free->kernel_stack);
            kfree(to_free);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

static task_t *pick_next_task(void) {
    static uint32_t schedule_counter = 0;
    schedule_counter++;
    
    task_priority_t try_priority;
    uint32_t mod = schedule_counter % 16;
   
    if (mod < 9) {
        try_priority = PRIORITY_HIGH;
    } else if (mod < 14) {
        try_priority = PRIORITY_NORMAL;
    } else if (mod < 16) {
        try_priority = PRIORITY_LOW;
    } else {
        try_priority = PRIORITY_IDLE;
    }
    
    task_t *t = current_task->next;
    task_t *start = current_task;
    
    do {
        if (t->state == TASK_READY && t->priority == try_priority) {
            return t;
        }
        t = t->next;
    } while (t != start);
    
    t = current_task->next;
    do {
        if (t->state == TASK_READY) {
            return t;
        }
        t = t->next;
    } while (t != start);
    
    if (current_task->state == TASK_RUNNING) {
        return current_task;
    }
    
    return task_list;
}

void schedule(void) {
    if (!tasking_enabled || !current_task) return;

    __asm__ volatile ("cli");
    
    wakeup_sleeping_tasks();
    cleanup_terminated_tasks();

    task_t *next = pick_next_task();
    
    if (next == current_task) {
        __asm__ volatile ("sti");
        return;
    }
    
    task_t *prev = current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    
    current_task = next;
    current_task->state = TASK_RUNNING;
    
    /* Update TSS with kernel stack for user tasks */
    if (current_task->user_mode && current_task->kernel_stack) {
        uint32_t kernel_esp = (uint32_t)current_task->kernel_stack + TASK_STACK_SIZE;
        tss_set_kernel_stack(kernel_esp);
    }

    switch (current_task->priority) {
        case PRIORITY_HIGH:   current_task->quantum_remaining = QUANTUM_HIGH; break;
        case PRIORITY_NORMAL: current_task->quantum_remaining = QUANTUM_NORMAL; break;
        case PRIORITY_LOW:    current_task->quantum_remaining = QUANTUM_LOW; break;
        case PRIORITY_IDLE:   current_task->quantum_remaining = QUANTUM_IDLE; break;
    }
    
    __asm__ volatile (
        "movl %0, %%esp\n\t"
        "popl %%ebp\n\t"
        "sti\n\t"
        "ret"
        :: "r"(current_task->esp)
    );
}

void task_tick(void) {
    if (!tasking_enabled || !current_task) return;
    if (current_task->state != TASK_RUNNING) return;
    
    if (current_task->quantum_remaining > 0) {
        current_task->quantum_remaining--;
    }
}

void task_yield(void) {
    if (!tasking_enabled || !current_task) return;
    
    __asm__ volatile ("cli");
    
    uint32_t esp_save;
    __asm__ volatile (
        "movl %%ebp, %%eax\n\t"
        "pushl %%eax\n\t"
        "movl %%esp, %0"
        : "=r"(esp_save)
        :: "eax", "memory"
    );
    
    current_task->esp = esp_save;
    
    __asm__ volatile ("sti");

    schedule();
}

void task_list_print(void) { 
    if (!task_list) {
        printf("Task list is NULL\n");
        return;
    }
    
    vga_set_color(0, 11);
    printf("TID  NAME              STATE        PRIORITY   MODE\n");
    vga_set_color(0, 8);
    printf("---  ----------------  -----------  ---------  ------\n");
    vga_set_color(0, 7);
    
    task_t *t = task_list;
    int count = 0;
    do {
        const char *state_str = "UNKNOWN";
        uint8_t state_color = 7;
        switch (t->state) {
            case TASK_READY:
                state_str = "READY";
                state_color = 14;
                break;
            case TASK_RUNNING:
                state_str = "RUNNING";
                state_color = 10;
                break;
            case TASK_SLEEPING:
                state_str = "SLEEPING";
                state_color = 11;
                break;
            case TASK_BLOCKED:
                state_str = "BLOCKED";
                state_color = 12;
                break;
            case TASK_TERMINATED:
                state_str = "TERMINATED";
                state_color = 8;
                break;
        }
        
        const char *prio_str = "UNKNOWN";
        switch (t->priority) {
            case PRIORITY_HIGH:   prio_str = "HIGH"; break;
            case PRIORITY_NORMAL: prio_str = "NORMAL"; break;
            case PRIORITY_LOW:    prio_str = "LOW"; break;
            case PRIORITY_IDLE:   prio_str = "IDLE"; break;
        }
        
        const char *mode_str = t->user_mode ? "USER" : "KERNEL";
        
        vga_set_color(0, 15);
        printf("%-4u ", t->tid);
        vga_set_color(0, 7);
        printf("%-16s  ", t->name);
        
        vga_set_color(0, state_color);
        printf("%-11s  ", state_str);
        
        vga_set_color(0, 8);
        printf("%-9s  %s\n", prio_str, mode_str);
        vga_set_color(0, 7);
        
        t = t->next;
        count++;
        
        if (count > 20) {
            vga_set_color(0, 12);
            printf("ERROR: Loop detected!\n");
            vga_set_color(0, 7);
            break;
        }
    } while (t != task_list);
}

task_t *task_get_current(void) {
    return current_task;
}

task_t *task_find_by_tid(uint32_t tid) {
    if (!task_list) return NULL;
    
    task_t *t = task_list;
    do {
        if (t->tid == tid) return t;
        t = t->next;
    } while (t != task_list);
    
    return NULL;
}