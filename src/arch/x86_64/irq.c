#include "arch/x86_64/irq.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/pic.h"
#include "console.h"
#include "lib.h"

#define IRQ_MAX 16
#define IRQ_NEST_LIMIT 8

static irq_handler_t irq_handlers[IRQ_MAX];
static const char* irq_names[IRQ_MAX];
static uint8_t irq_priorities[IRQ_MAX];

static uint16_t irq_mask_stack[IRQ_NEST_LIMIT];
static uint8_t irq_prio_stack[IRQ_NEST_LIMIT];
static uint8_t irq_nesting = 0;
static uint8_t current_priority = 0xFF;
static uint8_t unhandled_logged[IRQ_MAX];

void irq_init(void) {
    for (uint8_t i = 0; i < IRQ_MAX; i++) {
        irq_handlers[i] = 0;
        irq_names[i] = "unassigned";
        irq_priorities[i] = i; /* lower IRQ number = higher priority */
        unhandled_logged[i] = 0;
    }
    irq_nesting = 0;
    current_priority = 0xFF;
    memset(irq_mask_stack, 0, sizeof(irq_mask_stack));
    memset(irq_prio_stack, 0, sizeof(irq_prio_stack));
}

void irq_register_handler(uint8_t irq, irq_handler_t handler, const char* name) {
    if (irq >= IRQ_MAX) return;
    irq_handlers[irq] = handler;
    if (name) irq_names[irq] = name;
}

void irq_unregister_handler(uint8_t irq) {
    if (irq >= IRQ_MAX) return;
    irq_handlers[irq] = 0;
    irq_names[irq] = "unassigned";
}

void irq_set_priority(uint8_t irq, uint8_t priority) {
    if (irq >= IRQ_MAX) return;
    irq_priorities[irq] = priority;
}

uint8_t irq_get_priority(uint8_t irq) {
    if (irq >= IRQ_MAX) return 0xFF;
    return irq_priorities[irq];
}

const char* irq_get_name(uint8_t irq) {
    if (irq >= IRQ_MAX) return "invalid";
    return irq_names[irq];
}

void irq_enter(uint8_t irq) {
    if (irq >= IRQ_MAX) return;
    if (irq_nesting >= IRQ_NEST_LIMIT) {
        console_write("[irq] nesting overflow, keeping interrupts masked\n");
        return;
    }

    uint16_t prev_mask = pic_get_mask();
    uint16_t new_mask = prev_mask;
    uint8_t prio = irq_priorities[irq];

    for (uint8_t i = 0; i < IRQ_MAX; i++) {
        if (irq_priorities[i] >= prio) {
            new_mask |= (uint16_t)(1u << i);
        }
    }

    irq_mask_stack[irq_nesting] = prev_mask;
    irq_prio_stack[irq_nesting] = current_priority;
    irq_nesting++;
    current_priority = prio;

    pic_set_mask_all(new_mask);
    cpu_sti();
}

void irq_exit(void) {
    if (irq_nesting == 0) return;
    cpu_cli();

    irq_nesting--;
    current_priority = irq_prio_stack[irq_nesting];
    pic_set_mask_all(irq_mask_stack[irq_nesting]);

    if (irq_nesting > 0) {
        cpu_sti();
    }
}

void irq_dispatch(uint8_t irq, intr_frame_t* frame) {
    if (irq >= IRQ_MAX) return;
    if (irq_handlers[irq]) {
        irq_handlers[irq](irq, frame);
        return;
    }

    if (!unhandled_logged[irq]) {
        console_write("[irq] unhandled IRQ ");
        console_write_dec_u64(irq);
        console_write(" (");
        console_write(irq_names[irq]);
        console_write(")\n");
        unhandled_logged[irq] = 1;
    }
}
