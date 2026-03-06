#include "vconsole.h"
#include "mem/heap.h"
#include "console.h"
#include "task/task.h"
#include "drivers/keyboard.h"

vconsole_t *vc_create(uint32_t owner_tid) {
    vconsole_t *vc = (vconsole_t *)kmalloc(sizeof(vconsole_t));
    if (!vc) return NULL;
    memset_s(vc, 0, sizeof(vconsole_t));
    vc->color = 0x07;
    vc->owner_tid = owner_tid;
    vc->active = 1;
    for (int i = 0; i < VC_ROWS * VC_COLS; i++) {
        vc->chars[i] = ' ';
        vc->attrs[i] = vc->color;
    }
    return vc;
}

void vc_destroy(vconsole_t *vc) {
    if (!vc) return;
    vc->active = 0;
    kfree(vc);
}

static void vc_scroll(vconsole_t *vc) {
    memcpy_s(&vc->chars[0], &vc->chars[VC_COLS], (VC_ROWS - 1) * VC_COLS);
    memcpy_s(&vc->attrs[0], &vc->attrs[VC_COLS], (VC_ROWS - 1) * VC_COLS);
    int last = (VC_ROWS - 1) * VC_COLS;
    for (int x = 0; x < VC_COLS; x++) {
        vc->chars[last + x] = ' ';
        vc->attrs[last + x] = vc->color;
    }
}

void vc_putchar(vconsole_t *vc, char c) {
    switch ((unsigned char)c) {
        case '\r':
            vc->cursor_x = 0;
            break;
        case '\n':
            vc->cursor_x = 0;
            vc->cursor_y++;
            break;
        case '\t': {
            int stop = (vc->cursor_x + 4) & ~3;
            while (vc->cursor_x < stop && vc->cursor_x < VC_COLS) {
                int off = vc->cursor_y * VC_COLS + vc->cursor_x;
                vc->chars[off] = ' ';
                vc->attrs[off] = vc->color;
                vc->cursor_x++;
            }
            break;
        }
        case '\b':
            if (vc->cursor_x > 0) vc->cursor_x--;
            break;
        default: {
            if (vc->cursor_x >= VC_COLS) {
                vc->cursor_x = 0;
                vc->cursor_y++;
            }
            while (vc->cursor_y >= VC_ROWS) {
                vc_scroll(vc);
                vc->cursor_y = VC_ROWS - 1;
            }
            int off = vc->cursor_y * VC_COLS + vc->cursor_x;
            vc->chars[off] = (uint8_t)c;
            vc->attrs[off] = vc->color;
            vc->cursor_x++;
            break;
        }
    }
    while (vc->cursor_y >= VC_ROWS) {
        vc_scroll(vc);
        vc->cursor_y = VC_ROWS - 1;
    }
    vc->dirty = 1;
}

void vc_write(vconsole_t *vc, const char *s) {
    while (*s) vc_putchar(vc, *s++);
}

void vc_send_key(vconsole_t *vc, uint8_t key) {
    int next = (vc->key_head + 1) % VC_KEYBUF_SIZE;
    if (next == vc->key_tail) return;
    vc->keybuf[vc->key_head] = key;
    vc->key_head = next;
}

int vc_getchar(vconsole_t *vc) {
    while (vc->key_head == vc->key_tail) {
        if (!vc->active) return -1;
        task_yield();
    }
    uint8_t key = vc->keybuf[vc->key_tail];
    vc->key_tail = (vc->key_tail + 1) % VC_KEYBUF_SIZE;
    return key;
}

size_t vc_getline(vconsole_t *vc, char *buf, size_t maxlen) {
    size_t n = 0;
    size_t cursor = 0;
    if (maxlen == 0) return 0;

    int browsing = 0;
    int browse_idx = vc->history_count;
    char temp_buf[VC_HISTORY_MAXLEN];
    memset_s(temp_buf, 0, sizeof(temp_buf));

    for (;;) {
        int c = vc_getchar(vc);
        if (c < 0) break;

        if (c == KEY_UP) {
            if (vc->history_count == 0) continue;
            if (!browsing) {
                memcpy_s(temp_buf, buf, n);
                temp_buf[n] = '\0';
                browsing = 1;
                browse_idx = vc->history_count;
            }
            if (browse_idx > 0) {
                browse_idx--;
                while (cursor < n) { vc_putchar(vc, buf[cursor]); cursor++; }
                while (n > 0) { vc_putchar(vc, '\b'); vc_putchar(vc, ' '); vc_putchar(vc, '\b'); n--; }
                const char *hist = vc->history[browse_idx];
                n = 0;
                while (hist[n] && n < maxlen - 1) { buf[n] = hist[n]; vc_putchar(vc, hist[n]); n++; }
                buf[n] = '\0';
                cursor = n;
            }
            continue;
        }

        if (c == KEY_DOWN) {
            if (!browsing) continue;
            if (browse_idx < vc->history_count - 1) {
                browse_idx++;
                while (cursor < n) { vc_putchar(vc, buf[cursor]); cursor++; }
                while (n > 0) { vc_putchar(vc, '\b'); vc_putchar(vc, ' '); vc_putchar(vc, '\b'); n--; }
                const char *hist = vc->history[browse_idx];
                n = 0;
                while (hist[n] && n < maxlen - 1) { buf[n] = hist[n]; vc_putchar(vc, hist[n]); n++; }
                buf[n] = '\0';
                cursor = n;
            } else if (browse_idx == vc->history_count - 1) {
                browse_idx = vc->history_count;
                while (cursor < n) { vc_putchar(vc, buf[cursor]); cursor++; }
                while (n > 0) { vc_putchar(vc, '\b'); vc_putchar(vc, ' '); vc_putchar(vc, '\b'); n--; }
                n = 0;
                while (temp_buf[n] && n < maxlen - 1) { buf[n] = temp_buf[n]; vc_putchar(vc, temp_buf[n]); n++; }
                buf[n] = '\0';
                cursor = n;
                browsing = 0;
            }
            continue;
        }

        if (c == KEY_LEFT) {
            if (cursor > 0) { cursor--; vc_putchar(vc, '\b'); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cursor < n) { vc_putchar(vc, buf[cursor]); cursor++; }
            continue;
        }
        if (c == KEY_HOME) {
            while (cursor > 0) { cursor--; vc_putchar(vc, '\b'); }
            continue;
        }
        if (c == KEY_END) {
            while (cursor < n) { vc_putchar(vc, buf[cursor]); cursor++; }
            continue;
        }
        if (c == KEY_DELETE) {
            if (cursor < n) {
                for (size_t i = cursor; i < n - 1; i++) buf[i] = buf[i + 1];
                n--;
                buf[n] = '\0';
                for (size_t i = cursor; i < n; i++) vc_putchar(vc, buf[i]);
                vc_putchar(vc, ' ');
                for (size_t i = cursor; i <= n; i++) vc_putchar(vc, '\b');
            }
            continue;
        }

        if (c >= 0x80) continue;

        if (browsing && c != '\b' && c != '\n') browsing = 0;

        if (c == '\b') {
            if (cursor > 0) {
                cursor--;
                for (size_t i = cursor; i < n - 1; i++) buf[i] = buf[i + 1];
                n--;
                buf[n] = '\0';
                vc_putchar(vc, '\b');
                for (size_t i = cursor; i < n; i++) vc_putchar(vc, buf[i]);
                vc_putchar(vc, ' ');
                for (size_t i = cursor; i <= n; i++) vc_putchar(vc, '\b');
            }
            continue;
        }

        if (c == '\n') {
            vc_putchar(vc, '\n');
            break;
        }

        if (n < maxlen - 1) {
            for (size_t i = n; i > cursor; i--) buf[i] = buf[i - 1];
            buf[cursor] = (char)c;
            n++;
            buf[n] = '\0';
            for (size_t i = cursor; i < n; i++) vc_putchar(vc, buf[i]);
            cursor++;
            for (size_t i = cursor; i < n; i++) vc_putchar(vc, '\b');
        } else {
            vc_putchar(vc, '\n');
            break;
        }
    }

    buf[n] = '\0';

    if (n > 0) {
        int save = 1;
        if (vc->history_count > 0) {
            if (strcmp_s(buf, vc->history[vc->history_count - 1]) == 0) save = 0;
        }
        if (save) {
            int idx = vc->history_count % VC_HISTORY_SIZE;
            memcpy_s(vc->history[idx], buf, n);
            vc->history[idx][n] = '\0';
            if (vc->history_count < VC_HISTORY_SIZE) vc->history_count++;
        }
    }

    return n;
}

void vc_clear(vconsole_t *vc) {
    for (int i = 0; i < VC_ROWS * VC_COLS; i++) {
        vc->chars[i] = ' ';
        vc->attrs[i] = vc->color;
    }
    vc->cursor_x = 0;
    vc->cursor_y = 0;
    vc->dirty = 1;
}

void vc_set_color(vconsole_t *vc, uint8_t bg, uint8_t fg) {
    vc->color = (fg & 0x0F) | ((bg & 0x07) << 4);
}

void vc_set_cursor(vconsole_t *vc, int x, int y) {
    if (x >= 0 && x < VC_COLS) vc->cursor_x = x;
    if (y >= 0 && y < VC_ROWS) vc->cursor_y = y;
    vc->dirty = 1;
}

void vc_get_cursor(vconsole_t *vc, int *x, int *y) {
    if (x) *x = vc->cursor_x;
    if (y) *y = vc->cursor_y;
}
