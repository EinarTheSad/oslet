    .section .text
    .code32
    .globl isr0
    .extern isr_common_stub
    .extern irq_invoke_from_stub

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

/* For exceptions that do not automatically push an error code, push a dummy 0 so the common
   handler always sees [vector, error_code] on the stack in the same layout.
   For exceptions that do push an error code, the CPU already placed it on the stack; in that
   case we only push the vector. */

/* exceptions that push an error code are: 8, 10, 11, 12, 13, 14 */

.macro MAKE_ISR_NOERR vec
    .globl isr\vec
isr\vec:
    cli
    SAVE_REGS
    pushl $0              /* dummy error code */
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
    addl $4, %esp
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

/* For IRQs push the vector as the single argument to irq_invoke_from_stub and preserve regs */

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