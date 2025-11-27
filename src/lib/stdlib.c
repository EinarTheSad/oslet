#include "stdlib.h"
#include "../syscall.h"

void* malloc(size_t size) {
    return sys_malloc(size);
}

void free(void* ptr) {
    sys_free(ptr);
}