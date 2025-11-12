#pragma once
#include <stdint.h>
#include <stddef.h>

#define EXEC_LOAD_ADDR 0x200000  /* 2MB - user programs load here */
#define EXEC_STACK_SIZE 8192

/* Simple flat binary format */
typedef struct {
    uint32_t entry_point;
    uint32_t size;
    void *memory;
} exec_image_t;

/* Load and execute a binary from filesystem */
int exec_load(const char *path, exec_image_t *image);
int exec_run(exec_image_t *image);
void exec_free(exec_image_t *image);