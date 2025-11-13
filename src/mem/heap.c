#include "heap.h"
#include "pmm.h"
#include "paging.h"
#include "../console.h"

#define HEAP_START 0xC0000000
#define HEAP_INITIAL_SIZE (4 * 1024 * 1024)
#define PAGE_SIZE 4096
#define ALIGN_UP(x, a) ((((uintptr_t)(x)) + ((a) - 1)) & ~((a) - 1))

typedef struct block {
    size_t size;
    struct block *next;
    int free;
} block_t;

static block_t *heap_start = NULL;
static uintptr_t heap_end = 0;
static size_t total_allocated = 0;
static size_t total_freed = 0;

extern void vga_set_color(uint8_t background, uint8_t foreground);

static int expand_heap(size_t min_size) {
    size_t pages = (min_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("expand_heap: pmm_alloc_frame failed at page %u\n", (unsigned)i);
            return -1;
        }
        
        uintptr_t virt = heap_end + (i * PAGE_SIZE);
        int ret = paging_map_page(virt, phys, 0x3);
        if (ret != 0) {
            printf("expand_heap: paging_map_page failed (ret=%d) at virt=0x%x\n", 
                   ret, (unsigned)virt);
            pmm_free_frame(phys);
            
            /* Cleanup already mapped pages in this expansion */
            for (size_t j = 0; j < i; j++) {
                /* can't unmap here without a full page table walker */
                /* just mark frames as lost for now */
                /* TODO: page table walker */
            }
            return -1;
        }
    }
    
    heap_end += pages * PAGE_SIZE;
    return 0;
}

void heap_init(void) {
    heap_start = (block_t*)HEAP_START;
    heap_end = HEAP_START;
    
    if (expand_heap(HEAP_INITIAL_SIZE) != 0) {
        vga_set_color(12,15);
        printf("FAILED to initialize heap\n");
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
    __asm__ volatile ("cli");
    size = ALIGN_UP(size, 8);
    block_t *curr = heap_start;
    
    while (curr) {
        if (curr->free && curr->size >= size) {
            split_block(curr, size);
            curr->free = 0;
            total_allocated += size;
            return (void*)((char*)curr + sizeof(block_t));
        }
        
        if (!curr->next) {
            size_t need = size + sizeof(block_t);
            if (expand_heap(need) != 0)
                return NULL;
            
            block_t *new_block = (block_t*)((char*)curr + sizeof(block_t) + curr->size);
            size_t expanded = heap_end - ((uintptr_t)new_block);
            new_block->size = expanded - sizeof(block_t);
            new_block->free = 1;
            new_block->next = NULL;
            curr->next = new_block;
        }
        
        curr = curr->next;
    }
    __asm__ volatile ("sti");
    return NULL;
}

void kfree(void *ptr) {
    if (!ptr) return;
    __asm__ volatile ("cli");
    block_t *b = (block_t*)((char*)ptr - sizeof(block_t));
    
    if ((uintptr_t)b < HEAP_START || (uintptr_t)b >= heap_end) {
        printf("Invalid free heap at %p\n", ptr);
        return;
    }
    
    if (b->free) {
        printf("Double free heap detected at %p\n", ptr);
        return;
    }
    
    total_freed += b->size;
    b->free = 1;
    merge_free_blocks();
    __asm__ volatile ("sti");
}

void heap_print_stats(void) {
    size_t free_blocks = 0;
    size_t used_blocks = 0;
    size_t free_mem = 0;
    size_t used_mem = 0;
    
    block_t *curr = heap_start;
    while (curr) {
        if (curr->free) {
            free_blocks++;
            free_mem += curr->size;
        } else {
            used_blocks++;
            used_mem += curr->size;
        }
        curr = curr->next;
    }
    
    printf("Total heap size: %u KB\n", (unsigned)((heap_end - HEAP_START) / 1024));
    printf("Used: %u bytes (%u blocks)\n", (unsigned)used_mem, (unsigned)used_blocks);
    printf("Free: %u bytes (%u blocks)\n", (unsigned)free_mem, (unsigned)free_blocks);
    printf("Total allocated: %u bytes\n", (unsigned)total_allocated);
    printf("Total freed: %u bytes\n", (unsigned)total_freed);
}