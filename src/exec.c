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
        
        if (paging_is_mapped(addr)) {
            continue;
        }
        
        /* Allocate physical frame */
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("exec: Cannot allocate frame for 0x%x\n", addr);
            return -1;
        }
        
        /* Map with USER permissions */
        if (paging_map_page(addr, phys, P_PRESENT | P_RW | P_USER) != 0) {
            printf("exec: Cannot map 0x%x -> 0x%x\n", addr, phys);
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
        printf("exec: Cannot open %s\n", path);
        return -1;
    }
    
    uint32_t size = f->size;
    if (size == 0 || size > 2 * 1024 * 1024) {
        printf("exec: Invalid size %u\n", size);
        fat32_close(f);
        return -1;
    }
    
    /* Round up to page boundary */
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t total_size = pages * PAGE_SIZE;
    
    /* Map user memory */
    if (map_user_memory(EXEC_LOAD_ADDR, total_size) != 0) {
        printf("exec: Cannot map memory\n");
        fat32_close(f);
        return -1;
    }
    
    /* Map user stack */
    uintptr_t stack_bottom = EXEC_USER_STACK - EXEC_STACK_SIZE;
    if (map_user_memory(stack_bottom, EXEC_STACK_SIZE) != 0) {
        printf("exec: Cannot map stack\n");
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
        printf("exec: Read error (%d/%u)\n", bytes_read, size);
        return -1;
    }
    
    image->entry_point = EXEC_LOAD_ADDR;
    image->size = size;
    image->memory = (void*)EXEC_LOAD_ADDR;
    image->user_stack = EXEC_USER_STACK;
    
    printf("exec: Loaded %u bytes at 0x%x\n", size, EXEC_LOAD_ADDR);
    
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->memory) return -1;

    uint32_t tid = task_create_user((void(*)(void))image->entry_point, 
                                     "userapp", 
                                     PRIORITY_NORMAL,
                                     image->user_stack);
    if (!tid) {
        printf("exec: Cannot create task\n");
        return -1;
    }

    printf("exec: Started task %u\n", tid);
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