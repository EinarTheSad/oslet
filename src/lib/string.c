#include "string.h"

void *memcpy(void *dst, const void *src, size_t n) {
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *dst, int val, size_t n) {
    char *d = dst;
    while (n--) *d++ = (char)val;
    return dst;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

char *strcpy(char *dst, const char *src) {
    char *ret = dst;
    while ((*dst++ = *src++));
    return ret;
}

char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char*)s;
    return (char*)last;
}

static inline char _toupper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = _toupper(*a);
        char cb = _toupper(*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return _toupper(*a) - _toupper(*b);
}