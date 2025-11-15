#include "exec.h"
#include "drivers/fat32.h"
#include "mem/heap.h"
#include "mem/paging.h"
#include "console.h"
#include "task.h"
#include "gdt.h"

#define P_PRESENT 0x1
#define P_RW 0x2
#define P_USER 0x4
#define PAGE_SIZE 4096

extern void enter_usermode(uint32_t entry, uint32_t user_stack);

static int map_user_memory(uintptr_t vaddr, size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t addr = vaddr + (i * PAGE_SIZE);
        
        /* Check if already mapped */
        if (paging_is_mapped(addr)) {
            continue;
        }
        
        /* For now, identity map (TODO: proper frame allocation) */
        if (paging_map_page(addr, addr, P_PRESENT | P_RW | P_USER) != 0) {
            printf("Failed to map page at 0x%x\n", addr);
            return -1;
        }
    }
    return 0;
}

int exec_load(const char *path, exec_image_t *image) {
    if (!path || !image) return -1;
    
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) {
        printf("Cannot open %s\n", path);
        return -1;
    }
    
    uint32_t size = f->size;
    if (size == 0 || size > 2 * 1024 * 1024) {
        printf("Invalid binary size %u\n", size);
        fat32_close(f);
        return -1;
    }
    
    void *mem = (void*)EXEC_LOAD_ADDR;
    
    if (map_user_memory(EXEC_LOAD_ADDR, size) != 0) {
        printf("Failed to map memory\n");
        fat32_close(f);
        return -1;
    }
    
    /* Map user stack (1 page = 4KB) */
    uintptr_t stack_top = EXEC_USER_STACK - PAGE_SIZE;
    if (map_user_memory(stack_top, PAGE_SIZE) != 0) {
        printf("Failed to map user stack\n");
        fat32_close(f);
        return -1;
    }
    
    int bytes_read = fat32_read(f, mem, size);
    fat32_close(f);
    
    if (bytes_read != (int)size) {
        printf("Read error (%d/%u bytes)\n", bytes_read, size);
        return -1;
    }
    
    image->entry_point = EXEC_LOAD_ADDR;
    image->size = size;
    image->memory = mem;
    image->user_stack = EXEC_USER_STACK;
    
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->memory) return -1;

    /* Create task in kernel mode first */
    uint32_t tid = task_create_user((void(*)(void))image->entry_point, 
                                     "userapp", 
                                     PRIORITY_NORMAL,
                                     image->user_stack);
    if (!tid) {
        printf("Could not create task\n");
        return -1;
    }

    task_yield();
    return 0;
}

void exec_free(exec_image_t *image) {
    if (!image) return;
    
    size_t pages = (image->size + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        uintptr_t vaddr = EXEC_LOAD_ADDR + (i * PAGE_SIZE);
        paging_unmap_page(vaddr);
    }
    
    /* Unmap user stack */
    paging_unmap_page(EXEC_USER_STACK - PAGE_SIZE);
    
    image->memory = NULL;
    image->size = 0;
    image->entry_point = 0;
    image->user_stack = 0;
}