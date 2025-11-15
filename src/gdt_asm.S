.section .text
.globl gdt_flush
.globl tss_flush

gdt_flush:
    mov 4(%esp), %eax
    lgdt (%eax)
    
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    
    ljmp $0x08, $flush_cs
flush_cs:
    ret

tss_flush:
    mov $0x28, %ax
    ltr %ax
    ret