#pragma once
#include <stdint.h>
#include <stddef.h>

void pmm_init_from_multiboot(uint32_t mboot_ptr);
void pmm_reserve_region(uintptr_t start, size_t len);
void pmm_mark_region_free(uintptr_t start, size_t len);
uintptr_t pmm_alloc_frame(void);
void pmm_free_frame(uintptr_t paddr);
size_t pmm_total_frames(void);
void pmm_debug_dump_bitmap(void);
void pmm_print_stats(void);
void pmm_identity_map_bitmap(void);