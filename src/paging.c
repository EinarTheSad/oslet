#include "paging.h"
#include "pmm.h"
#include "console.h"
#include <stdint.h>

#define PAGE_SIZE 4096u
#define P_PRESENT (1u << 0)
#define P_RW (1u << 1)

static uintptr_t current_pd_phys = 0;

static inline uint32_t pd_index(uintptr_t a) { return (a >> 22) & 0x3FF; }
static inline uint32_t pt_index(uintptr_t a) { return (a >> 12) & 0x3FF; }
static inline void memzero(void *dst, size_t n) {
    volatile uint8_t *p = dst;
    while (n--) *p++ = 0;
}

static uintptr_t alloc_zeroed_frame(void) {
    uintptr_t f = pmm_alloc_frame();
    if (!f || (f & 0xFFFu)) return 0;
    memzero((void*)f, PAGE_SIZE);
    return f;
}

static int ensure_pt(uint32_t *pd, uint32_t idx) {
    uint32_t pde = pd[idx];
    if (pde & P_PRESENT) return (int)(pde & 0xFFFFF000u);

    uintptr_t pt_phys = alloc_zeroed_frame();
    if (!pt_phys) return 0;
    pd[idx] = (pt_phys & 0xFFFFF000u) | P_PRESENT | P_RW;
    return (int)pt_phys;
}

int paging_map_page(uintptr_t vaddr, uintptr_t paddr, uint32_t flags) {
    if (!current_pd_phys) return -1;
    uint32_t *pd = (uint32_t*)current_pd_phys;
    uint32_t *pt = (uint32_t*)ensure_pt(pd, pd_index(vaddr));
    if (!pt) return -2;
    pt[pt_index(vaddr)] = (uint32_t)(paddr & 0xFFFFF000u) | (flags & 0xFFFu);
    return 0;
}

int paging_identity_enable(uintptr_t upto_phys) {
    if (!upto_phys) return -1;

    extern uint8_t __kernel_end;
    uintptr_t pd_phys = (uintptr_t)mm_early_alloc(PAGE_SIZE, PAGE_SIZE);
    if (!pd_phys) return -1;
    memzero((void*)pd_phys, PAGE_SIZE);
    pmm_reserve_region(pd_phys, PAGE_SIZE);
    current_pd_phys = pd_phys;

    for (uintptr_t p = 0; p < upto_phys; p += PAGE_SIZE)
        if (paging_map_page(p, p, P_PRESENT | P_RW) != 0)
            return -4;

    if (upto_phys <= 0xB8000u)
        paging_map_page(0xB8000u, 0xB8000u, P_PRESENT | P_RW);

    asm volatile ("movl %0, %%cr3" :: "r"(pd_phys) : "memory");
    asm volatile (
        "movl %%cr0, %%eax\n\t"
        "orl  $0x80000000, %%eax\n\t"
        "movl %%eax, %%cr0\n\t"
        ::: "eax", "memory"
    );
    return 0;
}

static void paging_dump_debug(uintptr_t pd_phys) {
    uint32_t *pd = (uint32_t*)pd_phys;
    for (int i = 0; i < 8; ++i) {
        uint32_t pde = pd[i];
        printf("PDE[%02d]=%08x\n", i, pde);
        if (!(pde & P_PRESENT)) continue;
        uint32_t *pt = (uint32_t*)(pde & 0xFFFFF000u);
        for (int j = 0; j < 8; ++j)
            printf("  PTE[%02d][%02d]=%08x\n", i, j, pt[j]);
    }
}