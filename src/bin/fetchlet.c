/* Absolute minimum test binary */

void _start(void) {
    /* sys_write("Hello from binary!\n") */
    __asm__ volatile(
        "movl $2, %%eax\n\t"           /* SYS_WRITE */
        "leal message, %%ebx\n\t"       /* address of string */
        "int $0x80\n\t"                 /* syscall */
        
        "movl $1, %%eax\n\t"            /* SYS_EXIT */
        "int $0x80\n\t"                 /* syscall */
        "hlt\n\t"
        
        "message: .ascii \"Hello from binary!\\n\\0\"\n\t"
        ::: "eax", "ebx", "memory"
    );
}