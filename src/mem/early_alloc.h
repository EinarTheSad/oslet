#pragma once
#include <stddef.h>
#include <stdint.h>

void mm_early_init(uintptr_t kernel_end);
void *mm_early_alloc(size_t size, size_t align);