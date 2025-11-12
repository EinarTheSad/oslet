#include "exec.h"
#include "fat32.h"
#include "heap.h"
#include "paging.h"
#include "console.h"
#include "string.h"

#define P_PRESENT 0x1
#define P_RW 0x2
#define P_USER 0x4
#define PAGE_SIZE 4096

/* Map memory for user program with proper permissions */
static int map_user_memory(uintptr_t vaddr, size_t size) {
    size_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    for (size_t i = 0; i < pages; i++) {
        uintptr_t addr = vaddr + (i * PAGE_SIZE);
        /* Identity map (TODO: allocate frames) */
        if (paging_map_page(addr, addr, P_PRESENT | P_RW | P_USER) != 0) {
            return -1;
        }
    }
    return 0;
}

int exec_load(const char *path, exec_image_t *image) {
    if (!path || !image) return -1;
    
    /* Open binary file */
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) {
        printf("Cannot open %s\n", path);
        return -1;
    }
    
    /* Get file size */
    uint32_t size = f->size;
    if (size == 0 || size > 1024 * 1024) {  /* max 1MB binaries */
        printf("Invalid binary size %u\n", size);
        fat32_close(f);
        return -1;
    }
    
    /* Allocate memory at fixed load address */
    void *mem = (void*)EXEC_LOAD_ADDR;
    
    /* Map memory for the binary (identity mapped for now) */
    if (map_user_memory(EXEC_LOAD_ADDR, size) != 0) {
        printf("Failed to map memory for the executable\n");
        fat32_close(f);
        return -1;
    }
    
    /* Read entire binary into memory */
    int bytes_read = fat32_read(f, mem, size);
    fat32_close(f);
    
    if (bytes_read != (int)size) {
        printf("Read error (%d/%u bytes)\n", bytes_read, size);
        return -1;
    }
    
    /* Fill in image info */
    image->entry_point = EXEC_LOAD_ADDR; /* flat binary - start at beginning */
    image->size = size;
    image->memory = mem;
    
    printf("Loaded %s (%u bytes) at 0x%x\n", path, size, EXEC_LOAD_ADDR);
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->memory) return -1;
    
    /* Setup stack for the program */
    void *stack = kmalloc(EXEC_STACK_SIZE);
    if (!stack) {
        printf("Stack allocation failed\n");
        return -1;
    }
    
    uint32_t stack_top = (uint32_t)stack + EXEC_STACK_SIZE;
    
    /* Jump to entry point with new stack */
    void (*entry)(void) = (void(*)(void))image->entry_point;
    
    printf("Jumping to 0x%x\n", image->entry_point);
    
    /* Save kernel stack, switch to user stack, call entry */
    __asm__ volatile (
        "movl %%esp, %%ebx\n\t"      /* save kernel ESP */
        "movl %0, %%esp\n\t"          /* switch to user stack */
        "call *%1\n\t"                /* call entry point */
        "movl %%ebx, %%esp\n\t"      /* restore kernel ESP */
        :: "r"(stack_top), "r"(entry)
        : "ebx", "memory"
    );
    
    kfree(stack);
    return 0;
}

void exec_free(exec_image_t *image) {
    if (!image) return;
    /* For now, memory is at fixed address so we don't free it */
    /* TODO: unmap pages and free frames */
    image->memory = NULL;
    image->size = 0;
    image->entry_point = 0;
}