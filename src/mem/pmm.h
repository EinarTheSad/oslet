#pragma once
#include <stdint.h>
#include <stddef.h>

void pmm_init_from_multiboot(uint32_t mboot_ptr);
void pmm_reserve_region(uintptr_t start, size_t len);
void pmm_mark_region_free(uintptr_t start, size_t len);
uintptr_t pmm_alloc_frame(void);
void pmm_prefer_frames_above(uintptr_t addr);
uintptr_t pmm_alloc_contiguous(uintptr_t start, uintptr_t end, size_t count);
int pmm_region_overlaps_ram(uintptr_t start, size_t len);
void pmm_free_frame(uintptr_t paddr);
size_t pmm_total_frames(void);
int pmm_ready(void);
void pmm_identity_map_bitmap(void);
size_t pmm_count_free_frames(void);