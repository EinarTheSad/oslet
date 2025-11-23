#pragma once
#include <stddef.h>
#include <stdint.h>

void heap_init(void);
void *kmalloc(size_t size);
void kfree(void *ptr);
void heap_print_stats(void);

typedef struct block {
    size_t size;
    struct block *next;
    int free;
} __attribute__((packed)) block_t;