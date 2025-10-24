    .code32
    .section .multiboot
    .align 4

/* Multiboot v1 header: must live in the first 8 KiB of the file */
    .set MBOOT_MAGIC,     0x1BADB002
    .set MBOOT_FLAGS,     0x00000003        /* align + mem info */
    .set MBOOT_CHECKSUM, -(MBOOT_MAGIC + MBOOT_FLAGS)

    .globl multiboot_header
multiboot_header:
    .long MBOOT_MAGIC
    .long MBOOT_FLAGS
    .long MBOOT_CHECKSUM

/* -------- GDT: null, 32-bit code, 32-bit data -------- */
    .section .rodata
    .align 8
gdt_start:
gdt_null:   .quad 0

/* 0x08: base=0, limit=4GiB-1, code=0x9A, gran=0xCF */
gdt_code:
    .word 0xFFFF          /* limit 15:0  */
    .word 0x0000          /* base  15:0  */
    .byte 0x00            /* base  23:16 */
    .byte 0x9A            /* access: present|ring0|code|readable */
    .byte 0xCF            /* gran: 4K|32b + limit 19:16 */
    .byte 0x00            /* base  31:24 */

 /* 0x10: base=0, limit=4GiB-1, data=0x92, gran=0xCF */
gdt_data:
    .word 0xFFFF
    .word 0x0000
    .byte 0x00
    .byte 0x92            /* access: present|ring0|data|writable */
    .byte 0xCF
    .byte 0x00

gdt_end:

    .align 4
gdtr:
    .word gdt_end - gdt_start - 1   /* limit */
    .long gdt_start                 /* base  */

    .equ SEL_CODE, 0x08
    .equ SEL_DATA, 0x10

    .section .bss
    .align 16
stack_bottom:
    .skip 16 * 1024                 /* 16 KiB */
stack_top:

/* -------- Entry point -------- */
    .section .text
    .globl start
    .type  start,@function
    .extern kmain

start:
    cli

    lgdt gdtr
    nop

    /* Set data segments to the data descriptor */
    mov  $SEL_DATA, %eax
    mov  %ax, %ds
    mov  %ax, %es
    mov  %ax, %fs
    mov  %ax, %gs
    mov  %ax, %ss

    /* Set up a 16-byte aligned stack and keep ABI alignment across 'call' */
    mov  $stack_top, %esp
    and  $~0xF, %esp                /* align down to 16 */
    sub  $8, %esp                   /* compensate for return addr, keep 16B */

    cld

    ljmp $SEL_CODE, $1f
    
1:
    call kmain

.hang:
    hlt
    jmp .hang
