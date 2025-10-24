OUTPUT_FORMAT(elf32-i386)
ENTRY(start)

SECTIONS
{
  . = 0x00100000;

  __kernel_start = .;

  /* Put Multiboot header first in .text so it lands in the first PT_LOAD. */
  .text ALIGN(4096) : {
    KEEP(*(.multiboot))
    *(.text .text.*)
  }

  .rodata ALIGN(4096) : {
    *(.rodata .rodata.*)
  }

  .data ALIGN(4096) : {
    *(.data .data.*)
    *(.init_array .init_array.*)
    *(.fini_array .fini_array.*)
    *(.ctors .ctors.*)
    *(.dtors .dtors.*)
  }

  .bss ALIGN(4096) : {
    __bss_start = .;
    *(.bss .bss.*)
    *(COMMON)
    __bss_end = .;
  }

  __kernel_end = .;
}
