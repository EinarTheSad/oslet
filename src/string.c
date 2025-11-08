#include <stddef.h>
#include <stdint.h>

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

char toupper_s(char c) {
    return (c >= 'a' && c <= 'z') ? c - 32 : c;
}