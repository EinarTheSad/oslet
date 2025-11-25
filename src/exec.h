#pragma once
#include <stdint.h>
#include <stddef.h>

#define EXEC_LOAD_ADDR   0x200000  /* 2MB  - user programs load here */
#define EXEC_USER_STACK  0x400000  /* 4MB  - user stack top */
#define EXEC_STACK_SIZE  65536     /* 64kB - user stack size */

typedef struct {
    uint32_t entry_point;
    uint32_t size;
    uint32_t user_stack;
    void *memory;
} exec_image_t;

int exec_load(const char *path, exec_image_t *image);
int exec_run(exec_image_t *image);
void exec_free(exec_image_t *image);