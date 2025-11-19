#include "../syscall.h"
#include "../lib/stdio.h"
#include "../lib/string.h"

void _start(void) {
    sys_setcolor(0, 14);
    printf("=== Syscall Test Suite ===\n\n");
    
    /* Test 1: Console I/O */
    sys_setcolor(0, 11);
    printf("Test 1: Console I/O\n");
    sys_setcolor(0, 7);
    printf("  Writing text... ");
    sys_write("OK\n");
    
    /* Test 2: Process info */
    sys_setcolor(0, 11);
    printf("Test 2: Process Info\n");
    sys_setcolor(0, 7);
    uint32_t pid = sys_getpid();
    printf("  PID: %u\n", pid);
    
    /* Test 3: File operations */
    sys_setcolor(0, 11);
    printf("Test 3: File I/O\n");
    sys_setcolor(0, 7);
    
    int fd = sys_open("test.txt", "w");
    if (fd >= 0) {
        printf("  Open: OK (fd=%d)\n", fd);
        
        const char *data = "Hello from syscall!\n";
        int written = sys_write_file(fd, data, strlen(data));
        printf("  Write: %d bytes\n", written);
        
        sys_close(fd);
        printf("  Close: OK\n");
    } else {
        printf("  Open: FAILED\n");
    }
    
    /* Test 4: Directory operations */
    sys_setcolor(0, 11);
    printf("Test 4: Directory\n");
    sys_setcolor(0, 7);
    
    char cwd[64];
    sys_getcwd(cwd, sizeof(cwd));
    printf("  CWD: %s\n", cwd);
    
    /* Test 5: Memory info */
    sys_setcolor(0, 11);
    printf("Test 5: System Info\n");
    sys_setcolor(0, 7);
    
    sys_meminfo_t meminfo;
    if (sys_get_meminfo(&meminfo) == 0) {
        printf("  Total: %u KB\n", meminfo.total_kb);
        printf("  Free:  %u KB\n", meminfo.free_kb);
        printf("  Used:  %u KB\n", meminfo.used_kb);
    }
    
    /* Test 6: Timing */
    sys_setcolor(0, 11);
    printf("Test 6: Timing\n");
    sys_setcolor(0, 7);
    
    uint32_t ticks = sys_uptime();
    printf("  Uptime: %u ticks\n", ticks);
    
    sys_setcolor(0, 10);
    printf("\n=== All Tests Complete ===\n");
    
    sys_exit();
}