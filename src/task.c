#include "task.h"
#include "heap.h"
#include "console.h"
#include <stddef.h>

static task_t *task_list = NULL;
static task_t *current_task = NULL;
static uint32_t next_tid = 0;
static volatile int tasking_enabled = 0;

extern void vga_set_color(uint8_t background, uint8_t foreground);

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
    printf("kmalloc returned %p for kernel task\n", current_task);
    
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
    current_task->stack = NULL;
    current_task->next = current_task;
    
    printf("kernel task setup: tid=%u, name=%s, next=%p\n",
           current_task->tid, current_task->name, current_task->next);
    
    task_list = current_task;
    tasking_enabled = 1;
    
    printf("task_list=%p, tasking_enabled=%d\n", task_list, tasking_enabled);
    printf("Multitasking initialized\n");
}

/* Forward declaration of wrapper */
static void task_wrapper(void);

uint32_t task_create(void (*entry)(void), const char *name) {
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
    
    /* Setup initial stack frame */
    uint32_t *stack_top = (uint32_t*)((char*)task->stack + TASK_STACK_SIZE);
    
    /* Push initial context for task_wrapper */
    stack_top--;
    *stack_top = 0x202;          /* EFLAGS (interrupts enabled) */
    stack_top--;
    *stack_top = 0;              /* EDI */
    stack_top--;
    *stack_top = 0;              /* ESI */
    stack_top--;
    *stack_top = 0;              /* EBP */
    stack_top--;
    *stack_top = 0;              /* EDX */
    stack_top--;
    *stack_top = 0;              /* ECX */
    stack_top--;
    *stack_top = 0;              /* EBX */
    stack_top--;
    *stack_top = (uint32_t)entry; /* EAX = function pointer */
    
    task->regs.esp = (uint32_t)stack_top;
    task->regs.eip = (uint32_t)task_wrapper;
    
    __asm__ volatile ("cli");
    task_t *t = task_list;
    while (t->next != task_list) t = t->next;
    t->next = task;
    task->next = task_list;
    __asm__ volatile ("sti");
    
    return task->tid;
}

/* Wrapper executed when task starts - EAX contains entry point */
static void task_wrapper(void) {
    void (*func)(void);
    
    __asm__ volatile (
        "movl %%eax, %0"
        : "=r"(func)
    );
    
    __asm__ volatile ("sti");
    
    if (func) func();
    
    task_exit();
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
    
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void schedule(void) {
    if (!tasking_enabled || !current_task) return;
    
    task_t *next = current_task->next;
    task_t *start = current_task;
    
    /* Find next runnable task */
    do {
        if (next->state == TASK_READY) {
            break;
        }
        
        if (next->state == TASK_RUNNING && next != current_task) {
            break;
        }
        
        next = next->next;
    } while (next != start);
    
    if (next == start && current_task->state == TASK_TERMINATED) {
        next = task_list;
    }
    
    if (next == current_task) return;
    
    task_t *prev = current_task;
    if (prev->state == TASK_RUNNING) {
        prev->state = TASK_READY;
    }
    
    current_task = next;
    current_task->state = TASK_RUNNING;
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
    __asm__ volatile (
        "int $0x80"
    );
}

void task_list_print(void) { 
    if (!task_list) {
        printf("Something is wrong with task_list, it's NULL\n");
        return;
    }
    
    printf("Task list:\n");
    
    task_t *t = task_list;
    int count = 0;
    do {
        const char *state_str = "UNKNOWN";
        switch (t->state) {
            case TASK_READY:      state_str = "READY"; break;
            case TASK_RUNNING:    state_str = "RUNNING"; break;
            case TASK_BLOCKED:    state_str = "BLOCKED"; break;
            case TASK_TERMINATED: state_str = "TERMINATED"; break;
        }
        
        printf("#%u  %s  (%s)\n", t->tid, t->name, state_str);
        
        t = t->next;
        count++;
        
        if (count > 20) {
            printf("ERROR: Infinite loop detected!\n");
            break;
        }
    } while (t != task_list);
}

task_t *task_get_current(void) {
    return current_task;
}