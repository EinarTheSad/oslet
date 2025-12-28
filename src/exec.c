#include "exec.h"
#include "drivers/fat32.h"
#include "mem/heap.h"
#include "mem/paging.h"
#include "mem/pmm.h"
#include "console.h"
#include "task.h"

/* ELF Section Header (needed for relocations) */
typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} __attribute__((packed)) elf32_shdr_t;

/* ELF Relocation entry */
typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} __attribute__((packed)) elf32_rel_t;

/* Section types */
#define SHT_REL  9
#define SHT_RELA 4

/* Relocation types for i386 */
#define R_386_NONE     0
#define R_386_32       1   /* Direct 32-bit */
#define R_386_RELATIVE 8   /* Adjust by base */

#define ELF32_R_TYPE(i) ((i) & 0xFF)

/* Process slots */
#define MAX_PROCESS_SLOTS 16
#define SLOT_SIZE         0x00400000  /* 4MB per slot */
#define SLOT_BASE         0x08000000

static uint8_t slot_used[MAX_PROCESS_SLOTS] = {0};

int exec_init(void) {
    memset_s(slot_used, 0, sizeof(slot_used));
    return 0;
}

static int alloc_slot(void) {
    for (int i = 0; i < MAX_PROCESS_SLOTS; i++) {
        if (!slot_used[i]) {
            slot_used[i] = 1;
            return i;
        }
    }
    return -1;
}

static void free_slot(int slot) {
    if (slot >= 0 && slot < MAX_PROCESS_SLOTS) {
        slot_used[slot] = 0;
    }
}

static uint32_t slot_to_base(int slot) {
    return SLOT_BASE + (slot * SLOT_SIZE);
}

static int map_region(uint32_t vaddr, uint32_t size, uint32_t flags) {
    uint32_t aligned_start = vaddr & ~(PAGE_SIZE - 1);
    uint32_t aligned_end = (vaddr + size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    for (uint32_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        if (paging_is_mapped(addr)) continue;
        
        uint32_t phys = pmm_alloc_frame();
        if (!phys) return -1;
        
        if (paging_map_page(addr, phys, flags) != 0) {
            pmm_free_frame(phys);
            return -1;
        }
    }
    return 0;
}

static void unmap_region(uint32_t start, uint32_t end) {
    uint32_t aligned_start = start & ~(PAGE_SIZE - 1);
    uint32_t aligned_end = (end + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    for (uint32_t addr = aligned_start; addr < aligned_end; addr += PAGE_SIZE) {
        paging_unmap_page(addr);
    }
}

int elf_validate(const void *data, uint32_t size) {
    if (size < sizeof(elf32_ehdr_t)) return -1;
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)data;
    
    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC) return -1;
    if (ehdr->e_ident[4] != ELFCLASS32) return -1;
    if (ehdr->e_ident[5] != ELFDATA2LSB) return -1;
    /* Accept both ET_EXEC (2) and ET_DYN (3) for PIE */
    if (ehdr->e_type != 2 && ehdr->e_type != 3) return -1;
    if (ehdr->e_machine != EM_386) return -1;
    
    return 0;
}

/* Apply relocations */
static int apply_relocations(const void *file_data, uint32_t load_base, uint32_t elf_base) {
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)file_data;
    int32_t delta = (int32_t)(load_base - elf_base);
    
    if (delta == 0) return 0;  /* No relocation needed */
    if (ehdr->e_shoff == 0) return 0;  /* No section headers */
    
    const elf32_shdr_t *shdrs = (const elf32_shdr_t *)((uint8_t *)file_data + ehdr->e_shoff);
    
    for (int i = 0; i < ehdr->e_shnum; i++) {
        const elf32_shdr_t *sh = &shdrs[i];
        
        if (sh->sh_type != SHT_REL) continue;
        
        const elf32_rel_t *rels = (const elf32_rel_t *)((uint8_t *)file_data + sh->sh_offset);
        int num_rels = sh->sh_size / sizeof(elf32_rel_t);
        
        for (int j = 0; j < num_rels; j++) {
            uint32_t type = ELF32_R_TYPE(rels[j].r_info);
            uint32_t offset = rels[j].r_offset + delta;  /* Relocated address */
            
            switch (type) {
                case R_386_RELATIVE: {
                    /* *ptr += delta */
                    uint32_t *ptr = (uint32_t *)offset;
                    *ptr += delta;
                    break;
                }
                case R_386_32: {
                    /* *ptr += delta (symbol + addend, but for static we just add delta) */
                    uint32_t *ptr = (uint32_t *)offset;
                    *ptr += delta;
                    break;
                }
                case R_386_NONE:
                    break;
                default:
                    printf("[ELF] Unknown reloc type %u\n", type);
                    break;
            }
        }
    }
    
    return 0;
}

int exec_load(const char *path, exec_image_t *image) {
    if (!path || !image) return -1;
    
    memset_s(image, 0, sizeof(exec_image_t));
    
    fat32_file_t *f = fat32_open(path, "r");
    if (!f) {
        printf("[ELF] Cannot open: %s\n", path);
        return -1;
    }
    
    uint32_t file_size = f->size;
    void *file_data = kmalloc(file_size);
    if (!file_data) {
        fat32_close(f);
        return -1;
    }
    
    int bytes = fat32_read(f, file_data, file_size);
    fat32_close(f);
    
    if (bytes != (int)file_size) {
        kfree(file_data);
        return -1;
    }
    
    if (elf_validate(file_data, file_size) != 0) {
        printf("[ELF] Invalid ELF\n");
        kfree(file_data);
        return -1;
    }
    
    /* Allocate a slot */
    int slot = alloc_slot();
    if (slot < 0) {
        printf("[ELF] No free slots\n");
        kfree(file_data);
        return -1;
    }
    
    uint32_t load_base = slot_to_base(slot);
    
    const elf32_ehdr_t *ehdr = (const elf32_ehdr_t *)file_data;
    const elf32_phdr_t *phdrs = (const elf32_phdr_t *)((uint8_t *)file_data + ehdr->e_phoff);
    
    /* Find ELF's original base address */
    uint32_t elf_base = 0xFFFFFFFF;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD && phdrs[i].p_vaddr < elf_base) {
            elf_base = phdrs[i].p_vaddr;
        }
    }
    
    if (elf_base == 0xFFFFFFFF) elf_base = 0;
    
    int32_t delta = (int32_t)(load_base - elf_base);
    
    image->base_addr = 0xFFFFFFFF;
    image->end_addr = 0;
    
    /* Load segments */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        const elf32_phdr_t *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        
        uint32_t vaddr = ph->p_vaddr + delta;
        uint32_t memsz = ph->p_memsz;
        uint32_t filesz = ph->p_filesz;
        
        uint32_t flags = P_PRESENT;
        if (ph->p_flags & PF_W) flags |= P_RW;
        
        if (map_region(vaddr, memsz, flags) != 0) {
            free_slot(slot);
            kfree(file_data);
            return -1;
        }
        
        memset_s((void *)vaddr, 0, memsz);
        if (filesz > 0) {
            memcpy_s((void *)vaddr, (uint8_t *)file_data + ph->p_offset, filesz);
        }
        
        if (vaddr < image->base_addr) image->base_addr = vaddr;
        if (vaddr + memsz > image->end_addr) image->end_addr = vaddr + memsz;
    }
    
    /* Apply relocations */
    apply_relocations(file_data, load_base, elf_base);
    
    image->entry_point = ehdr->e_entry + delta;
    image->brk = image->end_addr;
    image->file_data = file_data;
    image->file_size = file_size;
    
    /* Store slot in file_size high bits */
    image->file_size = (slot << 24) | (file_size & 0x00FFFFFF);
    
    return 0;
}

int exec_run(exec_image_t *image) {
    if (!image || !image->entry_point) return -1;
    
    if (image->file_data) {
        kfree(image->file_data);
        image->file_data = NULL;
    }
    
    uint32_t tid = task_create((void (*)(void))image->entry_point,
                                "elf_proc",
                                PRIORITY_NORMAL);
    if (!tid) {
        int slot = image->file_size >> 24;
        free_slot(slot);
        return -1;
    }
    
    task_yield();
    return 0;
}

void exec_free(exec_image_t *image) {
    if (!image) return;
    
    if (image->file_data) {
        kfree(image->file_data);
        image->file_data = NULL;
    }
    
    if (image->base_addr && image->end_addr > image->base_addr) {
        unmap_region(image->base_addr, image->end_addr);
    }
    
    /* Free slot */
    int slot = image->file_size >> 24;
    free_slot(slot);
    
    memset_s(image, 0, sizeof(exec_image_t));
}

void exec_cleanup_process(uint32_t base_addr, uint32_t end_addr, int slot) {
    if (base_addr && end_addr > base_addr) {
        unmap_region(base_addr, end_addr);
    }
    free_slot(slot);
}