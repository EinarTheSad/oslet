#include "exec.h"
#include "drivers/fat32.h"
#include "mem/heap.h"
#include "mem/paging.h"
#include "mem/pmm.h"
#include "console.h"
#include "task.h"

static int map_memory(uintptr_t vaddr, size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t addr = vaddr + (i * PAGE_SIZE);
        
        if (paging_is_mapped(addr)) {
            continue;
        }
        
        uintptr_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("Cannot allocate frame for 0x%x\n", addr);
            return -1;
        }
        
        if (paging_map_page(addr, phys, P_PRESENT | P_RW) != 0) {
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
    
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t total_size = (pages + 256) * PAGE_SIZE;
    
    if (map_memory(EXEC_LOAD_ADDR, total_size) != 0) {
        printf("Cannot map memory for %s\n", path);
        fat32_close(f);
        return -1;
    }
    
    char *mem = (char*)EXEC_LOAD_ADDR;
    for (size_t i = 0; i < total_size; i++) {
        mem[i] = 0;
    }
    
    int bytes_read = fat32_read(f, (void*)EXEC_LOAD_ADDR, size);
    fat32_close(f);
    
    if (bytes_read != (int)size) {
        printf("Read error (%d/%u)\n", bytes_read, size);
        return -1;
    }
    
    image->entry_point = EXEC_LOAD_ADDR;
    image->size = size;
    image->memory = (void*)EXEC_LOAD_ADDR;
    
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->memory) return -1;

    uint32_t tid = task_create((void(*)(void))image->entry_point, 
                               "executable", 
                               PRIORITY_NORMAL);
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
    
    image->memory = NULL;
    image->size = 0;
    image->entry_point = 0;
}