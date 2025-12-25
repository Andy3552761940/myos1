#include "arch/x86_64/idt.h"
#include "arch/x86_64/gdt.h"
#include "console.h"
#include "lib.h"

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;       /* bits 0-2 hold IST index, rest 0 */
    uint8_t  type_attr; /* type and attributes */
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

static idt_entry_t idt[256];

extern void* isr_stub_table[256];

static void idt_set_gate(uint8_t vec, void* isr, uint8_t type_attr, uint8_t ist_index) {
    uint64_t addr = (uint64_t)(uintptr_t)isr;
    idt_entry_t* e = &idt[vec];
    e->offset_low  = (uint16_t)(addr & 0xFFFF);
    e->selector    = GDT_SEL_KCODE;
    e->ist         = (uint8_t)(ist_index & 0x7);
    e->type_attr   = type_attr;
    e->offset_mid  = (uint16_t)((addr >> 16) & 0xFFFF);
    e->offset_high = (uint32_t)((addr >> 32) & 0xFFFFFFFF);
    e->zero        = 0;
}

void idt_init(void) {
    memset(idt, 0, sizeof(idt));

    for (uint16_t i = 0; i < 256; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x8E, 0);
    }

    /* Exceptions 0-31 (interrupt gates, DPL=0). Double fault uses IST1. */
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t ist = (i == 8) ? 1 : 0;
        idt_set_gate(i, isr_stub_table[i], 0x8E, ist);
    }

    /* Syscall (int 0x80): DPL=3 so user mode can invoke. */
    idt_set_gate(0x80, isr_stub_table[0x80], 0xEE, 0);

    idt_ptr_t idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base  = (uint64_t)(uintptr_t)&idt[0],
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    console_write("[idt] loaded IDT\n");
}
