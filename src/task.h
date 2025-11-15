#pragma once
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"

#define TASK_STACK_SIZE 8192

typedef enum {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_BLOCKED,
    TASK_TERMINATED
} task_state_t;

typedef enum {
    PRIORITY_HIGH   = 0,
    PRIORITY_NORMAL = 1,
    PRIORITY_LOW    = 2,
    PRIORITY_IDLE   = 3
} task_priority_t;

typedef struct task {
    uint32_t tid;
    char name[32];
    uint32_t esp;
    void *stack;
    void *kernel_stack;
    uint32_t user_stack;
    uint8_t user_mode;
    task_state_t state;
    task_priority_t priority;
    uint32_t sleep_until_ticks;
    uint32_t quantum_remaining;
    msg_queue_t msg_queue;
    struct task *next;
} task_t;

void tasking_init(void);
uint32_t task_create(void (*entry)(void), const char *name, task_priority_t priority);
uint32_t task_create_user(void (*entry)(void), const char *name, 
                          task_priority_t priority, uint32_t user_stack);
void task_yield(void);
void task_sleep(uint32_t milliseconds);
void task_exit(void);
void task_list_print(void);
task_t *task_get_current(void);

void schedule(void);
void task_tick(void);

task_t *task_find_by_tid(uint32_t tid);