#include <stdint.h>
#include <stddef.h>
#include "../irq/io.h"
#include "keyboard.h"
#include "../console.h"
#include "../irq/irq.h"

#define KBD_BUF_SZ 128
#define HISTORY_SIZE 10
#define HISTORY_MAXLEN 128
static char cmd_history[HISTORY_SIZE][HISTORY_MAXLEN];
static int history_count = 0;
static int history_pos = 0;

static volatile int kbuf[KBD_BUF_SZ];
static volatile unsigned khead = 0, ktail = 0;

static inline int buf_empty(void) { return khead == ktail; }
static inline void buf_push(int c) {
    unsigned nxt = (khead + 1) & (KBD_BUF_SZ - 1);
    if (nxt != ktail) { kbuf[khead] = c; khead = nxt; }
}
static inline int buf_pop(void) {
    int c = 0;
    if (!buf_empty()) { c = kbuf[ktail]; ktail = (ktail + 1) & (KBD_BUF_SZ - 1); }
    return c;
}

/* Modifier states */
static volatile uint8_t mod_shift = 0;
static volatile uint8_t mod_ctrl  = 0;
static volatile uint8_t mod_alt   = 0;
static volatile uint8_t mod_caps  = 0;
static volatile uint8_t prefix_e0 = 0;

/* Scancode Set 1 - US QWERTY layout */
static const char sc_to_ascii[128] = {
/*      0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/* 0 */ 0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b', '\t',
/* 1 */ 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',  0,  'a', 's',
/* 2 */ 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',  0, '\\', 'z', 'x', 'c', 'v',
/* 3 */ 'b', 'n', 'm', ',', '.', '/',  0,  '*',  0,  ' ',  0,   0,   0,   0,   0,   0,
/* 4 */  0,   0,   0,   0,   0,   0,   0,  '7', '8', '9', '-', '4', '5', '6', '+', '1',
/* 5 */ '2', '3', '0', '.',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
/* 6 */  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
/* 7 */  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static const char sc_to_ascii_shift[128] = {
/*      0     1     2     3     4     5     6     7     8     9     A     B     C     D     E     F */
/* 0 */ 0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b', '\t',
/* 1 */ 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',  0,  'A', 'S',
/* 2 */ 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  0,  '|', 'Z', 'X', 'C', 'V',
/* 3 */ 'B', 'N', 'M', '<', '>', '?',  0,  '*',  0,  ' ',  0,   0,   0,   0,   0,   0,
/* 4 */  0,   0,   0,   0,   0,   0,   0,  '7', '8', '9', '-', '4', '5', '6', '+', '1',
/* 5 */ '2', '3', '0', '.',  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
/* 6 */  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
/* 7 */  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

/* Scancode to letter index for Alt+letter (returns 0-25 for A-Z, -1 otherwise) */
static int sc_to_letter_index(uint8_t sc) {
    switch (sc) {
        case 0x1E: return 0;   /* A */
        case 0x30: return 1;   /* B */
        case 0x2E: return 2;   /* C */
        case 0x20: return 3;   /* D */
        case 0x12: return 4;   /* E */
        case 0x21: return 5;   /* F */
        case 0x22: return 6;   /* G */
        case 0x23: return 7;   /* H */
        case 0x17: return 8;   /* I */
        case 0x24: return 9;   /* J */
        case 0x25: return 10;  /* K */
        case 0x26: return 11;  /* L */
        case 0x32: return 12;  /* M */
        case 0x31: return 13;  /* N */
        case 0x18: return 14;  /* O */
        case 0x19: return 15;  /* P */
        case 0x10: return 16;  /* Q */
        case 0x13: return 17;  /* R */
        case 0x1F: return 18;  /* S */
        case 0x14: return 19;  /* T */
        case 0x16: return 20;  /* U */
        case 0x2F: return 21;  /* V */
        case 0x11: return 22;  /* W */
        case 0x2D: return 23;  /* X */
        case 0x15: return 24;  /* Y */
        case 0x2C: return 25;  /* Z */
        default:   return -1;
    }
}

static void keyboard_irq(void) {
    uint8_t sc = inb(0x60);
    
    /* Handle E0 prefix (extended keys) */
    if (sc == 0xE0) {
        prefix_e0 = 1;
        return;
    }
    
    int is_release = (sc & 0x80) != 0;
    uint8_t code = sc & 0x7F;
    
    /* ============ EXTENDED KEYS (with E0 prefix) ============ */
    if (prefix_e0) {
        prefix_e0 = 0;
        
        if (is_release) {
            /* Extended modifier releases */
            if (code == 0x38) mod_alt = 0;   /* Right Alt */
            if (code == 0x1D) mod_ctrl = 0;  /* Right Ctrl */
            return;
        }
        
        /* Extended key presses */
        switch (code) {
            case 0x48: buf_push(KEY_UP);     return;
            case 0x50: buf_push(KEY_DOWN);   return;
            case 0x4B: buf_push(KEY_LEFT);   return;
            case 0x4D: buf_push(KEY_RIGHT);  return;
            case 0x47: buf_push(KEY_HOME);   return;
            case 0x4F: buf_push(KEY_END);    return;
            case 0x49: buf_push(KEY_PGUP);   return;
            case 0x51: buf_push(KEY_PGDN);   return;
            case 0x52: buf_push(KEY_INSERT); return;
            case 0x53: buf_push(KEY_DELETE); return;
            case 0x38: mod_alt = 1;          return;  /* Right Alt */
            case 0x1D: mod_ctrl = 1;         return;  /* Right Ctrl */
        }
        return;
    }
    
    /* ============ STANDARD KEYS (no E0 prefix) ============ */
    
    /* Handle modifier key presses and releases */
    switch (code) {
        case 0x2A: /* Left Shift */
        case 0x36: /* Right Shift */
            mod_shift = !is_release;
            return;
        case 0x1D: /* Left Ctrl */
            mod_ctrl = !is_release;
            return;
        case 0x38: /* Left Alt */
            mod_alt = !is_release;
            return;
        case 0x3A: /* Caps Lock - toggle on press only */
            if (!is_release) mod_caps ^= 1;
            return;
    }
    
    /* Ignore releases for non-modifier keys */
    if (is_release) return;
    
    /* ============ KEY PRESS HANDLING ============ */
    
    /* Escape */
    if (code == 0x01) {
        buf_push(KEY_ESC);
        return;
    }
    
    /* F1-F10 (scancodes 0x3B - 0x44) */
    if (code >= 0x3B && code <= 0x44) {
        buf_push(KEY_F1 + (code - 0x3B));
        return;
    }
    
    /* F11, F12 */
    if (code == 0x57) { buf_push(KEY_F11); return; }
    if (code == 0x58) { buf_push(KEY_F12); return; }
    
    /* Alt + Letter combinations */
    if (mod_alt) {
        int letter_idx = sc_to_letter_index(code);
        if (letter_idx >= 0) {
            buf_push(KEY_ALT_A + letter_idx);
            return;
        }
        /* Alt pressed but not a letter - could handle Alt+number here */
        /* For now, just ignore non-letter Alt combinations */
        return;
    }
    
    /* Regular ASCII keys */
    if (code < 128) {
        char c = mod_shift ? sc_to_ascii_shift[code] : sc_to_ascii[code];
        
        /* Apply Caps Lock (only affects letters) */
        if (mod_caps) {
            if (c >= 'a' && c <= 'z') c -= 32;
            else if (c >= 'A' && c <= 'Z') c += 32;
        }
        
        if (c) buf_push(c);
    }
}

void keyboard_init(void) {
    irq_install_handler(1, keyboard_irq);
}

int kbd_getchar(void) {
    while (buf_empty()) {
        __asm__ volatile("sti; hlt");
    }
    return buf_pop();
}

size_t kbd_getline(char* out, size_t maxlen) {
    size_t n = 0;
    if (maxlen == 0) return 0;
    
    int browsing = 0;
    int browse_idx = history_count;
    char temp_buf[HISTORY_MAXLEN];
    memset_s(temp_buf, 0, sizeof(temp_buf));

    for (;;) {
        int c = kbd_getchar();

        /* Arrow UP - previous command */
        if (c == KEY_UP) {
            if (history_count == 0) continue;
            
            if (!browsing) {
                memcpy_s(temp_buf, out, n);
                temp_buf[n] = '\0';
                browsing = 1;
                browse_idx = history_count;
            }
            
            if (browse_idx > 0) {
                browse_idx--;
                
                while (n > 0) {
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                    n--;
                }
                
                const char *hist = cmd_history[browse_idx];
                n = 0;
                while (hist[n] && n < maxlen - 1) {
                    out[n] = hist[n];
                    putchar(hist[n]);
                    n++;
                }
                out[n] = '\0';
            }
            continue;
        }

        /* Arrow DOWN - next command */
        if (c == KEY_DOWN) {
            if (!browsing) continue;
            
            if (browse_idx < history_count - 1) {
                browse_idx++;
                
                while (n > 0) {
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                    n--;
                }
                
                const char *hist = cmd_history[browse_idx];
                n = 0;
                while (hist[n] && n < maxlen - 1) {
                    out[n] = hist[n];
                    putchar(hist[n]);
                    n++;
                }
                out[n] = '\0';
            } else if (browse_idx == history_count - 1) {
                browse_idx = history_count;
                
                while (n > 0) {
                    putchar('\b');
                    putchar(' ');
                    putchar('\b');
                    n--;
                }
                
                n = 0;
                while (temp_buf[n] && n < maxlen - 1) {
                    out[n] = temp_buf[n];
                    putchar(temp_buf[n]);
                    n++;
                }
                out[n] = '\0';
                browsing = 0;
            }
            continue;
        }

        /* Ignore other extended keys */
        if (c >= 0x80) continue;

        /* User typed something - exit browse mode */
        if (browsing && c != '\b' && c != '\n') {
            browsing = 0;
        }

        if (c == '\b') {
            if (n > 0) {
                putchar('\b');
                putchar(' ');
                putchar('\b');
                n--;
            }
            continue;
        }

        putchar(c);

        if (c == '\n') break;

        if (n < maxlen - 1) {
            out[n++] = (char)c;
        } else {
            putchar('\n');
            break;
        }
    }

    out[n] = '\0';
    
    /* Save to history if not empty and different from last */
    if (n > 0) {
        int save = 1;
        if (history_count > 0) {
            if (strcmp_s(out, cmd_history[history_count - 1]) == 0) {
                save = 0;
            }
        }
        
        if (save) {
            int idx = history_count % HISTORY_SIZE;
            memcpy_s(cmd_history[idx], out, n);
            cmd_history[idx][n] = '\0';
            if (history_count < HISTORY_SIZE) {
                history_count++;
            }
        }
    }
    
    return n;
}