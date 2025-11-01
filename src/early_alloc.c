#include "early_alloc.h"

static uintptr_t early_ptr;
static uintptr_t early_end;

void mm_early_init(uintptr_t kernel_end) {
    /* start allocations just after kernel_end */
    early_ptr = (kernel_end + 0xFFF) & ~((uintptr_t)0xFFF); /* page-align up */
    early_end = early_ptr + (16 * 1024 * 1024); /* give 16MB for early allocations */
}

void *mm_early_alloc(size_t size, size_t align) {
    if (align == 0) align = 1;
    uintptr_t p = (early_ptr + (align - 1)) & ~(align - 1);
    early_ptr = p + size;
    return (void*)p;
}