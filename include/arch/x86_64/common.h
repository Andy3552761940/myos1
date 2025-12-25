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

static inline uint64_t read_msr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void write_msr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFULL);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}
