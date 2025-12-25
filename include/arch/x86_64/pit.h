#pragma once
#include <stdint.h>

void pit_init(uint32_t hz);

/* Called from IRQ0 handler to advance the tick counter. */
void pit_handle_irq0(void);

uint64_t pit_ticks(void);
