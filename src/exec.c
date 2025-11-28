#include "exec.h"
#include "drivers/fat32.h"
#include "mem/heap.h"
#include "mem/paging.h"
#include "mem/pmm.h"
#include "console.h"
#include "task.h"
#include "lib/string.h"

/* Simple address allocator for multiple processes */
static exec_allocator_t allocator = {
    .base = EXEC_BASE_ADDR,
    .size = EXEC_MAX_SIZE,
    .next_slot = 0
};

int exec_init(void) {
    allocator.next_slot = 0;
    return 0;
}

/* Allocate a unique base address for a new process */
static uint32_t alloc_process_base(void) {
    uint32_t base = allocator.base + (allocator.next_slot * allocator.size);
    allocator.next_slot++;
    
    /* Wrap around if we exceed reasonable limits (32 slots) */
    if (allocator.next_slot > 32) {
        allocator.next_slot = 0;
    }
    return base;
}

/* Map memory region, allocating physical frames */
static int map_region(uint32_t vaddr, uint32_t size, uint32_t flags) {
    uint32_t aligned_start = vaddr & ~(PAGE_SIZE - 1);
    uint32_t aligned_end = (vaddr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    for (uint32_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        if (paging_is_mapped(addr)) continue;
        
        uint32_t phys = pmm_alloc_frame();
        if (!phys) {
            printf("[ELF] Out of memory at 0x%x\n", addr);
            return -1;
        }
        
        if (paging_map_page(addr, phys, flags) != 0) {
            pmm_free_frame(phys);
            printf("[ELF] Map failed: 0x%x\n", addr);
            return -1;
        }
    }
    return 0;
}

/* Zero memory region */
static void zero_region(uint32_t addr, uint32_t size) {
    uint8_t *p = (uint8_t *)addr;
    for (uint32_t i = 0; i < size; i++) {
        p[i] = 0;
    }
}

/* Validate ELF header */
int elf_validate(const void *data, uint32_t size) {
    if (size < sizeof(elf32_ehdr_t)) {
        return -1;
    }
    
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)data;
    
    /* Check magic */
    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC) {
        return -1;
    }
    
    /* Check class (32-bit) */
    if (ehdr->e_ident[4] != ELFCLASS32) {
        printf("[ELF] Not 32-bit\n");
        return -1;
    }
    
    /* Check endianness (little) */
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        printf("[ELF] Not little-endian\n");
        return -1;
    }
    
    /* Check type (executable) */
    if (ehdr->e_type != ET_EXEC) {
        printf("[ELF] Not executable (type=%d)\n", ehdr->e_type);
        return -1;
    }
    
    /* Check machine (i386) */
    if (ehdr->e_machine != EM_386) {
        printf("[ELF] Not i386 (machine=%d)\n", ehdr->e_machine);
        return -1;
    }
    
    return 0;
}

int exec_load(const char *path, exec_image_t *image) {
    if (!path || !image) return -1;
    
    memset_s(image, 0, sizeof(exec_image_t));
    
    /* Open file */
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) {
        printf("[ELF] Cannot open: %s\n", path);
        return -1;
    }
    
    uint32_t file_size = f->size;
    
    /* Allocate buffer for file */
    void *file_data = kmalloc(file_size);
    if (!file_data) {
        printf("[ELF] Cannot allocate %u bytes\n", file_size);
        fat32_close(f);
        return -1;
    }
    
    /* Read entire file */
    int bytes = fat32_read(f, file_data, file_size);
    fat32_close(f);
    
    if (bytes != (int)file_size) {
        printf("[ELF] Read error: %d/%u\n", bytes, file_size);
        kfree(file_data);
        return -1;
    }
    
    /* Validate ELF */
    if (elf_validate(file_data, file_size) != 0) {
        printf("[ELF] Invalid ELF file\n");
        kfree(file_data);
        return -1;
    }
    
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)file_data;
    const elf32_phdr_t *phdrs = (const elf32_phdr_t *)((uint8_t *)file_data + ehdr->e_phoff);
    
    /* Calculate relocation offset */
    uint32_t process_base = alloc_process_base();
    
    /* Find the lowest virtual address in LOAD segments */
    uint32_t elf_base = 0xFFFFFFFF;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_vaddr < elf_base) {
            elf_base = phdrs[i].p_vaddr;
        }
    }
    
    if (elf_base == 0xFFFFFFFF) {
        printf("[ELF] No LOAD segments\n");
        kfree(file_data);
        return -1;
    }
    
    /* Relocation offset: where we actually load - where ELF expects */
    uint32_t reloc_offset = process_base - elf_base;
    
    image->base_addr = 0xFFFFFFFF;
    image->end_addr = 0;
    
    /* Load each PT_LOAD segment */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdrs[i];
        
        if (ph->p_type != PT_LOAD) continue;
        if (ph->p_memsz == 0) continue;
        
        /* Calculate relocated address */
        uint32_t vaddr = ph->p_vaddr + reloc_offset;
        uint32_t memsz = ph->p_memsz;
        uint32_t filesz = ph->p_filesz;
        
        /* Page flags */
        uint32_t flags = P_PRESENT;
        if (ph->p_flags & PF_W) flags |= P_RW;
        
        /* Map memory for segment */
        if (map_region(vaddr, memsz, flags) != 0) {
            printf("[ELF] Failed to map segment %d\n", i);
            kfree(file_data);
            return -1;
        }
        
        /* Zero the entire region first (for .bss) */
        zero_region(vaddr, memsz);
        
        /* Copy segment data from file */
        if (filesz > 0) {
            uint8_t *src = (uint8_t *)file_data + ph->p_offset;
            uint8_t *dst = (uint8_t *)vaddr;
            for (uint32_t j = 0; j < filesz; j++) {
                dst[j] = src[j];
            }
        }
        
        /* Update image bounds */
        if (vaddr < image->base_addr) {
            image->base_addr = vaddr;
        }
        if (vaddr + memsz > image->end_addr) {
            image->end_addr = vaddr + memsz;
        }
        
        printf("[ELF] Loaded seg %d: 0x%x - 0x%x (%s%s%s)\n",
               i, vaddr, vaddr + memsz,
               (ph->p_flags & PF_R) ? "r" : "-",
               (ph->p_flags & PF_W) ? "w" : "-",
               (ph->p_flags & PF_X) ? "x" : "-");
    }
    
    /* Set entry point (relocated) */
    image->entry_point = ehdr->e_entry + reloc_offset;
    image->brk = image->end_addr;
    image->file_data = file_data;
    image->file_size = file_size;
    
    printf("[ELF] Entry: 0x%x, Base: 0x%x, End: 0x%x\n",
           image->entry_point, image->base_addr, image->end_addr);
    
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->entry_point) return -1;
    
    /* Free file buffer - no longer needed */
    if (image->file_data) {
        kfree(image->file_data);
        image->file_data = NULL;
    }
    
    /* Create task */
    uint32_t tid = task_create((void (*)(void))image->entry_point,
                                "elf_proc",
                                PRIORITY_NORMAL);
    if (!tid) {
        printf("[ELF] Cannot create task\n");
        return -1;
    }
    
    printf("[ELF] Started task %u at 0x%x\n", tid, image->entry_point);
    task_yield();
    
    return 0;
}

void exec_free(exec_image_t *image) {
    if (!image) return;
    
    /* Free file buffer if still allocated */
    if (image->file_data) {
        kfree(image->file_data);
        image->file_data = NULL;
    }
    
    /* Unmap pages */
    if (image->base_addr && image->end_addr > image->base_addr) {
        uint32_t start = image->base_addr & ~(PAGE_SIZE - 1);
        uint32_t end = (image->end_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        
        for (uint32_t addr = start; addr < end; addr += PAGE_SIZE) {
            paging_unmap_page(addr);
        }
    }
    
    memset_s(image, 0, sizeof(exec_image_t));
}