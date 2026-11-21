#pragma once
#include <stddef.h>
#include <stdint.h>
#include "../arch/multiboot.h"

int mm_early_init(uintptr_t kernel_end, const multiboot_info_t *info);
void *mm_early_alloc(size_t size, size_t align);