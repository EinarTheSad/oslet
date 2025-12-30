#pragma once
#include <stdint.h>

#define P_PRESENT 0x1
#define P_RW 0x2
#define PAGE_SIZE 4096
#define FRAME_SIZE 4096

int paging_identity_enable(uintptr_t upto_phys);
int paging_map_page(uintptr_t vaddr, uintptr_t paddr, uint32_t flags);
int paging_unmap_page(uintptr_t vaddr);
int paging_is_mapped(uintptr_t vaddr);