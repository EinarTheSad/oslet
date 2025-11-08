#pragma once
#include <stddef.h>
#include <stdint.h>

void memcpy_s(void *dst, const void *src, size_t n);
void memset_s(void *dst, int val, size_t n);
int strcmp_s(const char *a, const char *b);
size_t strlen_s(const char *s);
void strcpy_s(char *dst, const char *src, size_t max);
char toupper_s(char c);