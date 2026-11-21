#include "paging.h"
#include "pmm.h"
#include "../console.h"
#include "../drivers/graphics/vga.h"
#include <stdint.h>
#include "early_alloc.h"
#include "../arch/cpu.h"

static uintptr_t current_pd_phys = 0;
static uintptr_t identity_limit = 0;

void paging_invalidate(uintptr_t vaddr) {
    static int has_invlpg = -1;
    if (has_invlpg < 0) has_invlpg = cpu_has_invlpg();
    if (has_invlpg) {
        __asm__ volatile("invlpg (%0)" :: "r"(vaddr) : "memory");
    } else {
        uintptr_t cr3;
        __asm__ volatile("movl %%cr3, %0\n\tmovl %0, %%cr3"
                         : "=r"(cr3) :: "memory");
    }
}

static inline uint32_t pd_index(uintptr_t a) { return (a >> 22) & 0x3FF; }
static inline uint32_t pt_index(uintptr_t a) { return (a >> 12) & 0x3FF; }
static inline void memzero(void *dst, size_t n) {
    volatile uint8_t *p = dst;
    while (n--) *p++ = 0;
}

static uintptr_t alloc_zeroed_frame(void) {
    /* Page-table frames are accessed through their identity mapping. */
    uintptr_t f = pmm_alloc_contiguous(PAGE_SIZE, identity_limit, 1);
    if (!f || (f & 0xFFFu)) return 0;   
    memzero((void*)f, PAGE_SIZE);
    return f;
}

static uintptr_t ensure_pt(uint32_t *pd, uint32_t idx) {
    uint32_t pde = pd[idx];
    if (pde & P_PRESENT) return (uintptr_t)(pde & 0xFFFFF000u);

    uintptr_t pt_phys = alloc_zeroed_frame();
    if (!pt_phys) return 0;
    pd[idx] = (pt_phys & 0xFFFFF000u) | P_PRESENT | P_RW;
    return pt_phys;
}

int paging_map_page(uintptr_t vaddr, uintptr_t paddr, uint32_t flags) {
    if (!current_pd_phys) return -1;
    if (vaddr & 0xFFF || paddr & 0xFFF) return -3;
    
    uint32_t *pd = (uint32_t*)current_pd_phys;
    
    uintptr_t pt_phys = ensure_pt(pd, pd_index(vaddr));
    if (!pt_phys) return -2;
    
    uint32_t *pt = (uint32_t*)pt_phys;
    uint32_t existing = pt[pt_index(vaddr)];
    if ((existing & P_PRESENT) && (existing & 0xFFFFF000u) != paddr)
        return -4;
    pt[pt_index(vaddr)] = (uint32_t)(paddr & 0xFFFFF000u) | (flags & 0xFFFu);
    return 0;
}

int paging_identity_enable(uintptr_t upto_phys) {
    if (!upto_phys || (upto_phys & (PAGE_SIZE - 1))) return -1;
    identity_limit = upto_phys;
    
    uintptr_t pd_phys = (uintptr_t)mm_early_alloc(PAGE_SIZE, PAGE_SIZE);
    if (!pd_phys) {
        vga_set_color(12,15);
        printf("FAILED to allocate page directory\n");
        vga_set_color(0,7);
        return -1;
    }
    
    memzero((void*)pd_phys, PAGE_SIZE);
    pmm_reserve_region(pd_phys, PAGE_SIZE);
    current_pd_phys = pd_phys;

    for (uintptr_t p = 0; p < upto_phys; p += PAGE_SIZE) {
        if (paging_map_page(p, p, P_PRESENT | P_RW) != 0) {
            vga_set_color(12,15);
            printf("FAILED to map page at 0x%x\n", (unsigned)p);
            vga_set_color(0,7);
            return -4;
        }
    }

    if (upto_phys <= 0xB8000u)
        paging_map_page(0xB8000u, 0xB8000u, P_PRESENT | P_RW);

    asm volatile ("movl %0, %%cr3" :: "r"(pd_phys) : "memory");
    asm volatile (
        "movl %%cr0, %%eax\n\t"
        "orl  $0x80000000, %%eax\n\t"
        "movl %%eax, %%cr0\n\t"
        ::: "eax", "memory"
    );
    /* Leave identity-mapped RAM available for future page tables. */
    pmm_prefer_frames_above(identity_limit);
    return 0;
}

int paging_unmap_page(uintptr_t vaddr) {
    if (!current_pd_phys) return -1;
    if (vaddr & 0xFFF) return -3;
    
    uint32_t *pd = (uint32_t*)current_pd_phys;
    uint32_t pde = pd[pd_index(vaddr)];
    
    if (!(pde & P_PRESENT)) return 0;
    
    uint32_t *pt = (uint32_t*)(pde & 0xFFFFF000u);
    pt[pt_index(vaddr)] = 0;

    int empty = 1;
    for (uint32_t i = 0; i < 1024; i++) {
        if (pt[i] & P_PRESENT) {
            empty = 0;
            break;
        }
    }
    if (empty) {
        pd[pd_index(vaddr)] = 0;
        pmm_free_frame((uintptr_t)pt);
    }
    
    paging_invalidate(vaddr);
    return 0;
}

int paging_is_mapped(uintptr_t vaddr) {
    if (!current_pd_phys) return 0;
    if (vaddr & 0xFFF) return 0;
    
    uint32_t *pd = (uint32_t*)current_pd_phys;
    uint32_t pde = pd[pd_index(vaddr)];
    
    if (!(pde & P_PRESENT)) return 0;
    
    uint32_t *pt = (uint32_t*)(pde & 0xFFFFF000u);
    uint32_t pte = pt[pt_index(vaddr)];
    
    return (pte & P_PRESENT) ? 1 : 0;
}

int paging_get_physical(uintptr_t vaddr, uintptr_t *out_paddr) {
    if (!out_paddr || !current_pd_phys || (vaddr & 0xFFF)) return -1;
    uint32_t *pd = (uint32_t*)current_pd_phys;
    uint32_t pde = pd[pd_index(vaddr)];
    if (!(pde & P_PRESENT)) return -1;
    uint32_t *pt = (uint32_t*)(pde & 0xFFFFF000u);
    uint32_t pte = pt[pt_index(vaddr)];
    if (!(pte & P_PRESENT)) return -1;
    *out_paddr = (uintptr_t)(pte & 0xFFFFF000u);
    return 0;
}
