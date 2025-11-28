/* PIE ELF linker script - relocatable */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS
{
    /* Start at 0 - will be relocated */
    . = 0;
    
    .text ALIGN(4K) : {
        *(.entry)
        *(.text .text.*)
    }
    
    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    }
    
    .data ALIGN(4K) : {
        *(.data .data.*)
    }
    
    /* Relocation sections - KEEP these! */
    .rel.dyn : {
        *(.rel.dyn)
        *(.rel.text)
        *(.rel.rodata)
        *(.rel.data)
        *(.rel.got)
        *(.rel.plt)
    }
    
    .rel.plt : {
        *(.rel.plt)
    }
    
    .dynamic : {
        *(.dynamic)
    }
    
    .got : {
        *(.got)
    }
    
    .got.plt : {
        *(.got.plt)
    }
    
    .bss ALIGN(4K) : {
        *(.bss .bss.*)
        *(COMMON)
    }
    
    __end = .;
    
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.gnu.hash)
        *(.hash)
        *(.dynsym)
        *(.dynstr)
    }
}
