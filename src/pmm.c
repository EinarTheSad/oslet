#define FRAME_SIZE 4096u
#define PAGE_SIZE 4096u
#define P_PRESENT 0x1
#define P_RW 0x2
#define ALIGN_UP(x, a)   ((((uintptr_t)(x)) + ((a) - 1)) & ~((a) - 1))

#include "pmm.h"
#include "early_alloc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "io.h"
#include "console.h"
#include "paging.h"

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

static void pmm_build_bitmap(uintptr_t base, uintptr_t top) {
    phys_base = base & ~(FRAME_SIZE - 1);
    phys_top  = (top + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    nframes = (phys_top - phys_base) >> 12;
    bitmap_bytes = (nframes + 7) / 8;

    /* allocate bitmap via early allocator and align it to page */
    void *alloc = (void*)mm_early_alloc(bitmap_bytes + PAGE_SIZE, PAGE_SIZE);
    if (!alloc) {
        frame_bitmap = NULL;
        return;
    }
    frame_bitmap = (uint8_t*)ALIGN_UP(alloc, PAGE_SIZE);

    /* mark all frames used initially */
    for (size_t i = 0; i < bitmap_bytes; ++i) frame_bitmap[i] = 0xFFu;
}

void pmm_mark_region_free(uintptr_t start, size_t len) {
    if (!frame_bitmap || len == 0) return;
    uintptr_t a = (start + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    uintptr_t end = (start + len) & ~(FRAME_SIZE - 1);
    if (end <= a) return;
    size_t first = addr_to_frame_index(a);
    size_t last_ex = addr_to_frame_index(end);
    if (first >= nframes) return;
    if (last_ex > nframes) last_ex = nframes;
    for (size_t f = first; f < last_ex; ++f) bitmap_clear(f);
}

void pmm_reserve_region(uintptr_t start, size_t len) {
    if (!frame_bitmap || len == 0) return;
    uintptr_t a = start & ~(FRAME_SIZE - 1);
    uintptr_t end = (start + len + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    size_t first = addr_to_frame_index(a);
    size_t last_ex = addr_to_frame_index(end);
    if (first >= nframes) return;
    if (last_ex > nframes) last_ex = nframes;
    for (size_t f = first; f < last_ex; ++f) bitmap_set(f);
}

uintptr_t pmm_alloc_frame(void) {
    if (!frame_bitmap) {
        printf("PMM: alloc called but frame_bitmap==NULL\n");
        return 0;
    }

    for (size_t f = 0; f < nframes; ++f) {
        if (!bitmap_test(f)) {
            bitmap_set(f);
            uintptr_t addr = frame_index_to_addr(f);
            return addr;
        }
    }
    return 0;
}

void pmm_free_frame(uintptr_t paddr) {
    if (!frame_bitmap) return;
    if (paddr < phys_base || paddr >= phys_top) return;
    size_t f = addr_to_frame_index(paddr);
    if (f < nframes) bitmap_clear(f);
}

size_t pmm_total_frames(void) { return nframes; }

void pmm_init_from_multiboot(uint32_t mboot_ptr) {
    multiboot_info_t *mi = (multiboot_info_t*)(uintptr_t)mboot_ptr;
    if (!mi) return;

    uintptr_t highest = 0;

    if (mi->mmap_length == 0 || mi->mmap_addr == 0) {
        if (mi->mem_upper) highest = 0x100000u + ((uintptr_t)mi->mem_upper * 1024u);
        else highest = 0x100000u + (16 * 1024 * 1024);
        pmm_build_bitmap(0, highest);
        pmm_mark_region_free(0x100000u, highest - 0x100000u);
    } else {
        uintptr_t mmap = (uintptr_t)mi->mmap_addr;
        uint32_t mmap_len = mi->mmap_length;
        uintptr_t p = mmap;
        uintptr_t end = mmap + mmap_len;

        for (; p < end; ) {
            mboot_mmap_entry_t *e = (mboot_mmap_entry_t*)p;
            uintptr_t base = (uintptr_t)e->addr;
            uintptr_t len  = (uintptr_t)e->len;
            if (base + len > highest) highest = base + len;
            p += e->size + sizeof(e->size);
        }

        pmm_build_bitmap(0, highest);

        p = mmap;
        for (; p < end; ) {
            mboot_mmap_entry_t *e = (mboot_mmap_entry_t*)p;
            uintptr_t base = (uintptr_t)e->addr;
            uintptr_t len  = (uintptr_t)e->len;
            if (e->type == 1) pmm_mark_region_free(base, len);
            p += e->size + sizeof(e->size);
        }
    }

    pmm_reserve_region(0, FRAME_SIZE);

    uintptr_t k_end = (uintptr_t)&__kernel_end;
    uintptr_t kernel_phys_base = 0x00100000u;
    if (k_end > kernel_phys_base) pmm_reserve_region(kernel_phys_base, k_end - kernel_phys_base);

    if (frame_bitmap) {
        uintptr_t bmp = (uintptr_t)frame_bitmap;
        pmm_reserve_region(bmp, bitmap_bytes);
    }
}

void pmm_identity_map_bitmap(void) {
    if (!frame_bitmap) return;

    uintptr_t bmp_phys = (uintptr_t)frame_bitmap;
    uintptr_t bmp_end  = bmp_phys + bitmap_bytes;
    bmp_phys &= ~(PAGE_SIZE - 1);

    for (uintptr_t p = bmp_phys; p < bmp_end; p += PAGE_SIZE)
        paging_map_page(p, p, P_PRESENT | P_RW);
}

void pmm_print_stats(void) {   
    if (!frame_bitmap) {
        printf("PMM not initialized\n");
        return;
    }

    size_t total = pmm_total_frames();
    size_t free = 0;

    for (size_t f = 0; f < total; ++f)
        if (!bitmap_test(f)) ++free;

    uintptr_t total_bytes = total * FRAME_SIZE;
    uintptr_t free_bytes  = free  * FRAME_SIZE;

    printf("Total memory = %u KiB (%.2f MiB)\n",
           (unsigned)(total_bytes / 1024),
           (double)total_bytes / (1024.0 * 1024.0));

    printf("Free memory  = %u KiB (%.2f MiB)\n",
           (unsigned)(free_bytes / 1024),
           (double)free_bytes / (1024.0 * 1024.0));

    printf("Used frames = %u / %u\n",
           (unsigned)(total - free),
           (unsigned)total);
}