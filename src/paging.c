#include "paging.h"
#include "pmm.h"
#include <stdint.h>
#include "console.h"

#define PAGE_SIZE       4096u
#define PDE_COUNT       1024u
#define PTE_COUNT       1024u

#define P_PRESENT  (1u << 0)
#define P_RW       (1u << 1)
#define P_USER     (1u << 2)
#define P_PWT      (1u << 3)
#define P_PCD      (1u << 4)
#define P_ACCESSED (1u << 5)
#define P_DIRTY    (1u << 6)
#define P_PS       (1u << 7)

/* Keep the physical address of the current page directory for debugging */
static uintptr_t current_pd_phys = 0;

static inline uint32_t pd_index(uintptr_t addr) { return (addr >> 22) & 0x3FF; }
static inline uint32_t pt_index(uintptr_t addr) { return (addr >> 12) & 0x3FF; }
static inline void memzero(void *dst, size_t n) {
    volatile unsigned char *p = (volatile unsigned char*)dst;
    while (n--) *p++ = 0;
}

static void paging_dump_debug(uintptr_t pd_phys);

/* Internal: allocate a zeroed frame and return its physical address, or 0 on OOM */
static uintptr_t alloc_zeroed_frame(void) {
    uintptr_t f = pmm_alloc_frame();
    printf("DBG: pmm_alloc_frame -> %p\n", (void*)f);
    if (!f) return 0;
    /* sanity: check alignment */
    if (f & 0xFFFu) {
        printf("DBG: ERROR: alloc_zeroed_frame returned unaligned frame %p\n", (void*)f);
    }
    memzero((void*)f, PAGE_SIZE);
    /* show first 16 bytes to confirm zero */
    uint32_t *w = (uint32_t*)f;
    printf("DBG: frame %p first words: %08x %08x %08x %08x\n",
           (void*)f, w[0], w[1], w[2], w[3]);
    return f;
}

int paging_map_page(uintptr_t vaddr, uintptr_t paddr, uint32_t flags) {
    uintptr_t pd_phys = current_pd_phys;
    if (!pd_phys) return -1;

    uint32_t pd_idx = pd_index(vaddr);
    uint32_t pt_idx = pt_index(vaddr);

    uint32_t *pd = (uint32_t*)pd_phys;

    uint32_t pde = pd[pd_idx];
    uint32_t *pt;
    if (!(pde & P_PRESENT)) {
        /* allocate a page-table frame */
        uintptr_t new_pt_phys = alloc_zeroed_frame();
        if (!new_pt_phys) return -2;
        /* set PDE to point to PT frame with present+rw */
        pd[pd_idx] = (uint32_t)(new_pt_phys & 0xFFFFF000u) | P_PRESENT | P_RW;
        pt = (uint32_t*)new_pt_phys;
    } else {
        pt = (uint32_t*)(pde & 0xFFFFF000u);
    }

    /* set PTE = paddr | flags */
    pt[pt_idx] = (uint32_t)(paddr & 0xFFFFF000u) | (flags & 0xFFFu);
    return 0;
}

int paging_identity_enable(uintptr_t upto_phys) {
    printf("paging_identity_enable: upto_phys=%p\n", (void*)upto_phys);
    if (upto_phys == 0) {
        printf("paging: error: upto_phys==0\n");
        return -1;
    }

    /* quick PMM snapshot */
    pmm_debug_dump_bitmap();

    /* Basic sanity: ensure we at least map through kernel and current EIP/ESP */
    extern uint8_t __kernel_end;
    uintptr_t kernel_end = (uintptr_t)&__kernel_end;
    uintptr_t eip;
    asm volatile ("call 1f\n1: pop %0" : "=r"(eip));
    uintptr_t esp;
    asm volatile ("mov %%esp, %0" : "=r"(esp));
    printf("paging: kernel_end=%p EIP=%p ESP=%p\n", (void*)kernel_end, (void*)eip, (void*)esp);
    if (eip >= upto_phys) {
        printf("paging: ERROR: current EIP (%p) is outside requested map_upto (%p)\n",
               (void*)eip, (void*)upto_phys);
        /* not fatal here: we'll continue, but it's a red flag */
    }
    if (esp >= upto_phys) {
        printf("paging: ERROR: current ESP (%p) is outside requested map_upto (%p)\n",
               (void*)esp, (void*)upto_phys);
    }

    /* How many pages will we need? Count PDE/PT overhead */
    size_t pages_needed = (upto_phys + PAGE_SIZE - 1) / PAGE_SIZE; /* pages mapped */
    /* estimate extra pages for page-tables: each page-table covers 1024 pages (4MB).
       So number of page-tables = ceil(pages_needed / 1024). Plus 1 for PD. */
    size_t pt_count = (pages_needed + 1023) / 1024;
    size_t overhead_frames = 1 + pt_count; /* 1 for PD + pt_count for PTs */
    printf("paging: pages_needed=%u pt_count=%u overhead_frames=%u\n",
           (unsigned)pages_needed, (unsigned)pt_count, (unsigned)overhead_frames);

    /* count current free frames in PMM (cheap internal call) */
    size_t free_before = 0;
    size_t total_frames = pmm_total_frames();
    for (size_t i = 0; i < total_frames; ++i) {
        /* use pmm_alloc_frame is destructive; instead we rely on pmm_debug_dump output
           so require that pmm_debug_dump shows free frames earlier. If you want exact
           programmatic count, provide a pmm_free_count() helper in pmm.c. */
    }
    /* We skip programmatic free count here (avoid modifying pmm). Rely on PMMDBG earlier. */

    /* allocate page-directory frame */
    extern void mm_early_init(uintptr_t); /* already called earlier in kmain */
    uintptr_t pd_phys = (uintptr_t) mm_early_alloc(PAGE_SIZE, PAGE_SIZE);
    if (!pd_phys) {
        printf("paging: error: early_alloc for PD failed\n");
        return -1;
    }
    /* zero it */
    memzero((void*)pd_phys, PAGE_SIZE);

    /* mark PD page reserved in PMM so pmm_alloc_frame won't hand it out later */
    pmm_reserve_region(pd_phys, PAGE_SIZE);

    current_pd_phys = pd_phys;
    printf("paging: PD allocated via early_alloc -> %p\n", (void*)pd_phys);

    /* Map loop: try a small mapping strategy (map up to upto_phys, but bail on PT alloc failure) */
    for (uintptr_t p = 0; p < upto_phys; p += PAGE_SIZE) {
        int r = paging_map_page(p, p, P_PRESENT | P_RW);
        if (r != 0) {
            printf("paging: mapping failed for %p (err=%d)\n", (void*)p, r);
            /* on failure, dump PD/PT state for inspection */
            paging_dump_debug(pd_phys);
            return -4;
        }
    }

    /* Ensure VGA memory is mapped if outside range */
    if (upto_phys <= 0xB8000u) {
        int rr = paging_map_page(0xB8000u, 0xB8000u, P_PRESENT | P_RW);
        printf("paging: explicit VGA map returned %d\n", rr);
    }

    /* debug dump before loading CR3 */
    paging_dump_debug(pd_phys);

    /* load CR3 = pd_phys */
    asm volatile ("movl %0, %%cr3\n\t" :: "r"(pd_phys) : "memory");
    unsigned cr3_before, cr0_before;
    asm volatile ("movl %%cr3, %0" : "=r"(cr3_before));
    asm volatile ("movl %%cr0, %0" : "=r"(cr0_before));
    printf("DBG: before enable CR3=%08x CR0=%08x\n", cr3_before, cr0_before);

    /* enable paging */
    asm volatile (
        "movl %%cr0, %%eax\n\t"
        "orl $0x80000000, %%eax\n\t"
        "movl %%eax, %%cr0\n\t"
        :
        :
        : "eax", "memory"
    );

    unsigned cr3_after, cr0_after;
    asm volatile ("movl %%cr3, %0" : "=r"(cr3_after));
    asm volatile ("movl %%cr0, %0" : "=r"(cr0_after));
    printf("DBG: after  enable CR3=%08x CR0=%08x\n", cr3_after, cr0_after);

    /* sanity: ensure CR3 matches pd_phys low 12-bit masked off */
    if ((cr3_after & 0xFFFFF000) != ((unsigned)pd_phys & 0xFFFFF000)) {
        printf("paging: ERROR: CR3 doesn't match PD phys (expected %p, got 0x%08x)\n",
               (void*)pd_phys, cr3_after);
        return -5;
    }

    printf("paging: success (identity up to %p)\n", (void*)upto_phys);
    return 0;
}

static void paging_dump_debug(uintptr_t pd_phys) {
    printf("DBG: PD phys = %p\n", (void*)pd_phys);
    /* print a few low PDEs and PTEs */
    for (int i = 0; i < 8; ++i) {
        uint32_t *pd = (uint32_t*)pd_phys;
        uint32_t pde = pd[i];
        printf("DBG: PDE[%02d] = 0x%08x\n", i, pde);
        if (pde & 1) {
            uintptr_t pt_phys = (uintptr_t)(pde & 0xFFFFF000u);
            /* sanity check pt_phys alignment */
            if (pt_phys & 0xFFFu) printf("DBG: WARNING: PT phys not aligned %p\n", (void*)pt_phys);
            printf("DBG: PT phys = %p (first 8 PTEs):\n", (void*)pt_phys);
            uint32_t *pt = (uint32_t*)pt_phys;
            for (int j = 0; j < 8; ++j) {
                printf("   PTE[%02d][%02d] = 0x%08x\n", i, j, pt[j]);
            }
        }
    }
    /* show PDE for kernel base (1MB) and for current stack pointer */
    extern uint8_t __kernel_end;
    uintptr_t kbase = 0x00100000u;
    uint32_t pdi_k = (kbase >> 22) & 0x3FF;
    printf("DBG: kernel PDE index = %u, PDE = 0x%08x\n", (unsigned)pdi_k, ((uint32_t*)pd_phys)[pdi_k]);

    uintptr_t esp;
    asm volatile ("mov %%esp, %0" : "=r"(esp));
    uint32_t pdi_sp = (esp >> 22) & 0x3FF;
    uint32_t pte_sp = ((uint32_t*)pd_phys)[pdi_sp];
    printf("DBG: ESP=%p, PDEindex=%u, PDE=0x%08x\n", (void*)esp, (unsigned)pdi_sp, pte_sp);
}