.section .rodata.vbe_thunk,"a"
.code32
.global vbe_thunk_start
.global vbe_thunk_end
.global vbe_thunk_state
.global vbe_thunk_gdt
.global vbe_thunk_gdtr
.global vbe_thunk_real_pointer
.global vbe_thunk_protected_pointer
.global vbe_thunk_protected32
.global vbe_thunk_physical

.set IN_AX, 0
.set IN_BX, 2
.set IN_CX, 4
.set IN_DX, 6
.set IN_SI, 8
.set IN_DI, 10
.set IN_ES, 12
.set OUT_AX, 14
.set OUT_BX, 16
.set OUT_CX, 18
.set OUT_DX, 20
.set OUT_SI, 22
.set OUT_DI, 24
.set OUT_ES, 26
.set SAVED_ESP, 28
.set SAVED_CR0, 32
.set SAVED_CR3, 36
.set SAVED_GDTR, 40
.set SAVED_IDTR, 46
.set SAVED_DS, 52
.set SAVED_ES, 54
.set SAVED_FS, 56
.set SAVED_GS, 58
.set SAVED_SS, 60

vbe_thunk_start:
    pushal
    pushfl
    call 1f
1:  popl %ebx
    subl $(1b - vbe_thunk_start), %ebx
    movl %esp, vbe_thunk_state - vbe_thunk_start + SAVED_ESP(%ebx)
    movl %cr0, %eax
    movl %eax, vbe_thunk_state - vbe_thunk_start + SAVED_CR0(%ebx)
    movl %cr3, %eax
    movl %eax, vbe_thunk_state - vbe_thunk_start + SAVED_CR3(%ebx)
    sgdt vbe_thunk_state - vbe_thunk_start + SAVED_GDTR(%ebx)
    sidt vbe_thunk_state - vbe_thunk_start + SAVED_IDTR(%ebx)
    movw %ds, vbe_thunk_state - vbe_thunk_start + SAVED_DS(%ebx)
    movw %es, vbe_thunk_state - vbe_thunk_start + SAVED_ES(%ebx)
    movw %fs, vbe_thunk_state - vbe_thunk_start + SAVED_FS(%ebx)
    movw %gs, vbe_thunk_state - vbe_thunk_start + SAVED_GS(%ebx)
    movw %ss, vbe_thunk_state - vbe_thunk_start + SAVED_SS(%ebx)
    lgdt vbe_thunk_gdtr - vbe_thunk_start(%ebx)
    ljmp *protected16_pointer - vbe_thunk_start(%ebx)

.code16
protected16:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl $0x7ff0, %esp
    movl %cr0, %eax
    andl $0x7fffffff, %eax
    movl %eax, %cr0
    andl $0xfffffffe, %eax
    movl %eax, %cr0
    ljmp *vbe_thunk_real_pointer - vbe_thunk_start

real16:
    movw %cs, %ax
    movw %ax, %ds
    movw %ax, %ss
    movw $0x7ff0, %sp
    lidt real_idtr - vbe_thunk_start
    movw vbe_thunk_state - vbe_thunk_start + IN_BX, %bx
    movw vbe_thunk_state - vbe_thunk_start + IN_CX, %cx
    movw vbe_thunk_state - vbe_thunk_start + IN_DX, %dx
    movw vbe_thunk_state - vbe_thunk_start + IN_SI, %si
    movw vbe_thunk_state - vbe_thunk_start + IN_DI, %di
    movw vbe_thunk_state - vbe_thunk_start + IN_ES, %ax
    movw %ax, %es
    movw vbe_thunk_state - vbe_thunk_start + IN_AX, %ax
    sti
    int $0x10
    cli
    pushw %ax
    movw %cs, %ax
    movw %ax, %ds
    popw %ax
    movw %ax, vbe_thunk_state - vbe_thunk_start + OUT_AX
    movw %bx, vbe_thunk_state - vbe_thunk_start + OUT_BX
    movw %cx, vbe_thunk_state - vbe_thunk_start + OUT_CX
    movw %dx, vbe_thunk_state - vbe_thunk_start + OUT_DX
    movw %si, vbe_thunk_state - vbe_thunk_start + OUT_SI
    movw %di, vbe_thunk_state - vbe_thunk_start + OUT_DI
    movw %es, vbe_thunk_state - vbe_thunk_start + OUT_ES

    call check_a20
    jnz a20_ready
    movw $0x2401, %ax
    int $0x15
    cli
    movw %cs, %ax
    movw %ax, %ds
    call check_a20
    jnz a20_ready
    inb $0x92, %al
    orb $2, %al
    andb $0xfe, %al
    outb %al, $0x92
    call check_a20
    jnz a20_ready
    movw $a20_error - vbe_thunk_start, %si
    movw $0x3f8, %dx
2:  lodsb
    testb %al, %al
    jz 3f
    outb %al, %dx
    jmp 2b
3:  cli
    hlt
    jmp 3b

a20_ready:
    lgdt vbe_thunk_gdtr - vbe_thunk_start
    movl vbe_thunk_physical - vbe_thunk_start, %ebx
    movl %cr0, %eax
    orl $1, %eax
    movl %eax, %cr0
    ljmpl *vbe_thunk_protected_pointer - vbe_thunk_start

check_a20:
    pushw %ds
    pushw %es
    xorw %ax, %ax
    movw %ax, %ds
    decw %ax
    movw %ax, %es
    movb 0x500, %al
    movb %es:0x510, %ah
    pushw %ax
    movb $0, 0x500
    movb $0xff, %es:0x510
    cmpb $0xff, 0x500
    popw %ax
    movb %ah, %es:0x510
    movb %al, 0x500
    popw %es
    popw %ds
    ret

.code32
vbe_thunk_protected32:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs
    movl vbe_thunk_state - vbe_thunk_start + SAVED_CR3(%ebx), %eax
    movl %eax, %cr3
    movl vbe_thunk_state - vbe_thunk_start + SAVED_CR0(%ebx), %eax
    movl %eax, %cr0
    lgdt vbe_thunk_state - vbe_thunk_start + SAVED_GDTR(%ebx)
    lidt vbe_thunk_state - vbe_thunk_start + SAVED_IDTR(%ebx)
    movw vbe_thunk_state - vbe_thunk_start + SAVED_ES(%ebx), %ax
    movw %ax, %es
    movw vbe_thunk_state - vbe_thunk_start + SAVED_FS(%ebx), %ax
    movw %ax, %fs
    movw vbe_thunk_state - vbe_thunk_start + SAVED_GS(%ebx), %ax
    movw %ax, %gs
    movw vbe_thunk_state - vbe_thunk_start + SAVED_SS(%ebx), %ax
    movw %ax, %ss
    movl vbe_thunk_state - vbe_thunk_start + SAVED_ESP(%ebx), %esp
    movw vbe_thunk_state - vbe_thunk_start + SAVED_DS(%ebx), %ax
    movw %ax, %ds
    popfl
    popal
    ret

.align 8
vbe_thunk_gdt:
.quad 0
.quad 0x00cf9a000000ffff
.quad 0x00cf92000000ffff
.quad 0x00009a000000ffff
.quad 0x000092000000ffff
vbe_thunk_gdt_end:
vbe_thunk_gdtr:
.word vbe_thunk_gdt_end - vbe_thunk_gdt - 1
.long 0
protected16_pointer:
.long protected16 - vbe_thunk_start
.word 0x18
vbe_thunk_real_pointer:
.word real16 - vbe_thunk_start
.word 0
vbe_thunk_protected_pointer:
.long 0
.word 0x08
real_idtr:
.word 0x3ff
.long 0
vbe_thunk_physical: .long 0
a20_error: .asciz "VBE: A20 unavailable\n"
.align 4
vbe_thunk_state:
.skip 62
vbe_thunk_end:
.section .note.GNU-stack,"",@progbits
