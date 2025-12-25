#include "arch/x86_64/smp.h"
#include "arch/x86_64/apic.h"
#include "arch/x86_64/cpu.h"
#include "arch/x86_64/mp.h"
#include "arch/x86_64/common.h"
#include "arch/x86_64/gdt.h"
#include "arch/x86_64/idt.h"
#include "console.h"
#include "scheduler.h"
#include "vmm.h"
#include "lib.h"

#define AP_TRAMPOLINE_ADDR   0x7000
#define AP_TRAMPOLINE_VECTOR 0x07
#define AP_BOOT_STACK_SIZE   16384

extern uint8_t ap_trampoline_start[];
extern uint8_t ap_trampoline_end[];
extern uint8_t ap_trampoline_data[];

typedef struct {
    uint64_t stack_top;
    uint64_t cpu_id;
    uint64_t entry;
    uint64_t cr3;
} __attribute__((packed)) ap_bootstrap_t;

static mp_info_t g_mp;
static uint32_t g_cpu_count = 1;
static bool g_smp_enabled = false;

static uint8_t ap_boot_stacks[MAX_CPUS][AP_BOOT_STACK_SIZE] __attribute__((aligned(16)));
static volatile uint32_t g_ap_online[MAX_CPUS];

void scheduler_ap_main(uint64_t cpu_id);

static void delay_cycles(uint32_t cycles) {
    for (uint32_t i = 0; i < cycles; i++) {
        cpu_pause();
    }
}

static void ap_trampoline_prepare(uint32_t cpu_id) {
    size_t size = (size_t)(ap_trampoline_end - ap_trampoline_start);
    memcpy((void*)(uintptr_t)AP_TRAMPOLINE_ADDR, ap_trampoline_start, size);

    uintptr_t data_offset = (uintptr_t)(ap_trampoline_data - ap_trampoline_start);
    ap_bootstrap_t* boot = (ap_bootstrap_t*)(uintptr_t)(AP_TRAMPOLINE_ADDR + data_offset);
    boot->stack_top = (uint64_t)(uintptr_t)(ap_boot_stacks[cpu_id] + AP_BOOT_STACK_SIZE);
    boot->cpu_id = cpu_id;
    boot->entry = (uint64_t)(uintptr_t)scheduler_ap_main;
    boot->cr3 = vmm_kernel_cr3();
}

void smp_broadcast_tick(void) {
    if (!g_smp_enabled) return;
    apic_send_ipi_all(APIC_RESCHED_VECTOR);
}

uint32_t smp_cpu_count(void) {
    return g_cpu_count;
}

void smp_init(void) {
    apic_init_bsp();

    if (!mp_init(&g_mp)) {
        cpu_init_bsp(apic_id());
        g_cpu_count = 1;
        g_smp_enabled = false;
        console_write("[smp] fallback to single CPU\n");
        return;
    }

    cpu_init_bsp(g_mp.bsp_apic_id);
    for (uint8_t i = 0; i < g_mp.cpu_count; i++) {
        bool is_bsp = g_mp.cpu_apic_ids[i] == g_mp.bsp_apic_id;
        cpu_register(g_mp.cpu_apic_ids[i], is_bsp);
    }

    g_cpu_count = cpu_count();
    g_smp_enabled = g_cpu_count > 1;

    console_write("[smp] starting APs, count=");
    console_write_dec_u64(g_cpu_count);
    console_write("\n");

    for (uint32_t cpu = 0; cpu < g_cpu_count; cpu++) {
        g_ap_online[cpu] = (cpu == 0) ? 1 : 0;
    }

    for (uint32_t cpu = 0; cpu < g_cpu_count; cpu++) {
        if (cpu == 0) continue;
        uint32_t apic = cpu_apic_id(cpu);

        ap_trampoline_prepare(cpu);

        apic_send_init(apic);
        delay_cycles(100000);
        apic_send_sipi(apic, AP_TRAMPOLINE_VECTOR);
        delay_cycles(20000);
        apic_send_sipi(apic, AP_TRAMPOLINE_VECTOR);

        uint32_t timeout = 2000000;
        while (!g_ap_online[cpu] && timeout--) {
            cpu_pause();
        }

        if (!g_ap_online[cpu]) {
            console_write("[smp] AP did not start, apic=");
            console_write_dec_u64(apic);
            console_write("\n");
        }
    }

    console_write("[smp] online CPUs=");
    console_write_dec_u64(cpu_online_count());
    console_write("\n");
}

void scheduler_ap_main(uint64_t cpu_id) {
    cpu_set_online((uint32_t)cpu_id, true);
    gdt_init_cpu((uint32_t)cpu_id);
    idt_init();
    apic_init_ap();

    scheduler_register_cpu_bootstrap((uint32_t)cpu_id,
                                     ap_boot_stacks[cpu_id],
                                     AP_BOOT_STACK_SIZE);

    g_ap_online[cpu_id] = 1;
    console_write("[smp] AP online cpu=");
    console_write_dec_u64(cpu_id);
    console_write("\n");

    cpu_sti();
    for (;;) {
        cpu_hlt();
    }
}
