#pragma once
#include <stdint.h>
#include <stddef.h>
#include "syscall.h"

#define TASK_STACK_SIZE 65536

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
    task_state_t state;
    task_priority_t priority;
    uint32_t sleep_until_ticks;
    uint32_t quantum_remaining;
    msg_queue_t msg_queue;
    uint32_t parent_tid;
    uint32_t child_tid;
    int exit_code;
    uint32_t exec_base;
    uint32_t exec_end;
    int exec_slot;
    struct task *next;
} task_t;

void tasking_init(void);
uint32_t task_create(void (*entry)(void), const char *name, task_priority_t priority);
void task_yield(void);
void task_sleep(uint32_t milliseconds);
void task_exit(void);
void task_list_print(void);
task_t *task_get_current(void);
int task_spawn_and_wait(const char *path);

void schedule(void);
void task_tick(void);

task_t *task_find_by_tid(uint32_t tid);