#include "exec.h"
#include "drivers/fat32.h"
#include "mem/heap.h"
#include "mem/paging.h"
#include "mem/pmm.h"
#include "console.h"
#include "task.h"
#include "gdt.h"

extern void enter_usermode(uint32_t entry, uint32_t user_stack);

static int map_user_memory(uintptr_t vaddr, size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t addr = vaddr + (i * PAGE_SIZE);
        
        if (paging_is_mapped(addr)) {
            continue;
        }
        
        /* Allocate physical frame */
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("Cannot allocate frame for 0x%x\n", addr);
            return -1;
        }
        
        /* Map with USER permissions */
        if (paging_map_page(addr, phys, P_PRESENT | P_RW | P_USER) != 0) {
            printf("Cannot map 0x%x -> 0x%x\n", addr, phys);
            pmm_free_frame(phys);
            return -1;
        }
    }
    return 0;
}

int exec_load(const char *path, exec_image_t *image) {
    if (!path || !image) return -1;
    
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) {
        printf("Cannot open file: %s\n", path);
        return -1;
    }
    
    uint32_t size = f->size;
    /*
        if (size == 0 || size > 2 * 1024 * 1024) {
            printf("Invalid size %u\n", size);
            fat32_close(f);
            return -1;
        }
    */
    
    /* Round up to page boundary */
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t total_size = (pages + (size_t)256) * PAGE_SIZE; /* Added extra 1MB for heap */
    
    /* Map user memory */
    if (map_user_memory(EXEC_LOAD_ADDR, total_size) != 0) {
        printf("Cannot map memory for %s\n", path);
        fat32_close(f);
        return -1;
    }
    
    /* Map user stack */
    uintptr_t stack_bottom = EXEC_USER_STACK - EXEC_STACK_SIZE;
    if (map_user_memory(stack_bottom, EXEC_STACK_SIZE) != 0) {
        printf("Cannot map stack for %s\n", path);
        fat32_close(f);
        return -1;
    }
    
    /* Zero the memory first */
    char *mem = (char*)EXEC_LOAD_ADDR;
    for (size_t i = 0; i < total_size; i++) {
        mem[i] = 0;
    }
    
    /* Read binary */
    int bytes_read = fat32_read(f, (void*)EXEC_LOAD_ADDR, size);
    fat32_close(f);
    
    if (bytes_read != (int)size) {
        printf("Read error (%d/%u)\n", bytes_read, size);
        return -1;
    }
    
    image->entry_point = EXEC_LOAD_ADDR;
    image->size = size;
    image->memory = (void*)EXEC_LOAD_ADDR;
    image->user_stack = EXEC_USER_STACK;
    
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->memory) return -1;

    uint32_t tid = task_create_user((void(*)(void))image->entry_point, 
                                     "executable", 
                                     PRIORITY_NORMAL,
                                     image->user_stack);
    if (!tid) {
        printf("Cannot create task for the executable\n");
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
    
    size_t stack_pages = EXEC_STACK_SIZE / PAGE_SIZE;
    uintptr_t stack_bottom = EXEC_USER_STACK - EXEC_STACK_SIZE;
    for (size_t i = 0; i < stack_pages; i++) {
        uintptr_t vaddr = stack_bottom + (i * PAGE_SIZE);
        paging_unmap_page(vaddr);
    }
    
    image->memory = NULL;
    image->size = 0;
    image->entry_point = 0;
    image->user_stack = 0;
}