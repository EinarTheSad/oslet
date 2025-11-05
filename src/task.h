#pragma once
#include <stdint.h>
#include <stddef.h>

#define TASK_STACK_SIZE 8192

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef struct {
    uint32_t eax, ebx, ecx, edx;
    uint32_t esi, edi, ebp;
    uint32_t esp, eip;
    uint32_t eflags;
} registers_t;

typedef struct task {
    uint32_t tid;
    char name[32];
    registers_t regs;
    void *stack;
    task_state_t state;
    struct task *next;
} task_t;

void tasking_init(void);
uint32_t task_create(void (*entry)(void), const char *name);
void task_yield(void);
void task_exit(void);
void task_list_print(void);
task_t *task_get_current(void);

void schedule(void);
uint32_t switch_task(uint32_t esp);