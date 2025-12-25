#include "hpet.h"
#include "arch/x86_64/pit.h"

int hpet_init(void) {
    return 0;
}

uint64_t hpet_now_ns(void) {
    uint64_t ticks = pit_ticks();
    uint32_t hz = pit_frequency_hz();
    if (hz == 0) return 0;
    return (ticks * 1000000000ull) / hz;
}
