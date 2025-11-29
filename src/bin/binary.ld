/* PIE ELF linker script - relocatable */
OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* R+X */
    rodata PT_LOAD FLAGS(4); /* R */
    data PT_LOAD FLAGS(6);   /* RW */
    dynamic PT_DYNAMIC;
}

SECTIONS
{
    /* Start at 0 - will be relocated */
    . = 0;
    
    .text ALIGN(4K) : {
        *(.entry)
        *(.text .text.*)
    } :text
    
    .rodata ALIGN(4K) : {
        *(.rodata .rodata.*)
    } :rodata
    
    .data ALIGN(4K) : {
        *(.data .data.*)
    } :data
    
    .dynamic : {
        *(.dynamic)
    } :data :dynamic
    
    /* Relocation sections - KEEP these! */
    .rel.dyn : {
        *(.rel.dyn)
        *(.rel.text)
        *(.rel.rodata)
        *(.rel.data)
        *(.rel.got)
        *(.rel.plt)
    } :data
    
    .rel.plt : {
        *(.rel.plt)
    } :data
    
    .got : {
        *(.got)
    } :data
    
    .got.plt : {
        *(.got.plt)
    } :data
    
    .bss ALIGN(4K) : {
        *(.bss .bss.*)
        *(COMMON)
    } :data
    
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
