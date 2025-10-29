#include "console.h"
#include <stdbool.h>

static const console_t* CURRENT;

void console_set(const console_t* c) { CURRENT = c; }
const console_t* console_get(void)   { return CURRENT; }

/* Emit callback adapters */
typedef struct { char* dst; size_t cap; size_t n; } buf_ctx;

static void emit_console(char ch, void* user) {
    (void)user;
    if (!CURRENT || !CURRENT->write) return;
    CURRENT->write(&ch, 1, CURRENT->ctx);
}

static void emit_buffer(char ch, void* user) {
    buf_ctx* b = (buf_ctx*)user;
    if (b->n < b->cap - 1) {
        b->dst[b->n] = ch;
    }
    b->n++;
}

static inline void pad_out(int count, char ch, emit_fn emit, void* user, int* written) {
    while (count-- > 0) {
        emit(ch, user);
        (*written)++;
    }
}

int streq(const char *a, const char *b) {
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

/* Supports: %c %s %d %u %x %X %p %% with width/zero-pad basics */
int kvprintf(const char* fmt, va_list ap, emit_fn emit, void* user) {
    int written = 0;
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') { emit(*p, user); written++; continue; }
        ++p;

        // flags & width (zero pad only)
        bool zero = false;
        int width = 0;
        if (*p == '0') { zero = true; ++p; }
        while (*p >= '0' && *p <= '9') { width = width*10 + (*p - '0'); ++p; }

        char spec = *p ? *p : '%';
        char pad_char = zero ? '0' : ' ';

        switch (spec) {
            case '%':
                emit('%', user); written++;
                break;

            case 'c': {
                int ch = va_arg(ap, int);
                emit((char)ch, user); written++;
            } break;

            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int len = 0; while (s[len]) len++;
                if (width > len) pad_out(width - len, pad_char, emit, user, &written);
                for (int i = 0; i < len; i++) { emit(s[i], user); written++; }
            } break;

            case 'd': {
                long v = va_arg(ap, int);
                unsigned long uv = (v < 0) ? (unsigned long)(-(long)v) : (unsigned long)v;
                char tmp[32]; int i = 0;
                do { tmp[i++] = (char)('0' + (uv % 10)); uv /= 10; } while (uv && i < 32);
                int len = i + (v < 0 ? 1 : 0);
                if (width > len) pad_out(width - len, pad_char, emit, user, &written);
                if (v < 0) { emit('-', user); written++; }
                while (i--) { emit(tmp[i], user); written++; }
            } break;

            case 'u': {
                unsigned long v = va_arg(ap, unsigned int);
                char tmp[32]; int i = 0;
                do { tmp[i++] = (char)('0' + (v % 10)); v /= 10; } while (v && i < 32);
                if (width > i) pad_out(width - i, pad_char, emit, user, &written);
                while (i--) { emit(tmp[i], user); written++; }
            } break;

            case 'x':
            case 'X': {
                unsigned long v = va_arg(ap, unsigned int);
                // compute nibble count without output side effects
                unsigned long t = v; int n = 0; do { n++; t >>= 4; } while (t);
                if (width > n) pad_out(width - n, pad_char, emit, user, &written);
                const char* dig = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";
                // emit hex
                for (int i = (n - 1) * 4; i >= 0; i -= 4) {
                    unsigned d = (v >> i) & 0xF;
                    emit(dig[d], user); written++;
                }
            } break;

            case 'p': {
                uintptr_t v = (uintptr_t)va_arg(ap, void*);
                emit('0', user); emit('x', user); written += 2;
                int nibbles = (int)(sizeof(uintptr_t) * 2);
                for (int i = nibbles - 1; i >= 0; --i) {
                    unsigned d = (v >> (i * 4)) & 0xF;
                    emit("0123456789abcdef"[d], user); written++;
                }
            } break;

            default:
                // unknown specifier: print literally
                emit('%', user); emit(spec, user); written += 2;
                break;
        }
    }
    return written;
}

/* Public “stdio” */
int putchar(int c) {
    if (!CURRENT || !CURRENT->write) return -1;
    char ch = (char)c;
    return (int)CURRENT->write(&ch, 1, CURRENT->ctx);
}

int puts(const char* s) {
    if (!CURRENT || !CURRENT->write) return -1;
    size_t n = 0; while (s && s[n]) n++;
    CURRENT->write(s, n, CURRENT->ctx);
    CURRENT->write("\n", 1, CURRENT->ctx);
    return (int)n + 1;
}

int vprintf(const char* fmt, va_list ap) {
    if (!CURRENT) return -1;
    va_list cp; va_copy(cp, ap);
    int n = kvprintf(fmt, cp, emit_console, NULL);
    va_end(cp);
    return n;
}

int printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vprintf(fmt, ap);
    va_end(ap);
    return n;
}

int vsnprintf(char* dst, size_t cap, const char* fmt, va_list ap) {
    if (!dst || cap == 0) return 0;
    buf_ctx ctx = { .dst = dst, .cap = cap, .n = 0 };
    va_list cp; va_copy(cp, ap);
    int n = kvprintf(fmt, cp, emit_buffer, &ctx);
    va_end(cp);

    size_t term = (ctx.n < cap) ? ctx.n : (cap - 1);
    dst[term] = '\0';
    return n;
}

int snprintf(char* dst, size_t cap, const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(dst, cap, fmt, ap);
    va_end(ap);
    return n;
}
