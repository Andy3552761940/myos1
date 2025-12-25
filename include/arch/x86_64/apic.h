#pragma once
#include <stdint.h>

#define APIC_SPURIOUS_VECTOR 0xF0
#define APIC_RESCHED_VECTOR  0xF1

void apic_init_bsp(void);
void apic_init_ap(void);
uint32_t apic_id(void);
void apic_eoi(void);
void apic_send_init(uint32_t apic_id);
void apic_send_sipi(uint32_t apic_id, uint8_t vector);
void apic_send_ipi_all(uint8_t vector);
