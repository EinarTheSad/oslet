#include "task.h"
#include "mem/heap.h"
#include "console.h"
#include "timer.h"
#include <stddef.h>
#include "syscall.h"
#include "exec.h"

static task_t *task_list = NULL;
static task_t *current_task = NULL;
static uint32_t next_tid = 0;
static volatile int tasking_enabled = 0;

extern void vga_set_color(uint8_t background, uint8_t foreground);

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
    if (!task_list) {
        task_list = task;
        task->next = task;
    } else {
        task_t *t = task_list;
        while (t->next != task_list) t = t->next;
        t->next = task;
        task->next = task_list;
    }
    __asm__ volatile ("sti");
    
    return task->tid;
}

void task_exit(void) {
    __asm__ volatile ("cli");
    
    if (!current_task) {
        __asm__ volatile ("sti");
        return;
    }
    
    if (current_task == task_list) {
        printf("Cannot exit kernel task\n");
        __asm__ volatile ("sti");
        for (;;) __asm__ volatile ("hlt");
    }
    
    /* Unblock parent if waiting */
    if (current_task->parent_tid) {
        task_t *parent = task_find_by_tid(current_task->parent_tid);
        if (parent && parent->state == TASK_BLOCKED && parent->child_tid == current_task->tid) {
            parent->state = TASK_READY;
        }
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
    
    /* Active wait with HLT - timer IRQ will wake us and check if sleep is done */
    while (current_task->state == TASK_SLEEPING) {
        wakeup_sleeping_tasks();  /* Check if we should wake up */
        if (current_task->state == TASK_SLEEPING) {
            __asm__ volatile ("hlt");  /* Sleep until next interrupt */
        }
    }
}

/* Internal version - assumes interrupts already disabled */
static void wakeup_sleeping_tasks_locked(void) {
    if (!task_list) return;

    uint32_t current_ticks = timer_get_ticks();
    task_t *t = task_list;
    int woken = 0;

    do {
        if (t->state == TASK_SLEEPING) {
            if (current_ticks >= t->sleep_until_ticks) {
                t->state = TASK_READY;
                woken++;
            }
        }
        t = t->next;
    } while (t != task_list);
}

/* Public version - manages interrupt protection */
void wakeup_sleeping_tasks(void) {
    __asm__ volatile ("cli");
    wakeup_sleeping_tasks_locked();
    __asm__ volatile ("sti");
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

            /* Clean up file descriptors owned by this task */
            extern void fd_cleanup_task(uint32_t tid);
            fd_cleanup_task(to_free->tid);

            /* Clean up exec resources if this was a spawned process */
            if (to_free->exec_slot >= 0) {
                exec_cleanup_process(to_free->exec_base, to_free->exec_end, to_free->exec_slot);
            }

            if (to_free->stack) kfree(to_free->stack);
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
    
    if (task_list->state != TASK_TERMINATED) {
        task_list->state = TASK_READY;
        return task_list;
    }
    
    return current_task;
}

void schedule(void) {
    if (!tasking_enabled || !current_task) return;

    __asm__ volatile ("cli");

    wakeup_sleeping_tasks_locked();
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

task_t *task_get_current(void) {
    return current_task;
}

task_t *task_find_by_tid(uint32_t tid) {
    __asm__ volatile ("cli");

    if (!task_list) {
        __asm__ volatile ("sti");
        return NULL;
    }

    task_t *t = task_list;
    task_t *result = NULL;

    do {
        if (t->tid == tid) {
            result = t;
            break;
        }
        t = t->next;
    } while (t != task_list);

    __asm__ volatile ("sti");
    return result;
}

int task_spawn_and_wait(const char *path) {
    if (!path || !current_task) return -1;
    
    /* Load child process */
    exec_image_t image;
    if (exec_load(path, &image) != 0) return -1;
    
    /* Free file_data - it's been copied to process memory */
    if (image.file_data) {
        kfree(image.file_data);
        image.file_data = NULL;
    }
    
    /* Extract filename from path */
    const char *filename = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' || *p == '\\') filename = p + 1;
    }
    
    uint32_t child_tid = task_create((void (*)(void))image.entry_point,
                                      filename,
                                      PRIORITY_NORMAL);
    if (!child_tid) {
        /* Failed - clean up everything */
        int slot = image.file_size >> 24;
        exec_cleanup_process(image.base_addr, image.end_addr, slot);
        return -1;
    }
    
    /* Setup parent-child relationship */
    task_t *child = task_find_by_tid(child_tid);
    if (!child) {
        int slot = image.file_size >> 24;
        exec_cleanup_process(image.base_addr, image.end_addr, slot);
        return -1;
    }
    
    child->parent_tid = current_task->tid;
    current_task->child_tid = child_tid;
    
    /* Store exec info for cleanup on exit */
    child->exec_base = image.base_addr;
    child->exec_end = image.end_addr;
    child->exec_slot = image.file_size >> 24;
    
    /* Block parent and wait for child to finish */
    __asm__ volatile ("cli");
    current_task->state = TASK_BLOCKED;
    __asm__ volatile ("sti");
    
    /* Yield to child */
    task_yield();
    
    /* Child finished, we're back */
    current_task->child_tid = 0;
    
    return 0;
}