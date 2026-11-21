#include "early_alloc.h"

static uintptr_t early_ptr;
static uintptr_t early_end;

static void limit_before(uintptr_t start, size_t size) {
    if (!size || start > UINTPTR_MAX - size) return;
    uintptr_t end = start + size;
    if (start <= early_ptr && end > early_ptr) {
        if (end > UINTPTR_MAX - 0xfffu) early_ptr = early_end;
        else early_ptr = (end + 0xfffu) & ~(uintptr_t)0xfffu;
    }
    else if (start > early_ptr && start < early_end)
        early_end = start;
}

int mm_early_init(uintptr_t kernel_end, const multiboot_info_t *info) {
    if (!info || kernel_end > UINTPTR_MAX - 0xfffu) return -1;
    early_ptr = (kernel_end + 0xfffu) & ~(uintptr_t)0xfffu;
    early_end = 0;

    if (info->flags & MULTIBOOT_INFO_FLAG_MMAP) {
        if (!info->mmap_addr || !info->mmap_length ||
            info->mmap_addr > UINT32_MAX - info->mmap_length)
            return -1;
        uintptr_t p = info->mmap_addr;
        uintptr_t end = p + info->mmap_length;
        while (p < end) {
            if (end - p < sizeof(uint32_t)) return -1;
            const multiboot_mmap_entry_t *entry = (const void*)p;
            if (entry->size < 20 || entry->size > end - p - sizeof(entry->size))
                return -1;
            uint64_t top = entry->addr + entry->len;
            if (top < entry->addr) return -1;
            if (entry->type == 1 && entry->addr <= early_ptr && top > early_ptr) {
                early_end = top > UINTPTR_MAX ? UINTPTR_MAX : (uintptr_t)top;
            }
            p += entry->size + sizeof(entry->size);
        }
        if (!early_end) return -1;
        p = info->mmap_addr;
        while (p < end) {
            const multiboot_mmap_entry_t *entry = (const void*)p;
            uint64_t top = entry->addr + entry->len;
            if (entry->type != 1 && entry->addr < early_end && top > early_ptr) {
                if (entry->addr <= early_ptr) return -1;
                early_end = (uintptr_t)entry->addr;
            }
            p += entry->size + sizeof(entry->size);
        }
    } else if (info->flags & MULTIBOOT_INFO_FLAG_MEMORY) {
        uint64_t top = 0x100000ull + (uint64_t)info->mem_upper * 1024u;
        if (top > early_ptr)
            early_end = top > UINTPTR_MAX ? UINTPTR_MAX : (uintptr_t)top;
    }

    if (!early_end) return -1;
    limit_before((uintptr_t)info, sizeof(*info));
    if (info->flags & MULTIBOOT_INFO_FLAG_MMAP)
        limit_before(info->mmap_addr, info->mmap_length);
    return early_ptr < early_end ? 0 : -1;
}

void *mm_early_alloc(size_t size, size_t align) {
    if (align == 0) align = 1;
    if ((align & (align - 1)) || early_ptr > UINTPTR_MAX - (align - 1))
        return (void*)0;
    uintptr_t p = (early_ptr + (align - 1)) & ~(align - 1);
    if (p > UINTPTR_MAX - size) return (void*)0;
    uintptr_t next_ptr = p + size;
    
    /* check memory limit */
    if (next_ptr > early_end) {
        return (void*)0;
    }
    
    early_ptr = next_ptr;
    return (void*)p;
}