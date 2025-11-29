#include <stdint.h>
#include <stddef.h>
#include "../io.h"
#include "keyboard.h"
#include "../console.h"
#include "../irq.h"

#define KBD_BUF_SZ 128
#define HISTORY_SIZE 16
#define HISTORY_LINE_MAX 128

/* --- ring buffer for chars produced by IRQ --- */
static volatile char kbuf[KBD_BUF_SZ];
static volatile unsigned khead = 0, ktail = 0;

/* --- command history --- */
static char history[HISTORY_SIZE][HISTORY_LINE_MAX];
static int history_count = 0;
static int history_pos = 0;

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

    /* Extended arrow keys */
    if (e0) {
        e0 = 0;
        if (release) return;
        
        switch(code) {
            case 0x48: buf_push(0x80); return; /* Up */
            case 0x50: buf_push(0x81); return; /* Down */
            case 0x4B: buf_push(0x82); return; /* Left */
            case 0x4D: buf_push(0x83); return; /* Right */
            case 0x53: buf_push(0x84); return; /* Delete */
            default: return;
        }
    }

    /* modifiers */
    if (code == 0x2A || code == 0x36) { /* LShift/RShift */
        if (release) shift = 0; else shift = 1;
        return;
    }
    if (code == 0x3A && !release) {     /* CapsLock */
        caps ^= 1;
        return;
    }
    if (code == 0x1D) {                 /* Ctrl (ignore for now) */
        return;
    }

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
    while (buf_empty()) __asm__ volatile("sti; hlt");
    return buf_pop();
}

/* History management */
static void history_add(const char *line) {
    if (!line || line[0] == '\0') return;
    
    /* Don't add if same as last command */
    if (history_count > 0) {
        int last_idx = (history_count - 1) % HISTORY_SIZE;
        if (strcmp_s(history[last_idx], line) == 0) return;
    }
    
    int idx = history_count % HISTORY_SIZE;
    size_t len = 0;
    while (line[len] && len < HISTORY_LINE_MAX - 1) {
        history[idx][len] = line[len];
        len++;
    }
    history[idx][len] = '\0';
    history_count++;
}

static const char* history_get(int offset) {
    if (history_count == 0) return NULL;
    if (offset < 0 || offset >= history_count) return NULL;
    if (offset >= HISTORY_SIZE) return NULL;
    
    int actual_count = history_count < HISTORY_SIZE ? history_count : HISTORY_SIZE;
    if (offset >= actual_count) return NULL;
    
    int idx = (history_count - 1 - offset) % HISTORY_SIZE;
    if (idx < 0) idx += HISTORY_SIZE;
    
    return history[idx];
}

/* Advanced line editor with cursor movement, insert/delete, and history */
size_t kbd_getline(char* buf, size_t maxlen) {
    extern void vga_get_cursor(int *x, int *y);
    extern void vga_set_cursor(int x, int y);
    
    size_t len = 0;
    size_t cursor_pos = 0;
    int start_x, start_y;
    int history_offset = -1;
    char temp_buf[HISTORY_LINE_MAX] = {0};
    
    vga_get_cursor(&start_x, &start_y);
    buf[0] = '\0';
    
    while (1) {
        unsigned char ch = (unsigned char)kbd_getchar();
        
        if (ch == '\n' || ch == '\r') {
            buf[len] = '\0';
            putchar('\n');
            
            /* Add to history if not empty */
            if (len > 0) {
                history_add(buf);
            }
            
            return len;
        }
        
        if (ch == KEY_UP) {
            /* Navigate history backwards */
            history_offset++;
            const char *hist = history_get(history_offset);
            
            if (hist) {
                /* Save current line to temp if first time */
                if (history_offset == 0) {
                    for (size_t i = 0; i < len && i < HISTORY_LINE_MAX - 1; i++) {
                        temp_buf[i] = buf[i];
                    }
                    temp_buf[len] = '\0';
                }
                
                /* Load history */
                len = 0;
                while (hist[len] && len < maxlen - 1) {
                    buf[len] = hist[len];
                    len++;
                }
                buf[len] = '\0';
                cursor_pos = len;
                
                /* Redraw */
                vga_set_cursor(start_x, start_y);
                for (size_t i = 0; i < len; i++) {
                    putchar(buf[i]);
                }
                /* Clear rest of line */
                for (int i = 0; i < 60; i++) putchar(' ');
                vga_set_cursor(start_x + cursor_pos, start_y);
            } else {
                history_offset--;
            }
            continue;
        }
        
        if (ch == KEY_DOWN) {
            /* Navigate history forwards */
            if (history_offset > 0) {
                history_offset--;
                const char *hist = history_get(history_offset);
                
                if (hist) {
                    len = 0;
                    while (hist[len] && len < maxlen - 1) {
                        buf[len] = hist[len];
                        len++;
                    }
                    buf[len] = '\0';
                } else {
                    len = 0;
                    buf[0] = '\0';
                }
                
                cursor_pos = len;
                
                vga_set_cursor(start_x, start_y);
                for (size_t i = 0; i < len; i++) {
                    putchar(buf[i]);
                }
                for (int i = 0; i < 60; i++) putchar(' ');
                vga_set_cursor(start_x + cursor_pos, start_y);
            } else if (history_offset == 0) {
                /* Restore temp buffer */
                history_offset = -1;
                len = 0;
                while (temp_buf[len] && len < maxlen - 1) {
                    buf[len] = temp_buf[len];
                    len++;
                }
                buf[len] = '\0';
                cursor_pos = len;
                
                vga_set_cursor(start_x, start_y);
                for (size_t i = 0; i < len; i++) {
                    putchar(buf[i]);
                }
                for (int i = 0; i < 60; i++) putchar(' ');
                vga_set_cursor(start_x + cursor_pos, start_y);
            }
            continue;
        }
        
        if (ch == KEY_LEFT) {
            if (cursor_pos > 0) {
                cursor_pos--;
                vga_set_cursor(start_x + cursor_pos, start_y);
            }
            continue;
        }
        
        if (ch == KEY_RIGHT) {
            if (cursor_pos < len) {
                cursor_pos++;
                vga_set_cursor(start_x + cursor_pos, start_y);
            }
            continue;
        }
        
        if (ch == KEY_DELETE) {
            /* Any edit resets history navigation */
            history_offset = -1;
            
            /* Delete character at cursor position (not before it like backspace) */
            if (cursor_pos < len) {
                /* Remove character at cursor_pos */
                for (size_t i = cursor_pos; i < len - 1; i++) {
                    buf[i] = buf[i + 1];
                }
                len--;
                buf[len] = '\0';
                
                /* Redraw entire line */
                vga_set_cursor(start_x, start_y);
                for (size_t i = 0; i < len; i++) {
                    putchar(buf[i]);
                }
                putchar(' ');
                vga_set_cursor(start_x + cursor_pos, start_y);
            }
            continue;
        }
        
        if (ch >= 0x80) {
            continue;
        }
        
        if (ch == '\b') {
            /* Any edit resets history navigation */
            history_offset = -1;
            
            if (cursor_pos > 0) {
                /* Remove character at cursor_pos - 1 */
                for (size_t i = cursor_pos - 1; i < len - 1; i++) {
                    buf[i] = buf[i + 1];
                }
                len--;
                cursor_pos--;
                buf[len] = '\0';
                
                /* Redraw entire line */
                vga_set_cursor(start_x, start_y);
                for (size_t i = 0; i < len; i++) {
                    putchar(buf[i]);
                }
                putchar(' ');
                vga_set_cursor(start_x + cursor_pos, start_y);
            }
            continue;
        }
        
        if (ch >= 32 && ch < 127 && len < maxlen - 1) {
            /* Any edit resets history navigation */
            history_offset = -1;
            
            /* Insert character at cursor position */
            for (size_t i = len; i > cursor_pos; i--) {
                buf[i] = buf[i - 1];
            }
            buf[cursor_pos] = (char)ch;
            cursor_pos++;
            len++;
            buf[len] = '\0';
            
            /* Redraw from cursor position */
            vga_set_cursor(start_x, start_y);
            for (size_t i = 0; i < len; i++) {
                putchar(buf[i]);
            }
            vga_set_cursor(start_x + cursor_pos, start_y);
        }
    }
}