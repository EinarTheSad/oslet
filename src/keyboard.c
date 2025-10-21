#include <stdint.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static char scancode_to_ascii(uint8_t sc) {
    /* tiny map for letters, numbers, ENTER, space */
    static const char map[128] = {
        /* 0x00..0x0f */ 0, 27, '1','2','3','4','5','6','7','8','9','0','-','=', '\b',
        /* 0x10..0x1f */ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0,
        /* 0x20..0x2f */ 'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\','z','x',
        /* 0x30..0x3f */ 'c','v','b','n','m',',','.','/',' ', 0,0,0,0,0,0,0
    };
    if (sc < 128) return map[sc];
    return 0;
}

int kb_wait_and_echo(void) {
    while (1) {
        uint8_t status = inb(0x64);
        if (status & 1) {
            uint8_t sc = inb(0x60);
            char c = scancode_to_ascii(sc);
            if (c) {
                extern void putchar(char);
                putchar(c);
                return 0;
            }
        }
    }
    return -1;
}
