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