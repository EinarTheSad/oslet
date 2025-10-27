    .code32
    .section .multiboot
    .align 4

    .set MBOOT_MAGIC,     0x1BADB002
    .set MBOOT_FLAGS,     0x00000003
    .set MBOOT_CHECKSUM, -(MBOOT_MAGIC + MBOOT_FLAGS)

    .global multiboot_header
multiboot_header:
    .long MBOOT_MAGIC
    .long MBOOT_FLAGS
    .long MBOOT_CHECKSUM

    .section .rodata
    .align 8
gdt_start:
gdt_null:   .quad 0

gdt_code:
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x9A
    .byte 0xCF
    .byte 0x00

gdt_data:
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x92
    .byte 0xCF
    .byte 0x00

gdt_end:

    .align 4
gdtr:
    .word gdt_end - gdt_start - 1
    .long gdt_start

    .equ SEL_CODE, 0x08
    .equ SEL_DATA, 0x10

    .section .bss
    .align 16
stack_bottom:
    .skip 16 * 1024
stack_top:

    .section .text
    .globl start
    .type  start,@function
    .extern kmain

start:
    cli

    lgdt gdtr
    nop

    mov  $SEL_DATA, %eax
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs
    mov  %ax, %ss

    mov  $stack_top, %esp
    and  $~0xF, %esp
    sub  $8, %esp

    cld

    ljmp $SEL_CODE, $1f
    
1:
    call kmain

.hang:
    hlt
    jmp .hang
