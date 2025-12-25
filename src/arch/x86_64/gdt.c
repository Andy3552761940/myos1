#include "arch/x86_64/gdt.h"
#include "console.h"
#include "lib.h"

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) tss_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) gdt_ptr_t;

static uint64_t gdt[7];
static tss_t tss;
static uint8_t df_stack[4096] __attribute__((aligned(16)));

static void set_tss_descriptor(uint64_t base, uint32_t limit) {
    uint64_t low = 0;
    uint64_t high = 0;

    low |= (limit & 0xFFFFULL);
    low |= (base & 0xFFFFULL) << 16;
    low |= ((base >> 16) & 0xFFULL) << 32;
    low |= (0x89ULL) << 40; /* present=1, type=0x9 (available 64-bit TSS) */
    low |= ((limit >> 16) & 0xFULL) << 48;
    low |= ((base >> 24) & 0xFFULL) << 56;

    high = (base >> 32) & 0xFFFFFFFFULL;

    gdt[5] = low;
    gdt[6] = high;
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void gdt_init(void) {
    memset(gdt, 0, sizeof(gdt));
    memset(&tss, 0, sizeof(tss));

    /* Kernel code/data */
    gdt[1] = 0x00AF9A000000FFFFULL;
    gdt[2] = 0x00CF92000000FFFFULL;

    /* User code/data (DPL=3) */
    gdt[3] = 0x00AFFA000000FFFFULL;
    gdt[4] = 0x00CFF2000000FFFFULL;

    /* TSS */
    tss.iomap_base = (uint16_t)sizeof(tss_t);
    tss.ist1 = (uint64_t)(uintptr_t)(df_stack + sizeof(df_stack));
    set_tss_descriptor((uint64_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss_t) - 1));

    gdt_ptr_t gdtr = {
        .limit = (uint16_t)(sizeof(gdt) - 1),
        .base  = (uint64_t)(uintptr_t)&gdt[0],
    };

    __asm__ volatile ("lgdt %0" : : "m"(gdtr));

    /* Reload segment registers (CS stays valid as selector values match) */
    __asm__ volatile (
        "movw %0, %%ax\n"
        "movw %%ax, %%ds\n"
        "movw %%ax, %%es\n"
        "movw %%ax, %%ss\n"
        :
        : "i"(GDT_SEL_KDATA)
        : "ax"
    );

    /* Load Task Register */
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)GDT_SEL_TSS));

    console_write("[gdt] loaded GDT + TSS, IST1=");
    console_write_hex64(tss.ist1);
    console_write("\n");
}
