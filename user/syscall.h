#pragma once
#include <stdint.h>

static inline int64_t sys_call3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3)
        : "memory"
    );
    return ret;
}

static inline int64_t sys_call1(int64_t num, int64_t a1) {
    int64_t ret;
    __asm__ volatile (
        "int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(a1)
        : "memory"
    );
    return ret;
}

static inline int64_t sys_yield(void) {
    return sys_call3(3, 0, 0, 0);
}

static inline int64_t sys_write(int64_t fd, const void* buf, int64_t len) {
    return sys_call3(1, fd, (int64_t)(uintptr_t)buf, len);
}

static inline void sys_exit(int64_t code) {
    sys_call1(2, code);
    for (;;) { __asm__ volatile("hlt"); }
}
