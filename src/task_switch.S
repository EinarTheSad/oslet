.section .text
    .code32
    .globl perform_task_switch
    .extern switch_task

perform_task_switch:
    pushl %ebp
    pushl %edi
    pushl %esi
    pushl %edx
    pushl %ecx
    pushl %ebx
    pushl %eax
    
    pushfl
    
    movl %esp, %eax
    pushl %eax
    call switch_task
    addl $4, %esp
    
    movl %eax, %esp
    
    popfl
    
    popl %eax
    popl %ebx
    popl %ecx
    popl %edx
    popl %esi
    popl %edi
    popl %ebp
    
    ret