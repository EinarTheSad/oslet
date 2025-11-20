.section .text
.globl enter_usermode

/* void enter_usermode(uint32_t entry, uint32_t user_stack) */
enter_usermode:
    cli
    
    mov 4(%esp), %ecx   /* entry point */
    mov 8(%esp), %edx   /* user stack */
    
    /* Set data segments to USER data (0x23 = 0x20 | 0x3 for ring 3) */
    mov $0x23, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    /* Prepare stack for iret to ring 3 */
    /* Stack order (bottom to top): SS, ESP, EFLAGS, CS, EIP */
    
    push $0x23          /* User data segment (SS) - ring 3 */
    push %edx           /* User stack pointer (ESP) */
    
    /* Load EFLAGS, set IF bit to enable interrupts in ring 3 */
    pushf
    pop %eax
    or $0x200, %eax     /* Set IF flag */
    push %eax
    
    push $0x1B          /* User code segment (0x18 | 0x3 for ring 3) (CS) */
    push %ecx           /* Entry point (EIP) */
    
    sti
    iret                /* Jump to ring 3 with proper privilege level */