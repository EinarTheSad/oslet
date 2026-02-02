#pragma once
#include <stddef.h>

void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int val, size_t n);
size_t strlen(const char *s);
int strcmp(const char *a, const char *b);
int strcasecmp(const char *a, const char *b);
char *strcpy(char *dst, const char *src);
char *strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);
char *strncpy(char *dst, const char *src, size_t n);
void str_trim(char *s);
char toupper(char c);
char tolower(char c);
void str_toupper(char *s);
void str_tolower(char *s);
int str_ends_with_icase(const char *str, const char *suffix);
int str_match_wildcard(const char *pattern, const char *str);