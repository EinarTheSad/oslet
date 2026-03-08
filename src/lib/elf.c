#include "elf.h"
#include "string.h"
#include "stdlib.h"
#include "../syscall.h"

int elf_is_textmode(const char *path) {
    /* if path is missing or unreadable we default to text mode */
    if (!path) return 0;

    int fd = sys_open(path, "r");
    if (fd < 0) return 0;

    /* look for the graphical marker; absence means text mode */
    char pattern[10];
    int patlen = 8; /* length of "MODE=GIX" */
    pattern[0] = 'M'; pattern[1] = 'O'; pattern[2] = 'D'; pattern[3] = 'E';
    pattern[4] = '='; pattern[5] = 'G'; pattern[6] = 'I'; pattern[7] = 'X';
    pattern[8] = '\0';

    int match = 0;
    char buf[256];
    int n;
    while ((n = sys_read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; i++) {
            if (buf[i] == pattern[match]) {
                match++;
                if (match == patlen) {
                    sys_close(fd);
                    return 1; /* graphical mode */
                }
            } else {
                if (buf[i] == pattern[0])
                    match = 1;
                else
                    match = 0;
            }
        }
    }

    sys_close(fd);
    return 0; /* assume text mode */
}
