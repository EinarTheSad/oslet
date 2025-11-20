.section .text
    .code32
    .globl isr0
    .extern isr_common_stub
    .extern irq_invoke_from_stub
    .extern perform_task_switch

.macro SAVE_REGS
    pushl %eax
    pushl %ecx
    pushl %edx
    pushl %ebx
    pushl %ebp
    pushl %esi
    pushl %edi
.endm

.macro RESTORE_REGS
    popl %edi
    popl %esi
    popl %ebp
    popl %ebx
    popl %edx
    popl %ecx
    popl %eax
.endm

.macro MAKE_ISR_NOERR vec
    .globl isr\vec
isr\vec:
    cli
    SAVE_REGS
    pushl $0
    pushl $\vec
    call isr_common_stub
    addl $8, %esp
    RESTORE_REGS
    sti
    iret
.endm

.macro MAKE_ISR_ERR vec
    .globl isr\vec
isr\vec:
    cli
    SAVE_REGS
    pushl $\vec
    call isr_common_stub
    addl $8, %esp
    RESTORE_REGS
    sti
    iret
.endm

MAKE_ISR_NOERR 0
MAKE_ISR_NOERR 1
MAKE_ISR_NOERR 2
MAKE_ISR_NOERR 3
MAKE_ISR_NOERR 4
MAKE_ISR_NOERR 5
MAKE_ISR_NOERR 6
MAKE_ISR_NOERR 7
MAKE_ISR_ERR 8
MAKE_ISR_NOERR 9
MAKE_ISR_ERR 10
MAKE_ISR_ERR 11
MAKE_ISR_ERR 12
MAKE_ISR_ERR 13
MAKE_ISR_ERR 14
MAKE_ISR_NOERR 15
MAKE_ISR_NOERR 16
MAKE_ISR_NOERR 17
MAKE_ISR_NOERR 18
MAKE_ISR_NOERR 19
MAKE_ISR_NOERR 20
MAKE_ISR_NOERR 21
MAKE_ISR_NOERR 22
MAKE_ISR_NOERR 23
MAKE_ISR_NOERR 24
MAKE_ISR_NOERR 25
MAKE_ISR_NOERR 26
MAKE_ISR_NOERR 27
MAKE_ISR_NOERR 28
MAKE_ISR_NOERR 29
MAKE_ISR_NOERR 30
MAKE_ISR_NOERR 31

.macro MAKE_IRQ vec
    .globl irq\vec
irq\vec:
    cli
    SAVE_REGS
    pushl $(32 + \vec)
    call irq_invoke_from_stub
    addl $4, %esp
    RESTORE_REGS
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

    .extern syscall_handler
    .globl syscall_handler_idt
syscall_handler_idt:
    /* When ring 3 code calls int 0x80, CPU automatically switches stack to kernel stack (via TSS) */
    cli
    /* Save all registers */
    pushl %edi
    pushl %esi
    pushl %ebp
    pushl %esp
    pushl %ebx
    pushl %edx
    pushl %ecx
    pushl %eax
    
    /* Arguments for syscall_handler(eax, ebx, ecx, edx) */
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    call syscall_handler
    addl $16, %esp
    
    /* Save return value */
    movl %eax, (%esp)
    
    /* Restore registers */
    popl %eax
    popl %ecx
    popl %edx
    popl %ebx
    popl %esp
    popl %ebp
    popl %esi
    popl %edi
    
    sti
    
    /* iret will automatically restore ring 3 context including CS with ring bits */
    iret