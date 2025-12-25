#include "arch/x86_64/pit.h"
#include "io.h"

#define PIT_CH0      0x40
#define PIT_CMD      0x43
#define PIT_BASE_HZ  1193182

static volatile uint64_t g_ticks = 0;

void pit_init(uint32_t hz) {
    if (hz == 0) hz = 100;

    uint32_t divisor = PIT_BASE_HZ / hz;
    if (divisor == 0) divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;

    /* Channel 0, lobyte/hibyte, mode 3 (square wave), binary */
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0, (uint8_t)((divisor >> 8) & 0xFF));

    g_ticks = 0;
}

void pit_handle_irq0(void) {
    g_ticks++;
}

uint64_t pit_ticks(void) {
    return g_ticks;
}
