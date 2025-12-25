#include "arch/x86_64/apic.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/cpu.h"
#include "console.h"

#define APIC_MSR_BASE 0x1B

#define APIC_REG_ID      0x020
#define APIC_REG_EOI     0x0B0
#define APIC_REG_SVR     0x0F0
#define APIC_REG_DFR     0x0E0
#define APIC_REG_LDR     0x0D0
#define APIC_REG_LVT_LINT0 0x350
#define APIC_REG_LVT_LINT1 0x360
#define APIC_REG_ICR_LOW 0x300
#define APIC_REG_ICR_HIGH 0x310

#define APIC_ICR_DELIV_STATUS (1u << 12)
#define APIC_ICR_LEVEL_ASSERT (1u << 14)
#define APIC_ICR_TRIG_LEVEL   (1u << 15)
#define APIC_ICR_DEST_ALL_EXCL (3u << 18)

static volatile uint32_t* g_apic = 0;

static inline void apic_write(uint32_t reg, uint32_t val) {
    g_apic[reg / 4] = val;
    (void)g_apic[reg / 4];
}

static inline uint32_t apic_read(uint32_t reg) {
    return g_apic[reg / 4];
}

static void apic_wait_icr(void) {
    while (apic_read(APIC_REG_ICR_LOW) & APIC_ICR_DELIV_STATUS) {
        cpu_pause();
    }
}

static void apic_init_common(void) {
    uint64_t base = read_msr(APIC_MSR_BASE);
    base |= (1ULL << 11);
    write_msr(APIC_MSR_BASE, base);

    uint64_t apic_base = base & 0xFFFFF000ULL;
    g_apic = (volatile uint32_t*)(uintptr_t)apic_base;

    apic_write(APIC_REG_DFR, 0xFFFFFFFFU);
    apic_write(APIC_REG_LDR, 0x01000000U);

    apic_write(APIC_REG_LVT_LINT0, 1U << 16);
    apic_write(APIC_REG_LVT_LINT1, 1U << 16);

    apic_write(APIC_REG_SVR, APIC_SPURIOUS_VECTOR | 0x100U);
}

void apic_init_bsp(void) {
    apic_init_common();
    cpu_set_apic_ready(true);
    console_write("[apic] BSP enabled, id=");
    console_write_dec_u64(apic_id());
    console_write("\n");
}

void apic_init_ap(void) {
    apic_init_common();
    cpu_set_apic_ready(true);
}

uint32_t apic_id(void) {
    if (!g_apic) return 0;
    return apic_read(APIC_REG_ID) >> 24;
}

void apic_eoi(void) {
    if (!g_apic) return;
    apic_write(APIC_REG_EOI, 0);
}

void apic_send_init(uint32_t apic_id_value) {
    apic_wait_icr();
    apic_write(APIC_REG_ICR_HIGH, apic_id_value << 24);
    apic_write(APIC_REG_ICR_LOW, 0x000C500U | APIC_ICR_LEVEL_ASSERT | APIC_ICR_TRIG_LEVEL);
    apic_wait_icr();
}

void apic_send_sipi(uint32_t apic_id_value, uint8_t vector) {
    apic_wait_icr();
    apic_write(APIC_REG_ICR_HIGH, apic_id_value << 24);
    apic_write(APIC_REG_ICR_LOW, 0x00000600U | vector);
    apic_wait_icr();
}

void apic_send_ipi_all(uint8_t vector) {
    apic_wait_icr();
    apic_write(APIC_REG_ICR_HIGH, 0);
    apic_write(APIC_REG_ICR_LOW, vector | APIC_ICR_DEST_ALL_EXCL);
    apic_wait_icr();
}
