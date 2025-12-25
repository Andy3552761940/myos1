#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "arch/x86_64/cpu.h"

typedef struct {
    uint32_t lapic_addr;
    uint8_t cpu_count;
    uint8_t bsp_apic_id;
    uint8_t cpu_apic_ids[MAX_CPUS];
} mp_info_t;

bool mp_init(mp_info_t* info);
