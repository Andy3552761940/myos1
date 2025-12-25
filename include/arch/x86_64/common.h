#pragma once
#include <stdint.h>

static inline void cpu_cli(void) { __asm__ volatile("cli"); }
static inline void cpu_sti(void) { __asm__ volatile("sti"); }
static inline void cpu_hlt(void) { __asm__ volatile("hlt"); }
static inline void cpu_pause(void) { __asm__ volatile("pause"); }

static inline uint64_t read_rflags(void) {
    uint64_t r;
    __asm__ volatile ("pushfq; popq %0" : "=r"(r));
    return r;
}

static inline uint64_t read_cr2(void) {
    uint64_t r;
    __asm__ volatile ("movq %%cr2, %0" : "=r"(r));
    return r;
}

static inline uint64_t read_cr3(void) {
    uint64_t r;
    __asm__ volatile ("movq %%cr3, %0" : "=r"(r));
    return r;
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile ("movq %0, %%cr3" : : "r"(v) : "memory");
}
