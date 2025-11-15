.section .text
.globl enter_usermode

/* void enter_usermode(uint32_t entry, uint32_t user_stack) */
enter_usermode:
    cli
    
    mov 4(%esp), %ecx   /* entry point */
    mov 8(%esp), %edx   /* user stack */
    
    mov $0x23, %ax      /* User data segment (0x20 | 0x03) */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    
    /* Prepare for iret: SS, ESP, EFLAGS, CS, EIP */
    push $0x23          /* User data segment (SS) */
    push %edx           /* User ESP */
    
    pushf               /* Current EFLAGS */
    pop %eax
    or $0x200, %eax     /* Set IF (interrupts enabled) */
    push %eax           /* Modified EFLAGS */
    
    push $0x1B          /* User code segment (0x18 | 0x03) */
    push %ecx           /* EIP (entry point) */
    
    iret                /* Jump to ring 3 */