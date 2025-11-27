#pragma once
#include <stddef.h>
#include <stdint.h>

#define HEAP_START 0xC0000000
#define HEAP_INITIAL_SIZE (4 * 1024 * 1024)
#define ALIGN_UP(x, a) ((((uintptr_t)(x)) + ((a) - 1)) & ~((a) - 1))

typedef struct block {
    size_t size;
    struct block *next;
    int free;
} __attribute__((packed)) block_t;

extern block_t *heap_start;
extern uintptr_t heap_end;
extern size_t total_allocated;
extern size_t total_freed;

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void heap_print_stats(void);