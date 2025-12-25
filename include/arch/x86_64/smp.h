#pragma once
#include <stdint.h>

void smp_init(void);
void smp_broadcast_tick(void);
uint32_t smp_cpu_count(void);
