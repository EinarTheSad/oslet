#pragma once

#include <stdint.h>

static inline int cpu_can_toggle_flag(uint32_t flag) {
    uint32_t before, after;
    __asm__ volatile(
        "pushfl\n\t"
        "popl %0\n\t"
        "cli\n\t"
        "movl %0, %1\n\t"
        "xorl %2, %1\n\t"
        "andl $0xfffffdff, %1\n\t"
        "pushl %1\n\t"
        "popfl\n\t"
        "pushfl\n\t"
        "popl %1\n\t"
        "pushl %0\n\t"
        "popfl"
        : "=&r"(before), "=&r"(after)
        : "r"(flag)
        : "cc", "memory");
    return ((before ^ after) & flag) != 0;
}

static inline int cpu_has_cpuid(void) {
    return cpu_can_toggle_flag(1u << 21);
}

static inline int cpu_has_invlpg(void) {
    return cpu_can_toggle_flag(1u << 18);
}
