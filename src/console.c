#include "console.h"
#include <stdbool.h>

#define LONG_MIN (-2147483647L - 1)
#define LONG_MAX 2147483647L

const char *kernel_version = "0.5";
const char *shell_version = "oslet-v04";
const char *shell_name = "SHELL.ELF"; /* This can be any program! */

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

void memcpy_s(void *dst, const void *src, size_t n) {
    char *d = dst; const char *s = src;
    while (n--) *d++ = *s++;
}

void memset_s(void *dst, int val, size_t n) {
    char *d = dst;
    while (n--) *d++ = (uint8_t)val;
}

int strcmp_s(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

int strcasecmp_s(const char *a, const char *b) {
    while (*a && *b) {
        char ca = toupper_s(*a);
        char cb = toupper_s(*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return toupper_s(*a) - toupper_s(*b);
}

size_t strlen_s(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

void strcpy_s(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (i < max - 1 && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

char* strrchr_s(const char *s, char c) {
    const char *last = NULL;
    while (*s) {
        if (*s == c) last = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)last;
}

char toupper_s(char c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}

/* Supports: %c %s %d %u %x %X %p %% with width/zero-pad basics */
int kvprintf(const char* fmt, va_list ap, emit_fn emit, void* user) {
    int written = 0;
    for (const char* p = fmt; *p; ++p) {
        if (*p != '%') { emit(*p, user); written++; continue; }
        ++p;

        // flags: '-' (left-justify), '0' (zero pad)
        bool left = false;
        bool zero = false;
        for (;;) {
            if (*p == '-') { left = true; ++p; continue; }
            if (*p == '0') { zero = true; ++p; continue; }
            break;
        }
        if (left) zero = false;

        int width = 0;
        while (*p >= '0' && *p <= '9') { width = width*10 + (*p - '0'); ++p; }

        int precision = 6;
        if (*p == '.') {
            precision = 0; ++p;
            while (*p >= '0' && *p <= '9')
                precision = precision * 10 + (*p++ - '0');
        }

        char spec = *p ? *p : '%';
        char pad_char = zero ? '0' : ' ';

        switch (spec) {
            case '%':
                emit('%', user); written++;
                break;

            case 'c': {
                int ch = va_arg(ap, int);
                if (!left && width > 1) pad_out(width - 1, ' ', emit, user, &written);
                emit((char)ch, user); written++;
                if (left && width > 1) pad_out(width - 1, ' ', emit, user, &written);
            } break;

            case 's': {
                const char* s = va_arg(ap, const char*);
                if (!s) s = "(null)";
                int len = 0; while (s[len]) len++;
                if (!left && width > len) pad_out(width - len, ' ', emit, user, &written);
                for (int i = 0; i < len; i++) { emit(s[i], user); written++; }
                if (left && width > len) pad_out(width - len, ' ', emit, user, &written);
            } break;

            case 'd': {
                long v = va_arg(ap, int);
                unsigned long uv;
                int is_negative = 0;

                if (v < 0) {
                    is_negative = 1;
                    if (v == LONG_MIN) {
                        uv = (unsigned long)LONG_MAX + 1UL;
                    } else {
                        uv = (unsigned long)(-v);
                    }
                } else {
                    uv = (unsigned long)v;
                }

                char tmp[32]; int i = 0;
                do { tmp[i++] = (char)('0' + (uv % 10)); uv /= 10; } while (uv && i < 32);

                int len = i + (is_negative ? 1 : 0);

                if (!left) {
                    if (width > len) pad_out(width - len, pad_char, emit, user, &written);
                    if (is_negative) { emit('-', user); written++; }
                    while (i--) { emit(tmp[i], user); written++; }
                } else {
                    if (is_negative) { emit('-', user); written++; }
                    while (i--) { emit(tmp[i], user); written++; }
                    if (width > len) pad_out(width - len, ' ', emit, user, &written);
                }
            } break;

            case 'u': {
                unsigned long v = va_arg(ap, unsigned int);
                char tmp[32]; int i = 0;
                do { tmp[i++] = (char)('0' + (v % 10)); v /= 10; } while (v && i < 32);
                int len = i;
                if (!left) {
                    if (width > len) pad_out(width - len, pad_char, emit, user, &written);
                    while (i--) { emit(tmp[i], user); written++; }
                } else {
                    while (i--) { emit(tmp[i], user); written++; }
                    if (width > len) pad_out(width - len, ' ', emit, user, &written);
                }
            } break;

            case 'f': {
                double v = va_arg(ap, double);
                if (v < 0) { emit('-', user); written++; v = -v; }

                double rounding = 0.5;
                for (int d = 0; d < precision; d++) rounding /= 10.0;
                v += rounding;

                long whole = (long)v;
                double frac = v - (double)whole;

                // whole part
                char tmp[32]; int i = 0;
                do { tmp[i++] = (char)('0' + (whole % 10)); whole /= 10; } while (whole && i < 32);
                while (i--) { emit(tmp[i], user); written++; }

                emit('.', user); written++;

                // fractional part
                for (int d = 0; d < precision; d++) {
                    frac *= 10.0;
                    int digit = (int)frac;
                    emit('0' + digit, user); written++;
                    frac -= digit;
                }
            } break;

            case 'x':
            case 'X': {
                unsigned long v = va_arg(ap, unsigned int);
                unsigned long t = v; int n = 0; do { n++; t >>= 4; } while (t);
                const char* dig = (spec == 'X') ? "0123456789ABCDEF" : "0123456789abcdef";

                if (!left) {
                    if (width > n) pad_out(width - n, pad_char, emit, user, &written);
                    for (int i = (n - 1) * 4; i >= 0; i -= 4) {
                        unsigned d = (v >> i) & 0xF;
                        emit(dig[d], user); written++;
                    }
                } else {
                    for (int i = (n - 1) * 4; i >= 0; i -= 4) {
                        unsigned d = (v >> i) & 0xF;
                        emit(dig[d], user); written++;
                    }
                    if (width > n) pad_out(width - n, ' ', emit, user, &written);
                }
            } break;

            case 'p': {
                uintptr_t v = (uintptr_t)va_arg(ap, void*);
                int nibbles = (int)(sizeof(uintptr_t) * 2);
                int core_len = 2 + nibbles; // "0x" + hex
                if (!left) {
                    if (width > core_len) pad_out(width - core_len, pad_char, emit, user, &written);
                    emit('0', user); emit('x', user); written += 2;
                    for (int i = nibbles - 1; i >= 0; --i) {
                        unsigned d = (v >> (i * 4)) & 0xF;
                        emit("0123456789abcdef"[d], user); written++;
                    }
                } else {
                    emit('0', user); emit('x', user); written += 2;
                    for (int i = nibbles - 1; i >= 0; --i) {
                        unsigned d = (v >> (i * 4)) & 0xF;
                        emit("0123456789abcdef"[d], user); written++;
                    }
                    if (width > core_len) pad_out(width - core_len, ' ', emit, user, &written);
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