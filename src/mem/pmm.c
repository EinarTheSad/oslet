#define ALIGN_UP(x, a)   ((((uintptr_t)(x)) + ((a) - 1)) & ~((a) - 1))

#include "pmm.h"
#include "early_alloc.h"
#include <stdint.h>
#include <stdbool.h>
#include "../irq/io.h"
#include "../console.h"
#include "paging.h"
#include "../arch/multiboot.h"

extern uint8_t __kernel_end;

static uint8_t *frame_bitmap = NULL;
static uint8_t *ram_bitmap = NULL;
static uintptr_t phys_base = 0;
static uintptr_t phys_top  = 0;
static size_t nframes = 0;
static size_t bitmap_bytes = 0;
static size_t last_free_hint = 0;
static size_t preferred_frame = 0;
static size_t free_limit = 0;

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
    last_free_hint = 0;
    preferred_frame = 0;
    free_limit = 0;

    /* allocate bitmap via early allocator and align it to page */
    void *alloc = (void*)mm_early_alloc(bitmap_bytes + PAGE_SIZE, PAGE_SIZE);
    if (!alloc) {
        frame_bitmap = NULL;
        return;
    }
    frame_bitmap = (uint8_t*)ALIGN_UP(alloc, PAGE_SIZE);

    alloc = (void*)mm_early_alloc(bitmap_bytes + PAGE_SIZE, PAGE_SIZE);
    if (!alloc) {
        frame_bitmap = NULL;
        return;
    }
    ram_bitmap = (uint8_t*)ALIGN_UP(alloc, PAGE_SIZE);

    /* mark all frames used initially */
    for (size_t i = 0; i < bitmap_bytes; ++i) {
        frame_bitmap[i] = 0xFFu;
        ram_bitmap[i] = 0;
    }
}

void pmm_mark_region_free(uintptr_t start, size_t len) {
    if (!frame_bitmap || len == 0) return;
    if (start > UINTPTR_MAX - len) return;
    uintptr_t a = (start + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);
    uintptr_t end = (start + len) & ~(FRAME_SIZE - 1);
    if (end <= a) return;
    size_t first = addr_to_frame_index(a);
    size_t last_ex = addr_to_frame_index(end);
    if (first >= nframes) return;
    if (last_ex > nframes) last_ex = nframes;
    if (last_ex > free_limit) free_limit = last_ex;
    for (size_t f = first; f < last_ex; ++f) {
        bitmap_clear(f);
        ram_bitmap[f >> 3] |= (1u << (f & 7));
    }
}

void pmm_reserve_region(uintptr_t start, size_t len) {
    if (!frame_bitmap || len == 0) return;
    if (start > UINTPTR_MAX - len || start + len > UINTPTR_MAX - (FRAME_SIZE - 1)) return;
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

    size_t hint = last_free_hint;
    if (hint < preferred_frame || hint > free_limit) hint = preferred_frame;
    for (int pass = 0; pass < 3; ++pass) {
        size_t start = pass == 0 ? hint : (pass == 1 ? preferred_frame : 0);
        size_t end = pass == 0 ? free_limit : (pass == 1 ? hint : preferred_frame);
        for (size_t f = start; f < end; ++f) {
            if (!bitmap_test(f)) {
                bitmap_set(f);
                last_free_hint = f >= preferred_frame ? f + 1 : preferred_frame;
                return frame_index_to_addr(f);
            }
        }
    }
    return 0;
}

void pmm_prefer_frames_above(uintptr_t addr) {
    if (addr <= phys_base) preferred_frame = 0;
    else {
        uintptr_t offset = addr - phys_base;
        preferred_frame = offset / FRAME_SIZE + (offset % FRAME_SIZE != 0);
        if (preferred_frame > free_limit) preferred_frame = free_limit;
    }
    last_free_hint = preferred_frame;
}

uintptr_t pmm_alloc_contiguous(uintptr_t start, uintptr_t end, size_t count) {
    if (!frame_bitmap || !count || end <= start ||
        start > UINTPTR_MAX - (FRAME_SIZE - 1)) return 0;

    start = (start + FRAME_SIZE - 1) & ~(uintptr_t)(FRAME_SIZE - 1);
    end &= ~(uintptr_t)(FRAME_SIZE - 1);
    if (start < phys_base) start = phys_base;
    if (end > phys_top) end = phys_top;
    if (end <= start || count > (end - start) / FRAME_SIZE) return 0;

    uint32_t flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    size_t first = addr_to_frame_index(start);
    size_t limit = addr_to_frame_index(end);
    size_t run = 0;
    uintptr_t result = 0;

    for (size_t i = first; i < limit; ++i) {
        run = bitmap_test(i) ? 0 : run + 1;
        if (run == count) {
            size_t begin = i + 1 - count;
            for (size_t j = begin; j <= i; ++j) bitmap_set(j);
            result = frame_index_to_addr(begin);
            break;
        }
    }

    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(flags) : "cc", "memory");
    return result;
}

int pmm_region_overlaps_ram(uintptr_t start, size_t len) {
    if (!ram_bitmap || !len) return -1;
    uint64_t end = (uint64_t)start + len;
    if (end > 0x100000000ull) return -1;
    uint64_t first = (uint64_t)start & ~(uint64_t)(FRAME_SIZE - 1);
    uint64_t limit = (end + FRAME_SIZE - 1) & ~(uint64_t)(FRAME_SIZE - 1);
    if (first < phys_base) first = phys_base;
    if (limit > phys_top) limit = phys_top;
    if (limit <= first) return 0;

    size_t begin = addr_to_frame_index((uintptr_t)first);
    size_t finish = addr_to_frame_index((uintptr_t)limit);
    for (size_t i = begin; i < finish; ++i) {
        if (ram_bitmap[i >> 3] & (1u << (i & 7))) return 1;
    }
    return 0;
}

void pmm_free_frame(uintptr_t paddr) {
    if (!frame_bitmap) return;
    if (paddr < phys_base || paddr >= phys_top) return;
    size_t f = addr_to_frame_index(paddr);
    if (f < nframes) {
        bitmap_clear(f);
        if (f >= preferred_frame && f < last_free_hint) last_free_hint = f;
    }
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
            multiboot_mmap_entry_t *e = (multiboot_mmap_entry_t*)p;
            uintptr_t base = (uintptr_t)e->addr;
            uintptr_t len  = (uintptr_t)e->len;
            if (base + len > highest) highest = base + len;
            p += e->size + sizeof(e->size);
        }

        pmm_build_bitmap(0, highest);

        p = mmap;
        for (; p < end; ) {
            multiboot_mmap_entry_t *e = (multiboot_mmap_entry_t*)p;
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
        pmm_reserve_region((uintptr_t)ram_bitmap, bitmap_bytes);
    }

    if ((mi->flags & MULTIBOOT_INFO_FLAG_FRAMEBUF) &&
        mi->framebuffer_addr <= UINTPTR_MAX &&
        mi->framebuffer_height != 0 &&
        mi->framebuffer_pitch <= UINT64_MAX / mi->framebuffer_height) {
        uint64_t fb_len = (uint64_t)mi->framebuffer_pitch * mi->framebuffer_height;
        if (fb_len <= SIZE_MAX &&
            mi->framebuffer_addr <= UINT64_MAX - fb_len) {
            pmm_reserve_region((uintptr_t)mi->framebuffer_addr, (size_t)fb_len);
        }
    }
}

void pmm_identity_map_bitmap(void) {
    if (!frame_bitmap) return;

    uintptr_t bmp_phys = (uintptr_t)frame_bitmap;
    uintptr_t bmp_end  = bmp_phys + bitmap_bytes;
    bmp_phys &= ~(PAGE_SIZE - 1);

    for (uintptr_t p = bmp_phys; p < bmp_end; p += PAGE_SIZE)
        paging_map_page(p, p, P_PRESENT | P_RW);

    bmp_phys = (uintptr_t)ram_bitmap;
    bmp_end = bmp_phys + bitmap_bytes;
    bmp_phys &= ~(PAGE_SIZE - 1);
    for (uintptr_t p = bmp_phys; p < bmp_end; p += PAGE_SIZE)
        paging_map_page(p, p, P_PRESENT | P_RW);
}

size_t pmm_count_free_frames(void) {
    size_t free = 0;
    for (size_t f = 0; f < nframes; ++f) {
        if (!bitmap_test(f)) ++free;
    }
    return free;
}