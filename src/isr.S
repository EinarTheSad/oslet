    .section .text
    .code32

    .globl isr0
isr0:
    cli
    pushl $0
    pushl $0
    call isr_common_stub
    addl $8, %esp
    sti
    iret

    .extern irq_invoke_from_stub

    .macro MAKE_IRQ vec
    .globl irq\vec
irq\vec:
    cli
    pushl $0
    pushl $(32+\vec)
    call irq_invoke_from_stub
    addl $8, %esp
    sti
    iret
    .endm

    MAKE_IRQ 0
    MAKE_IRQ 1
    MAKE_IRQ 2
    MAKE_IRQ 3
    MAKE_IRQ 4
    MAKE_IRQ 5
    MAKE_IRQ 6
    MAKE_IRQ 7
    MAKE_IRQ 8
    MAKE_IRQ 9
    MAKE_IRQ 10
    MAKE_IRQ 11
    MAKE_IRQ 12
    MAKE_IRQ 13
    MAKE_IRQ 14
    MAKE_IRQ 15
