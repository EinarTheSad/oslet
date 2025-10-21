/* src/boot.S — Multiboot header + entry point, 32-bit GAS syntax */
    .set ALIGN,    1<<0             /* align loaded modules on page boundaries */
    .set MEMINFO,  1<<1             /* provide memory map */
    .set FLAGS,    ALIGN | MEMINFO
    .set MAGIC,    0x1BADB002
    .set CHECKSUM, -(MAGIC + FLAGS)

    .section .multiboot,"a"
    .align 4
    .long MAGIC
    .long FLAGS
    .long CHECKSUM

    .section .text
    .globl start
    .type start, @function
start:
    cli
    mov $stack_top, %esp
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    xor %ebp, %ebp
    call kmain

halt_loop:
    hlt
    jmp halt_loop

    .section .bss
    .align 16
    .lcomm stack, 16384
stack_top:
