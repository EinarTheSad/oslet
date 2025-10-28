#pragma once
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    // Write a chunk; return bytes written.
    size_t (*write)(const char* s, size_t n, void* ctx);
    void* ctx;
} console_t;

// Global current console (stdout/stderr equivalent)
void console_set(const console_t* c);
const console_t* console_get(void);

// Minimal stdio-like surface
int putchar(int c);
int puts(const char* s);
int vprintf(const char* fmt, va_list ap);
int printf(const char* fmt, ...);

// Safe buffers for logs or panic messages
int vsnprintf(char* dst, size_t cap, const char* fmt, va_list ap);
int snprintf(char* dst, size_t cap, const char* fmt, ...);

// Core formatter reusable for any sink
typedef void (*emit_fn)(char ch, void* user);
int kvprintf(const char* fmt, va_list ap, emit_fn emit, void* user);
