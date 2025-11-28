#pragma once
#include <stdint.h>
#include <stddef.h>

/* ELF Magic */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" little-endian */

/* ELF Class */
#define ELFCLASS32 1

/* ELF Data Encoding */
#define ELFDATA2LSB 1  /* Little-endian */

/* ELF Type */
#define ET_EXEC 2  /* Executable */

/* ELF Machine */
#define EM_386 3  /* i386 */

/* Program Header Types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4

/* Program Header Flags */
#define PF_X 0x1  /* Execute */
#define PF_W 0x2  /* Write */
#define PF_R 0x4  /* Read */

/* ELF32 Header */
typedef struct {
    uint8_t  e_ident[16];   /* ELF identification */
    uint16_t e_type;        /* Object file type */
    uint16_t e_machine;     /* Machine type */
    uint32_t e_version;     /* Object file version */
    uint32_t e_entry;       /* Entry point address */
    uint32_t e_phoff;       /* Program header offset */
    uint32_t e_shoff;       /* Section header offset */
    uint32_t e_flags;       /* Processor-specific flags */
    uint16_t e_ehsize;      /* ELF header size */
    uint16_t e_phentsize;   /* Size of program header entry */
    uint16_t e_phnum;       /* Number of program header entries */
    uint16_t e_shentsize;   /* Size of section header entry */
    uint16_t e_shnum;       /* Number of section header entries */
    uint16_t e_shstrndx;    /* Section name string table index */
} __attribute__((packed)) elf32_ehdr_t;

/* ELF32 Program Header */
typedef struct {
    uint32_t p_type;    /* Segment type */
    uint32_t p_offset;  /* Segment file offset */
    uint32_t p_vaddr;   /* Segment virtual address */
    uint32_t p_paddr;   /* Segment physical address */
    uint32_t p_filesz;  /* Segment size in file */
    uint32_t p_memsz;   /* Segment size in memory */
    uint32_t p_flags;   /* Segment flags */
    uint32_t p_align;   /* Segment alignment */
} __attribute__((packed)) elf32_phdr_t;

/* Base address for dynamically allocated processes */
#define EXEC_BASE_ADDR   0x08000000  /* Standard Linux user-space base */
#define EXEC_MAX_SIZE    0x00800000  /* 8MB max per process */

/* Image structure - extended for ELF */
typedef struct {
    uint32_t entry_point;      /* Entry point from ELF header */
    uint32_t base_addr;        /* Lowest loaded virtual address */
    uint32_t end_addr;         /* Highest loaded virtual address */
    uint32_t brk;              /* Program break (end of data segment) */
    void    *file_data;        /* Raw file data (temporary) */
    uint32_t file_size;        /* Size of raw file */
} exec_image_t;

/* Process address space manager */
typedef struct {
    uint32_t base;       /* Base virtual address */
    uint32_t size;       /* Total allocated size */
    uint32_t next_slot;  /* Next available slot number */
} exec_allocator_t;

/* API */
int exec_init(void);
int exec_load(const char *path, exec_image_t *image);
int exec_run(exec_image_t *image);
void exec_free(exec_image_t *image);

/* Utility */
int elf_validate(const void *data, uint32_t size);