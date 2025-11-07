#include "task.h"
#include "heap.h"
#include "console.h"
#include "timer.h"
#include <stddef.h>

static task_t *task_list = NULL;
static task_t *current_task = NULL;
static uint32_t next_tid = 0;
static volatile int tasking_enabled = 0;

extern void vga_set_color(uint8_t background, uint8_t foreground);

/* Quantum (CPU time) for each priority */
static const uint32_t QUANTUM_HIGH   = 10;  /* 100ms at 100Hz */
static const uint32_t QUANTUM_NORMAL = 5;   /* 50ms */
static const uint32_t QUANTUM_LOW    = 2;   /* 20ms */
static const uint32_t QUANTUM_IDLE   = 1;   /* 10ms */

static void memset_custom(void *dst, int val, size_t n) {
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)val;
}

static void strlen_copy(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

void tasking_init(void) {
    printf("tasking_init() starting...\n");
    
    current_task = (task_t*)kmalloc(sizeof(task_t));
    if (!current_task) {
        vga_set_color(12,15);
        printf("FAILED to allocate kernel task\n");
        vga_set_color(0,7);
        return;
    }
    
    memset_custom(&current_task->regs, 0, sizeof(registers_t));
    current_task->tid = next_tid++;
    strlen_copy(current_task->name, "kernel", sizeof(current_task->name));
    current_task->state = TASK_RUNNING;
    current_task->priority = PRIORITY_HIGH;
    current_task->sleep_until_ticks = 0;
    current_task->quantum_remaining = QUANTUM_HIGH;
    current_task->stack = NULL;
    current_task->next = current_task;
    
    task_list = current_task;
    tasking_enabled = 1;
    
    printf("Multitasking initialized\n");
}

static void task_trampoline(void (*entry)(void)) {
    __asm__ volatile ("sti");
    entry();
    task_exit();
}

uint32_t task_create(void (*entry)(void), const char *name, task_priority_t priority) {
    if (!tasking_enabled) return 0;
    
    task_t *task = (task_t*)kmalloc(sizeof(task_t));
    if (!task) return 0;
    
    task->stack = kmalloc(TASK_STACK_SIZE);
    if (!task->stack) {
        kfree(task);
        return 0;
    }
    
    memset_custom(task->stack, 0, TASK_STACK_SIZE);
    memset_custom(&task->regs, 0, sizeof(registers_t));
    
    task->tid = next_tid++;
    strlen_copy(task->name, name ? name : "unnamed", sizeof(task->name));
    task->state = TASK_READY;
    task->priority = priority;
    task->sleep_until_ticks = 0;
    
    switch (priority) {
        case PRIORITY_HIGH:   task->quantum_remaining = QUANTUM_HIGH; break;
        case PRIORITY_NORMAL: task->quantum_remaining = QUANTUM_NORMAL; break;
        case PRIORITY_LOW:    task->quantum_remaining = QUANTUM_LOW; break;
        case PRIORITY_IDLE:   task->quantum_remaining = QUANTUM_IDLE; break;
    }
    
    /* Setup stack */   
    uint32_t *sp = (uint32_t *)((uint8_t*)task->stack + TASK_STACK_SIZE);

    /* cdecl stack: top -> [return addr][arg0] */
    *--sp = (uint32_t)task_exit;
    *--sp = (uint32_t)entry;

    task->regs.esp = (uint32_t)sp;
    task->regs.eip = (uint32_t)task_trampoline;
    
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
    printf("Task '%s' (TID %u) exited\n", 
           current_task->name, current_task->tid);
    
    __asm__ volatile ("sti");
    
    for (;;) __asm__ volatile ("hlt");
}

void task_sleep(uint32_t milliseconds) {
    if (!tasking_enabled || !current_task) return;
    
    __asm__ volatile ("cli");
    
    uint32_t current_ticks = timer_get_ticks();
    uint32_t sleep_ticks = (milliseconds * 100) / 1000;  /* 100 Hz timer */
    
    current_task->state = TASK_SLEEPING;
    current_task->sleep_until_ticks = current_ticks + sleep_ticks;
    
    __asm__ volatile ("sti");
    
    task_yield();
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
            kfree(to_free);
        } else {
            prev = curr;
            curr = curr->next;
        }
    }
}

/* Weighted round-robin algorithm (thanks, Claude) */
static task_t *pick_next_task(void) {
    static uint32_t schedule_counter = 0;
    schedule_counter++;
    
    task_priority_t try_priority;
    uint32_t mod = schedule_counter % 10;
   
    if (mod < 9) {
        try_priority = PRIORITY_HIGH;
    } else if (mod < 9 + 5) {
        try_priority = PRIORITY_NORMAL;
    } else if (mod < 9 + 5 + 2) {
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
    
    wakeup_sleeping_tasks();
    cleanup_terminated_tasks();
    
    task_t *next = pick_next_task();
    
    if (next == current_task) return;
    
    task_t *prev = current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    
    current_task = next;
    current_task->state = TASK_RUNNING;
    
    /* Reset quantum for a new task */
    switch (current_task->priority) {
        case PRIORITY_HIGH:   current_task->quantum_remaining = QUANTUM_HIGH; break;
        case PRIORITY_NORMAL: current_task->quantum_remaining = QUANTUM_NORMAL; break;
        case PRIORITY_LOW:    current_task->quantum_remaining = QUANTUM_LOW; break;
        case PRIORITY_IDLE:   current_task->quantum_remaining = QUANTUM_IDLE; break;
    }
}

void task_tick(void) {
    if (!tasking_enabled || !current_task) return;
    if (current_task->state != TASK_RUNNING) return;
    
    if (current_task->quantum_remaining > 0) {
        current_task->quantum_remaining--;
    }
    
    if (current_task->quantum_remaining == 0) {
        schedule();
    }
}

uint32_t switch_task(uint32_t esp) {
    if (!tasking_enabled || !current_task) return esp;
    
    task_t *prev = current_task;
    
    if (prev->state != TASK_TERMINATED) {
        prev->regs.esp = esp;
    }
    
    schedule();
    
    return current_task->regs.esp;
}

void task_yield(void) {
    schedule();
}

void task_list_print(void) { 
    if (!task_list) {
        printf("Task list is NULL\n");
        return;
    }
    
    printf("TID  NAME              STATE        PRIORITY   QUANTUM\n");
    printf("---  ----------------  -----------  ---------  -------\n");
    
    task_t *t = task_list;
    int count = 0;
    do {
        const char *state_str = "UNKNOWN";
        switch (t->state) {
            case TASK_READY:      state_str = "READY"; break;
            case TASK_RUNNING:    state_str = "RUNNING"; break;
            case TASK_SLEEPING:   state_str = "SLEEPING"; break;
            case TASK_BLOCKED:    state_str = "BLOCKED"; break;
            case TASK_TERMINATED: state_str = "TERMINATED"; break;
        }
        
        const char *prio_str = "UNKNOWN";
        switch (t->priority) {
            case PRIORITY_HIGH:   prio_str = "HIGH"; break;
            case PRIORITY_NORMAL: prio_str = "NORMAL"; break;
            case PRIORITY_LOW:    prio_str = "LOW"; break;
            case PRIORITY_IDLE:   prio_str = "IDLE"; break;
        }
        
        printf("%-4u %-16s %-11s  %-9s  %u\n", 
               t->tid, t->name, state_str, prio_str, t->quantum_remaining);
        
        t = t->next;
        count++;
        
        if (count > 20) {
            printf("ERROR: Loop detected!\n");
            break;
        }
    } while (t != task_list);
}

task_t *task_get_current(void) {
    return current_task;
}