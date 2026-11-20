#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "../console.h"
#include "../drivers/graphics/vga.h"
#include "../task/task.h"

static volatile int heap_lock = 0;
#define RECLAIM_MIN (64 * 1024)

static inline uint32_t heap_acquire(void) {
    uint32_t eflags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(eflags) :: "memory");
    while (__sync_lock_test_and_set(&heap_lock, 1)) {
        __asm__ volatile("pause");
    }
    return eflags;
}

static inline void heap_release(uint32_t eflags) {
    __sync_lock_release(&heap_lock);
    __asm__ volatile("pushl %0\n\tpopfl" :: "r"(eflags) : "cc", "memory");
}

block_t *heap_start = NULL;
uintptr_t heap_end = 0;
size_t total_allocated = 0;
size_t total_freed = 0;
static int heap_ready = 0;

static void release_block_pages(block_t *b) {
    /* Boundary pages may contain this block's header or its neighbour's. */
    uintptr_t start = ALIGN_UP((uintptr_t)b + sizeof(*b), PAGE_SIZE);
    uintptr_t end = ((uintptr_t)b + sizeof(*b) + b->size) & ~(uintptr_t)(PAGE_SIZE - 1);
    for (uintptr_t page = start; page < end; page += PAGE_SIZE) {
        uintptr_t phys;
        if (paging_get_physical(page, &phys) != 0) continue;
        if (paging_unmap_page(page) != 0) {
            printf("Heap: failed to unmap page at %p\n", (void*)page);
            continue;
        }
        pmm_free_frame(phys);
    }
}

static int commit_block_pages(block_t *b, size_t size) {
    uintptr_t start = ((uintptr_t)b + sizeof(*b)) & ~(uintptr_t)(PAGE_SIZE - 1);
    uintptr_t end = ALIGN_UP((uintptr_t)b + sizeof(*b) + size, PAGE_SIZE);
    for (uintptr_t page = start; page < end; page += PAGE_SIZE) {
        uintptr_t phys;
        if (paging_get_physical(page, &phys) == 0) continue;
        phys = pmm_alloc_frame();
        if (!phys) {
            printf("Heap: failed to allocate page at %p\n", (void*)page);
            release_block_pages(b);
            return -1;
        }
        if (paging_map_page(page, phys, P_PRESENT | P_RW) != 0) {
            printf("Heap: failed to map page at %p\n", (void*)page);
            pmm_free_frame(phys);
            release_block_pages(b);
            return -1;
        }
        __asm__ volatile("invlpg (%0)" :: "r"(page) : "memory");
    }
    return 0;
}

static int expand_heap(size_t min_size) {
    if (min_size > (size_t)-1 - (PAGE_SIZE - 1)) return -1;
    size_t pages = (min_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t old_heap_end = heap_end;
    if (pages > ((uintptr_t)-1 - old_heap_end) / PAGE_SIZE) return -1;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("expand_heap: pmm_alloc_frame failed at page %u\n", (unsigned)i);
            for (size_t j = 0; j < i; j++) {
                uintptr_t mapped = 0;
                if (paging_get_physical(old_heap_end + (j * PAGE_SIZE), &mapped) == 0)
                    pmm_free_frame(mapped);
                paging_unmap_page(old_heap_end + (j * PAGE_SIZE));
            }
            return -1;
        }
        
        uintptr_t virt = old_heap_end + (i * PAGE_SIZE);
        int ret = paging_map_page(virt, phys, 0x3);
        if (ret != 0) {
            printf("expand_heap: paging_map_page failed (ret=%d) at virt=0x%x\n", 
                   ret, (unsigned)virt);
            pmm_free_frame(phys);
            
            for (size_t j = 0; j < i; j++) {
                uintptr_t mapped = 0;
                if (paging_get_physical(old_heap_end + (j * PAGE_SIZE), &mapped) == 0)
                    pmm_free_frame(mapped);
                paging_unmap_page(old_heap_end + (j * PAGE_SIZE));
            }
            return -1;
        }
    }
    
    heap_end = old_heap_end + (pages * PAGE_SIZE);
    return 0;
}

void heap_init(void) {
    heap_start = (block_t*)HEAP_START;
    heap_end = HEAP_START;
    
    if (expand_heap(HEAP_INITIAL_SIZE) != 0) {
        vga_set_color(12,15);
        printf("[FAIL] Heap could not be initialised\n");
        vga_set_color(0,7);
        heap_start = NULL;
        heap_end = 0;
        return;
    }
    
    heap_start->size = HEAP_INITIAL_SIZE - sizeof(block_t);
    heap_start->next = NULL;
    heap_start->free = 1;
    heap_ready = 1;
}

static void split_block(block_t *b, size_t size) {
    if (size > (size_t)-1 - sizeof(block_t) - 16 ||
        b->size < size + sizeof(block_t) + 16)
        return;
    
    block_t *new_block = (block_t*)((char*)b + sizeof(block_t) + size);
    new_block->size = b->size - size - sizeof(block_t);
    new_block->free = 1;
    new_block->next = b->next;
    
    b->size = size;
    b->next = new_block;
}

static block_t *merge_free_blocks(block_t *block) {
    block_t *curr = heap_start;
    
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            if (curr->next == block) block = curr;
            curr->size += sizeof(block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
    return block;
}

void *kmalloc(size_t size) {
    if (size == 0 || !heap_ready || size > (size_t)-1 - 7) return NULL;
    uint32_t eflags = heap_acquire();
    size = ALIGN_UP(size, 8);
    block_t *curr = heap_start;
    
    while (curr) {
        if (curr->free && curr->size >= size) {
            size_t commit_size = curr->size;
            if (size <= (size_t)-1 - sizeof(block_t) - 16 &&
                curr->size >= size + sizeof(block_t) + 16)
                commit_size = size + sizeof(block_t);
            if (commit_block_pages(curr, commit_size) != 0) {
                heap_release(eflags);
                return NULL;
            }
            split_block(curr, size);
            curr->free = 0;
            total_allocated += size;
            heap_release(eflags);
            return (void*)((char*)curr + sizeof(block_t));
        }
        
        if (!curr->next) {
            if (curr->free) {
                uintptr_t old_end = heap_end;
                if (expand_heap(size - curr->size) != 0) {
                    heap_release(eflags);
                    return NULL;
                }
                curr->size += heap_end - old_end;
                continue;
            }
            if (size > (size_t)-1 - sizeof(block_t)) {
                heap_release(eflags);
                return NULL;
            }
            size_t need = size + sizeof(block_t);
            if (expand_heap(need) != 0) {
                heap_release(eflags);
                return NULL;
            }
            
            block_t *new_block = (block_t*)((char*)curr + sizeof(block_t) + curr->size);
            size_t expanded = heap_end - ((uintptr_t)new_block);
            new_block->size = expanded - sizeof(block_t);
            new_block->free = 1;
            new_block->next = NULL;
            curr->next = new_block;
        }
        
        curr = curr->next;
    }
    heap_release(eflags);
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    uint32_t eflags = heap_acquire();
    block_t *b = (block_t*)((char*)ptr - sizeof(block_t));

    block_t *curr = heap_start;
    while (curr && curr != b) curr = curr->next;
    if (!curr) {
        printf("Invalid free heap at %p\n", ptr);
        heap_release(eflags);
        return;
    }
    if (b->free) {
        printf("Double free heap detected at %p\n", ptr);
        heap_release(eflags);
        return;
    }
    
    size_t size = b->size;
    total_freed += size;
    b->free = 1;
    b = merge_free_blocks(b);
    if (size >= RECLAIM_MIN) release_block_pages(b);
    heap_release(eflags);
}
