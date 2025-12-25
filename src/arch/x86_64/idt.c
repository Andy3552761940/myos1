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

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

extern void isr128(void);

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

    /* Exceptions 0-31 (interrupt gates, DPL=0). Double fault uses IST1. */
    void* isrs[32] = {
        isr0,isr1,isr2,isr3,isr4,isr5,isr6,isr7,isr8,isr9,isr10,isr11,isr12,isr13,isr14,isr15,
        isr16,isr17,isr18,isr19,isr20,isr21,isr22,isr23,isr24,isr25,isr26,isr27,isr28,isr29,isr30,isr31
    };
    for (uint8_t i = 0; i < 32; i++) {
        uint8_t ist = (i == 8) ? 1 : 0;
        idt_set_gate(i, isrs[i], 0x8E, ist);
    }

    /* IRQs 0-15 remapped to 32-47. */
    void* irqs[16] = { irq0,irq1,irq2,irq3,irq4,irq5,irq6,irq7,irq8,irq9,irq10,irq11,irq12,irq13,irq14,irq15 };
    for (uint8_t i = 0; i < 16; i++) {
        idt_set_gate(32 + i, irqs[i], 0x8E, 0);
    }

    /* Syscall (int 0x80): DPL=3 so user mode can invoke. */
    idt_set_gate(0x80, isr128, 0xEE, 0);

    idt_ptr_t idtr = {
        .limit = (uint16_t)(sizeof(idt) - 1),
        .base  = (uint64_t)(uintptr_t)&idt[0],
    };
    __asm__ volatile ("lidt %0" : : "m"(idtr));

    console_write("[idt] loaded IDT\n");
}
