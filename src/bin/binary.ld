/* ELF executable linker script for user programs */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

/* Base address - will be relocated by loader */
PROVIDE(__base = 0x08000000);

SECTIONS
{
    . = __base;
    
    /* Code section */
    .text ALIGN(4K) : {
        __text_start = .;
        *(.entry)
        *(.text .text.*)
        __text_end = .;
    }
    
    /* Read-only data */
    .rodata ALIGN(4K) : {
        __rodata_start = .;
        *(.rodata .rodata.*)
        __rodata_end = .;
    }
    
    /* Initialized data */
    .data ALIGN(4K) : {
        __data_start = .;
        *(.data .data.*)
        __data_end = .;
    }
    
    /* Uninitialized data (zeroed by loader) */
    .bss ALIGN(4K) : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }
    
    __end = .;
    
    /DISCARD/ : {
        *(.comment)
        *(.eh_frame)
        *(.note*)
        *(.gnu.hash)
    }
}
