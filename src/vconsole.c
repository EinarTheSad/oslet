#include "vconsole.h"
#include "mem/heap.h"
#include "console.h"
#include "task/task.h"
#include "drivers/keyboard.h"

static vconsole_t *vc_registry[64];

int vc_is_registered(vconsole_t *vc) {
    if (!vc) return 0;
    for (int i = 0; i < 64; i++) {
        if (vc_registry[i] == vc) return 1;
    }
    return 0;
}

static int vc_register(vconsole_t *vc) {
    if (!vc || vc_is_registered(vc)) return -1;
    for (int i = 0; i < 64; i++) {
        if (!vc_registry[i]) {
            vc_registry[i] = vc;
            return 0;
        }
    }
    return -1;
}

static void vc_unregister(vconsole_t *vc) {
    if (!vc) return;
    for (int i = 0; i < 64; i++) {
        if (vc_registry[i] == vc) {
            vc_registry[i] = NULL;
            return;
        }
    }
}

static void vc_release(vconsole_t *vc) {
    if (vc && --vc->refs == 0) kfree(vc);
}

void vc_assign_task(task_t *task, vconsole_t *vc) {
    if (task->vconsole == vc) return;
    if (vc) vc->refs++;
    vconsole_t *old = task->vconsole;
    task->vconsole = vc;
    vc_release(old);
}

void vc_begin_read(task_t *task, vconsole_t *vc) {
    vc->refs++;
    task->vc_reading = vc;
}

void vc_end_read(task_t *task) {
    vconsole_t *vc = task->vc_reading;
    task->vc_reading = NULL;
    vc_release(vc);
}

void vc_cleanup_task(task_t *task) {
    for (int i = 0; i < 64; i++) {
        vconsole_t *vc = vc_registry[i];
        if (vc && vc->owner_tid == task->tid) vc_destroy(vc);
    }
    vc_end_read(task);
    vc_assign_task(task, NULL);
}

vconsole_t *vc_create(uint32_t owner_tid) {
    vconsole_t *vc = (vconsole_t *)kmalloc(sizeof(vconsole_t));
    if (!vc) return NULL;
    memset_s(vc, 0, sizeof(vconsole_t));
    vc->color = 0x07;
    vc->cols = VC_COLS;
    vc->rows = VC_ROWS;
    vc->owner_tid = owner_tid;
    vc->refs = 1;
    vc->active = 1;
    if (vc_register(vc) != 0) {
        kfree(vc);
        return NULL;
    }
    for (int i = 0; i < VC_MAX_ROWS * VC_MAX_COLS; i++) {
        vc->chars[i] = ' ';
        vc->attrs[i] = vc->color;
    }
    return vc;
}

void vc_destroy(vconsole_t *vc) {
    if (!vc || !vc_is_registered(vc)) return;
    vc->active = 0;
    vc_unregister(vc);
    vc_release(vc);
}

static void vc_scroll(vconsole_t *vc) {
    int slot;
    if (vc->history_lines < VC_SCROLLBACK) {
        slot = (vc->history_start + vc->history_lines++) % VC_SCROLLBACK;
    } else {
        slot = vc->history_start;
        vc->history_start = (vc->history_start + 1) % VC_SCROLLBACK;
    }
    memcpy_s(vc->history_chars + slot * VC_MAX_COLS, vc->chars, VC_MAX_COLS);
    memcpy_s(vc->history_attrs + slot * VC_MAX_COLS, vc->attrs, VC_MAX_COLS);
    vc->history_sequence++;
    memcpy_s(vc->chars, vc->chars + VC_MAX_COLS, (vc->rows - 1) * VC_MAX_COLS);
    memcpy_s(vc->attrs, vc->attrs + VC_MAX_COLS, (vc->rows - 1) * VC_MAX_COLS);
    int last = (vc->rows - 1) * VC_MAX_COLS;
    for (int x = 0; x < VC_MAX_COLS; x++) {
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
            while (vc->cursor_x < stop && vc->cursor_x < vc->cols) {
                int off = vc->cursor_y * VC_MAX_COLS + vc->cursor_x;
                vc->chars[off] = ' ';
                vc->attrs[off] = vc->color;
                vc->cursor_x++;
            }
            break;
        }
        case '\b':
            if (vc->cursor_x > 0) vc->cursor_x--;
            else if (vc->cursor_y > 0) {
                vc->cursor_y--;
                vc->cursor_x = vc->cols - 1;
            }
            break;
        default: {
            if (vc->cursor_x >= vc->cols) {
                vc->cursor_x = 0;
                vc->cursor_y++;
            }
            while (vc->cursor_y >= vc->rows) {
                vc_scroll(vc);
                vc->cursor_y = vc->rows - 1;
            }
            int off = vc->cursor_y * VC_MAX_COLS + vc->cursor_x;
            vc->chars[off] = (uint8_t)c;
            vc->attrs[off] = vc->color;
            vc->cursor_x++;
            break;
        }
    }
    while (vc->cursor_y >= vc->rows) {
        vc_scroll(vc);
        vc->cursor_y = vc->rows - 1;
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
    for (;;) {
        if (!vc->active) return -1;
        if (vc->key_head != vc->key_tail) break;
        task_sleep(10);
    }
    uint8_t key = vc->keybuf[vc->key_tail];
    vc->key_tail = (vc->key_tail + 1) % VC_KEYBUF_SIZE;
    return key;
}

size_t vc_getline(vconsole_t *vc, char *buf, size_t maxlen) {
    size_t n = 0;
    size_t cursor = 0;
    if (!vc || !buf || maxlen == 0) return 0;
    if (maxlen > VC_HISTORY_MAXLEN - 1) maxlen = VC_HISTORY_MAXLEN - 1;

    int browsing = 0;
    int browse_idx = vc->history_count;
    char temp_buf[VC_HISTORY_MAXLEN];
    memset_s(temp_buf, 0, sizeof(temp_buf));

    for (;;) {
        int c = vc_getchar(vc);
        if (c < 0) {
            buf[n] = '\0';
            return (size_t)-1;
        }

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
            if (n >= VC_HISTORY_MAXLEN) n = VC_HISTORY_MAXLEN - 1;
            memcpy_s(vc->history[idx], buf, n);
            vc->history[idx][n] = '\0';
            if (vc->history_count < VC_HISTORY_SIZE) vc->history_count++;
        }
    }

    return n;
}

void vc_clear(vconsole_t *vc) {
    for (int i = 0; i < VC_MAX_ROWS * VC_MAX_COLS; i++) {
        vc->chars[i] = ' ';
        vc->attrs[i] = vc->color;
    }
    vc->cursor_x = 0;
    vc->cursor_y = 0;
    vc->history_start = 0;
    vc->history_lines = 0;
    vc->dirty = 1;
}

void vc_set_color(vconsole_t *vc, uint8_t bg, uint8_t fg) {
    vc->color = (fg & 0x0F) | ((bg & 0x07) << 4);
}

void vc_set_cursor(vconsole_t *vc, int x, int y) {
    if (x >= 0 && x < vc->cols) vc->cursor_x = x;
    if (y >= 0 && y < vc->rows) vc->cursor_y = y;
    vc->dirty = 1;
}

void vc_get_cursor(vconsole_t *vc, int *x, int *y) {
    if (x) *x = vc->cursor_x;
    if (y) *y = vc->cursor_y;
}

int vc_resize(vconsole_t *vc, int cols, int rows) {
    if (cols < 1 || cols > VC_MAX_COLS || rows < 1 || rows > VC_MAX_ROWS)
        return -1;
    if (cols == vc->cols && rows == vc->rows) return 0;

    while (vc->cursor_y >= rows) {
        vc_scroll(vc);
        vc->cursor_y--;
    }
    if (rows > vc->rows) {
        for (int y = vc->rows; y < rows; y++) {
            memset_s(vc->chars + y * VC_MAX_COLS, ' ', VC_MAX_COLS);
            memset_s(vc->attrs + y * VC_MAX_COLS, vc->color, VC_MAX_COLS);
        }
    }
    if (cols != vc->cols) {
        int from = cols < vc->cols ? cols : vc->cols;
        for (int y = 0; y < VC_MAX_ROWS; y++) {
            memset_s(vc->chars + y * VC_MAX_COLS + from, ' ', VC_MAX_COLS - from);
            memset_s(vc->attrs + y * VC_MAX_COLS + from, vc->color, VC_MAX_COLS - from);
        }
    }
    vc->cols = cols;
    vc->rows = rows;
    if (vc->cursor_x > cols) vc->cursor_x = cols;
    vc->dirty = 1;
    return 0;
}

void vc_copy_row(vconsole_t *vc, int row, uint8_t *chars, uint8_t *attrs) {
    if (row < vc->history_lines) {
        int slot = (vc->history_start + row) % VC_SCROLLBACK;
        memcpy_s(chars, vc->history_chars + slot * VC_MAX_COLS, vc->cols);
        memcpy_s(attrs, vc->history_attrs + slot * VC_MAX_COLS, vc->cols);
    } else {
        row -= vc->history_lines;
        memcpy_s(chars, vc->chars + row * VC_MAX_COLS, vc->cols);
        memcpy_s(attrs, vc->attrs + row * VC_MAX_COLS, vc->cols);
    }
}
