#include <stdint.h>
#include <stddef.h>
#include "../io.h"
#include "keyboard.h"
#include "../console.h"
#include "../irq.h"

/* --- ring buffer for chars produced by IRQ --- */
#define KBD_BUF_SZ 128
static volatile char kbuf[KBD_BUF_SZ];
static volatile unsigned khead = 0, ktail = 0;

static inline int buf_empty(void){ return khead == ktail; }
static inline void buf_push(char c){
    unsigned nxt = (khead + 1) & (KBD_BUF_SZ - 1);
    if (nxt != ktail) { kbuf[khead] = c; khead = nxt; }
    /* else: drop on overflow */
}
static inline char buf_pop(void){
    char c = 0;
    if (!buf_empty()) { c = kbuf[ktail]; ktail = (ktail + 1) & (KBD_BUF_SZ - 1); }
    return c;
}

static uint8_t shift = 0;
static uint8_t caps  = 0;
static uint8_t e0    = 0;

/* US keyboard, set 1, printable subset */
static const char keymap_norm[128] = {
 /*00*/ 0,  27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
 /*10*/ '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n', 0 /*LCtrl*/,
 /*20*/ 'a','s','d','f','g','h','j','k','l',';','\'','`', 0 /*LShift*/,'\\','z','x',
 /*30*/ 'c','v','b','n','m',',','.','/',' ', 0 /*Caps*/, 0, 0, 0, 0, 0, 0,
 /*40*/ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
 /*50*/ 0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
 /*60..7F unused here*/ 
};
static const char keymap_shift[128] = {
 /*00*/ 0,  27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
 /*10*/ '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n', 0,
 /*20*/ 'A','S','D','F','G','H','J','K','L',':','"','~', 0,'|','Z','X',
 /*30*/ 'C','V','B','N','M','<','>','?',' ', 0, 0, 0, 0, 0, 0, 0,
};

/* apply Caps only to letters */
static inline char apply_caps(char c){
    if (!c) return c;
    if (c >= 'a' && c <= 'z') return c - 32;     /* to upper */
    if (c >= 'A' && c <= 'Z') return c + 32;     /* to lower */
    return c;
}

static void keyboard_irq(void) {
    uint8_t sc = inb(0x60);

    /* handle extended prefix */
    if (sc == 0xE0) { e0 = 1; return; }

    int release = sc & 0x80;
    uint8_t code = sc & 0x7F;

    /* modifiers */
    if (!e0) {
        if (code == 0x2A || code == 0x36) { /* LShift/RShift */
            if (release) shift = 0; else shift = 1;
            e0 = 0; return;
        }
        if (code == 0x3A && !release) {     /* CapsLock */
            caps ^= 1;
            e0 = 0; return;
        }
        if (code == 0x1D) {                 /* Ctrl (ignore for now) */
            e0 = 0; return;
        }
    }

    e0 = 0;

    if (release) return;                    /* ignore key releases */

    char c = 0;

    if (code == 0x39 && !release) {
        c = ' ';
    } else {
        c = shift ? keymap_shift[code] : keymap_norm[code];
        if (!shift && caps) {
            if (c >= 'a' && c <= 'z') c -= 32;
           else if (c >= 'A' && c <= 'Z') c += 32;
        }
    }

    if (c) buf_push(c);
}

/* public API */
void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq);
}

char kbd_getchar(void) {
    while (buf_empty()) __asm__ volatile("hlt");
    return buf_pop();
}

/* crude line editor: backspace editing, stops on '\n' or buffer full-1, echoes */
size_t kbd_getline(char* out, size_t maxlen) {
    size_t n = 0;
    if (maxlen == 0) return 0;

    for (;;) {
        char c = kbd_getchar();

        if (c == '\b') {
            if (n > 0) {
                /* erase on screen */
                putchar('\b'); putchar(' '); putchar('\b');
                n--;
            }
            continue;
        }

        putchar(c);  /* echo */

        if (c == '\n') {
            break;
        }

        if (n < maxlen - 1) {
            out[n++] = c;
        } else {
            /* buffer full: finish line visually and break */
            putchar('\n');
            break;
        }
    }

    out[n] = 0; /* NUL-terminate for convenience */
    return n;
}
