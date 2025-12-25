#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MAX_CPUS 8

typedef struct {
    uint32_t apic_id;
    bool present;
    bool online;
} cpu_info_t;

void cpu_init_bsp(uint32_t apic_id);
uint32_t cpu_register(uint32_t apic_id, bool is_bsp);
void cpu_set_online(uint32_t cpu_id, bool online);
uint32_t cpu_count(void);
uint32_t cpu_online_count(void);
uint32_t cpu_apic_id(uint32_t cpu_id);
uint32_t cpu_current_id(void);
void cpu_set_apic_ready(bool ready);
