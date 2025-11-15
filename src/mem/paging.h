#pragma once
#include <stdint.h>
#include <stddef.h>

int paging_identity_enable(uintptr_t upto_phys);
int paging_map_page(uintptr_t vaddr, uintptr_t paddr, uint32_t flags);
int paging_unmap_page(uintptr_t vaddr);
int paging_is_mapped(uintptr_t vaddr);