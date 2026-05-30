#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "../console.h"
#include "../drivers/graphics/vga.h"
#include "../task/task.h"

static volatile int heap_lock = 0;

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

static int expand_heap(size_t min_size) {
    size_t pages = (min_size + PAGE_SIZE - 1) / PAGE_SIZE;
    uintptr_t old_heap_end = heap_end;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("expand_heap: pmm_alloc_frame failed at page %u\n", (unsigned)i);
            for (size_t j = 0; j < i; j++) {
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
        return;
    }
    
    heap_start->size = HEAP_INITIAL_SIZE - sizeof(block_t);
    heap_start->next = NULL;
    heap_start->free = 1;
}

static void split_block(block_t *b, size_t size) {
    if (b->size < size + sizeof(block_t) + 16)
        return;
    
    block_t *new_block = (block_t*)((char*)b + sizeof(block_t) + size);
    new_block->size = b->size - size - sizeof(block_t);
    new_block->free = 1;
    new_block->next = b->next;
    
    b->size = size;
    b->next = new_block;
}

static void merge_free_blocks(void) {
    block_t *curr = heap_start;
    
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

void *kmalloc(size_t size) {
    if (size == 0) return NULL;
    uint32_t eflags = heap_acquire();
    size = ALIGN_UP(size, 8);
    block_t *curr = heap_start;
    
    while (curr) {
        if (curr->free && curr->size >= size) {
            split_block(curr, size);
            curr->free = 0;
            total_allocated += size;
            heap_release(eflags);
            return (void*)((char*)curr + sizeof(block_t));
        }
        
        if (!curr->next) {
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
    
    if ((uintptr_t)b < HEAP_START || (uintptr_t)b >= heap_end) {
        printf("Invalid free heap at %p\n", ptr);
        heap_release(eflags);
        return;
    }
    
    if (b->free) {
        printf("Double free heap detected at %p\n", ptr);
        heap_release(eflags);
        return;
    }
    
    total_freed += b->size;
    b->free = 1;
    merge_free_blocks();
    heap_release(eflags);
}
