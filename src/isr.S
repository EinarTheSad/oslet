    .section .text
    .code32
    .global isr0

isr0:
    cli
    pushl $0             /* fake error code */
    pushl $0             /* vector number */
    call isr_common_stub
    addl $8, %esp
    sti
    iret
