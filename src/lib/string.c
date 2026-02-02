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

char *strcat(char *dst, const char *src) {
    char *ret = dst;
    while (*dst) dst++;
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

char *strncpy(char *dst, const char *src, size_t n) {
    char *ret = dst;
    while (n && (*dst++ = *src++)) n--;
    while (n--) *dst++ = '\0';
    return ret;
}

char toupper(char c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    return c;
}

char tolower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

void str_toupper(char *s) {
    while (*s) { *s = toupper(*s); s++; }
}

void str_tolower(char *s) {
    while (*s) { *s = tolower(*s); s++; }
}

int strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = toupper(*a);
        char cb = toupper(*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return toupper(*a) - toupper(*b);
}

void str_trim(char *s) {
    int len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\r' || s[len-1] == '\n' || s[len-1] == '\t')) {
        s[--len] = '\0';
    }
}

int str_ends_with_icase(const char *str, const char *suffix) {
    if (!str || !suffix) return 0;
    int n = strlen(str);
    int m = strlen(suffix);
    if (m > n) return 0;
    const char *a = str + (n - m);
    for (int i = 0; i < m; i++) {
        char ca = a[i];
        char cb = suffix[i];
        if (ca >= 'A' && ca <= 'Z') ca = ca - 'A' + 'a';
        if (cb >= 'A' && cb <= 'Z') cb = cb - 'A' + 'a';
        if (ca != cb) return 0;
    }
    return 1;
}

int str_match_wildcard(const char *pattern, const char *str) {
    while (*pattern && *str) {
        if (*pattern == '*') {
            pattern++;
            if (*pattern == '\0') return 1;

            if (*pattern == '.') {
                const char *dot = strchr(str, '.');
                if (!dot) return 0;
                str = dot;
                continue;
            }

            while (*str) {
                if (str_match_wildcard(pattern, str))
                    return 1;
                str++;
            }
            return 0;
        }
        else if (*pattern == '?') {
            /* '?' matches any single character */
            pattern++;
            str++;
        }
        else {
            /* Case-insensitive character match */
            char pc = *pattern;
            char sc = *str;
            if (pc >= 'A' && pc <= 'Z') pc = pc - 'A' + 'a';
            if (sc >= 'A' && sc <= 'Z') sc = sc - 'A' + 'a';

            if (pc == sc) {
                pattern++;
                str++;
                continue;
            }
            return 0;
        }
    }

    while (*pattern == '*') pattern++;

    return (*pattern == '\0' && *str == '\0');
}