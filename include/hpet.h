#pragma once
#include <stdint.h>

int hpet_init(void);
uint64_t hpet_now_ns(void);
