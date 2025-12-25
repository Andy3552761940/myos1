#pragma once
#include <stdint.h>
#include "arch/x86_64/interrupts.h"

typedef void (*irq_handler_t)(uint8_t irq, intr_frame_t* frame);

void irq_init(void);
void irq_register_handler(uint8_t irq, irq_handler_t handler, const char* name);
void irq_unregister_handler(uint8_t irq);
void irq_set_priority(uint8_t irq, uint8_t priority);
uint8_t irq_get_priority(uint8_t irq);
const char* irq_get_name(uint8_t irq);

void irq_enter(uint8_t irq);
void irq_exit(void);
void irq_dispatch(uint8_t irq, intr_frame_t* frame);
