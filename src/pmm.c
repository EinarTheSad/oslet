#define FRAME_SIZE 4096u
#define PAGE_SIZE 4096u
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))

#include "pmm.h"
#include "early_alloc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#include "console.h"

extern uint8_t __kernel_end;

typedef struct {
    uint32_t flags;
    uint32_t mem_lower;
    uint32_t mem_upper;
    uint32_t boot_device;
    uint32_t cmdline;
    uint32_t mods_count;
    uint32_t mods_addr;
    uint32_t syms[4];
    uint32_t mmap_length;
    uint32_t mmap_addr;
} multiboot_info_t;

typedef struct {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) mboot_mmap_entry_t;

static uint8_t *frame_bitmap = NULL;
static uintptr_t phys_base = 0;
static uintptr_t phys_top  = 0;
static size_t nframes = 0;
static size_t bitmap_bytes = 0;

static inline size_t addr_to_frame_index(uintptr_t addr) {
    return (addr - phys_base) >> 12;
}
static inline uintptr_t frame_index_to_addr(size_t i) {
    return phys_base + (i << 12);
}

static inline void bitmap_set(size_t i)   { frame_bitmap[i >> 3] |=  (1u << (i & 7)); }
static inline void bitmap_clear(size_t i) { frame_bitmap[i >> 3] &= ~(1u << (i & 7)); }
static inline bool bitmap_test(size_t i)  { return (frame_bitmap[i >> 3] >> (i & 7)) & 1u; }

/* Reserve everything as used initially, then mark available ranges free. */
static void pmm_build_bitmap(uintptr_t base, uintptr_t top) {
    phys_base = base & ~(FRAME_SIZE - 1);
    phys_top  = (top + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    nframes = (phys_top - phys_base) >> 12;
    bitmap_bytes = (nframes + 7) / 8;

    /* allocate bitmap using early allocator */
    frame_bitmap = (uint8_t*)mm_early_alloc(bitmap_bytes, FRAME_SIZE);
    frame_bitmap = (uint8_t*)ALIGN_UP((uintptr_t)frame_bitmap, PAGE_SIZE);
    /* mark all frames used at first */
    for (size_t i = 0; i < bitmap_bytes; ++i) frame_bitmap[i] = 0xFFu;
}

void pmm_mark_region_free(uintptr_t start, size_t len) {
    if (len == 0) return;
    uintptr_t a = (start + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    uintptr_t end = (start + len) & ~(FRAME_SIZE - 1);
    if (end <= a) return;
    size_t first = addr_to_frame_index(a);
    size_t last_ex = addr_to_frame_index(end);
    for (size_t f = first; f < last_ex; ++f) {
        bitmap_clear(f);
    }
}

void pmm_reserve_region(uintptr_t start, size_t len) {
    if (len == 0) return;
    uintptr_t a = start & ~(FRAME_SIZE - 1);
    uintptr_t end = (start + len + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    size_t first = addr_to_frame_index(a);
    size_t last_ex = addr_to_frame_index(end);
    for (size_t f = first; f < last_ex; ++f) {
        bitmap_set(f);
    }
}

/* Find first zero bit and set it */
uintptr_t pmm_alloc_frame(void) {
    if (!frame_bitmap) {
        printf("PMM: alloc called but frame_bitmap==NULL\n");
        return 0;
    }
    /* quick sanity: print first bytes of bitmap to spot corruption */
    printf("PMM: alloc_frame start: bitmap[0]=%02x bitmap[1]=%02x ...\n",
           (unsigned)frame_bitmap[0], (unsigned)frame_bitmap[1]);

    for (size_t f = 0; f < nframes; ++f) {
        if (!bitmap_test(f)) {
            /* mark used and return address */
            bitmap_set(f);
            uintptr_t addr = frame_index_to_addr(f);
            printf("PMM: alloc success f=%u addr=%p\n", (unsigned)f, (void*)addr);
            return addr;
        }
    }
    printf("PMM: alloc_frame OOM: nframes=%u\n", (unsigned)nframes);
    return 0; /* out of memory */
}

void pmm_free_frame(uintptr_t paddr) {
    if (paddr < phys_base || paddr >= phys_top) return;
    size_t f = addr_to_frame_index(paddr);
    bitmap_clear(f);
}

size_t pmm_total_frames(void) { return nframes; }

void pmm_init_from_multiboot(uint32_t mboot_ptr) {
    multiboot_info_t *mi = (multiboot_info_t*)(uintptr_t)mboot_ptr;
    if (!mi) return;

    uintptr_t highest = 0;

    if (mi->mmap_length == 0 || mi->mmap_addr == 0) {
        /* fallback: mem_upper gives kilobytes above 1MB */
        if (mi->mem_upper) {
            highest = 0x100000u + ((uintptr_t)mi->mem_upper * 1024u);
        } else {
            highest = 0x100000u + (16 * 1024 * 1024); /* conservative fallback 16MB */
        }
        pmm_build_bitmap(0, highest);
        /* mark available range 0..highest free, then reserve low/dangerous areas */
        pmm_mark_region_free(0x100000u, highest - 0x100000u); /* free region above 1MB */
    } else {
        uintptr_t mmap = (uintptr_t)mi->mmap_addr;
        uint32_t mmap_len = mi->mmap_length;
        uintptr_t p = mmap;
        uintptr_t end = mmap + mmap_len;

        /* First pass: compute highest address sampled */
        for (; p < end; ) {
            mboot_mmap_entry_t *e = (mboot_mmap_entry_t*)p;
            uintptr_t base = (uintptr_t)e->addr;
            uintptr_t len  = (uintptr_t)e->len;
            if (base + len > highest) highest = base + len;
            p += e->size + sizeof(e->size);
        }

        pmm_build_bitmap(0, highest);

        /* Second pass: mark type==1 regions free */
        p = mmap;
        for (; p < end; ) {
            mboot_mmap_entry_t *e = (mboot_mmap_entry_t*)p;
            uintptr_t base = (uintptr_t)e->addr;
            uintptr_t len  = (uintptr_t)e->len;
            if (e->type == 1) {
                pmm_mark_region_free(base, len);
            }
            p += e->size + sizeof(e->size);
        }
        pmm_debug_dump_bitmap();
    }
    /* Reserve first physical page (0x00000000..0x00000FFF) — IVT, BIOS data, etc. */
    pmm_reserve_region(0, FRAME_SIZE);
    /* Reserve kernel image */
    uintptr_t k_end = (uintptr_t)&__kernel_end;
    /* kernel base loaded at 1MB */
    uintptr_t kernel_phys_base = 0x00100000u;
    if (k_end > kernel_phys_base) {
        pmm_reserve_region(kernel_phys_base, k_end - kernel_phys_base);
    }

    /* Reserve the bitmap itself (frame_bitmap pointer may be non-null now) */
    if (frame_bitmap) {
        uintptr_t bmp = (uintptr_t)frame_bitmap;
        /* bitmap bytes is computed earlier — reserve them */
        pmm_reserve_region(bmp, bitmap_bytes);
    }

    /* Debug output */
    if (console_get()) {
        printf("PMM: phys_top=0x%08x frames=%u bitmap=%p bytes=%u\n",
               (unsigned)phys_top, (unsigned)pmm_total_frames(),
               (void*)frame_bitmap, (unsigned)bitmap_bytes);
    }
}

void pmm_debug_dump_bitmap(void) {
    if (!frame_bitmap) {
        printf("PMMDBG: frame_bitmap == NULL\n");
        return;
    }
    printf("PMMDBG: phys_base=%p phys_top=%p nframes=%u bitmap=%p bytes=%u\n",
           (void*)phys_base, (void*)phys_top, (unsigned)nframes,
           (void*)frame_bitmap, (unsigned)bitmap_bytes);

    /* show first 64 bytes of bitmap (or up to bitmap_bytes) */
    size_t toshow = bitmap_bytes < 64 ? bitmap_bytes : 64;
    printf("PMMDBG: bitmap first %u bytes:", (unsigned)toshow);
    for (size_t i = 0; i < toshow; ++i) {
        printf(" %02x", (unsigned)frame_bitmap[i]);
    }
    printf("\n");

    /* count free frames */
    size_t freecount = 0;
    for (size_t f = 0; f < nframes; ++f) {
        if (!bitmap_test(f)) ++freecount;
    }
    printf("PMMDBG: free frames = %u / %u\n", (unsigned)freecount, (unsigned)nframes);

    /* show a few sample frame addresses (first few free frames) */
    printf("PMMDBG: sample free frames (first 8):");
    size_t found = 0;
    for (size_t f = 0; f < nframes && found < 8; ++f) {
        if (!bitmap_test(f)) {
            printf(" %p", (void*)frame_index_to_addr(f));
            ++found;
        }
    }
    printf("\n");
}
