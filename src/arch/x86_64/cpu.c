#include "arch/x86_64/cpu.h"
#include "arch/x86_64/apic.h"
#include "lib.h"

static cpu_info_t g_cpus[MAX_CPUS];
static uint32_t g_cpu_count = 0;
static uint32_t g_online_count = 0;
static uint32_t g_bsp_id = 0;
static bool g_apic_ready = false;

void cpu_set_apic_ready(bool ready) {
    g_apic_ready = ready;
}

void cpu_init_bsp(uint32_t apic_id) {
    memset(g_cpus, 0, sizeof(g_cpus));
    g_cpu_count = 1;
    g_online_count = 1;
    g_bsp_id = 0;
    g_cpus[0].apic_id = apic_id;
    g_cpus[0].present = true;
    g_cpus[0].online = true;
}

uint32_t cpu_register(uint32_t apic_id, bool is_bsp) {
    for (uint32_t i = 0; i < g_cpu_count; i++) {
        if (g_cpus[i].present && g_cpus[i].apic_id == apic_id) {
            if (is_bsp) g_bsp_id = i;
            return i;
        }
    }

    if (g_cpu_count >= MAX_CPUS) return g_bsp_id;

    uint32_t idx = g_cpu_count++;
    g_cpus[idx].apic_id = apic_id;
    g_cpus[idx].present = true;
    g_cpus[idx].online = false;
    if (is_bsp) g_bsp_id = idx;
    return idx;
}

void cpu_set_online(uint32_t cpu_id, bool online) {
    if (cpu_id >= g_cpu_count) return;
    if (g_cpus[cpu_id].online == online) return;
    g_cpus[cpu_id].online = online;
    if (online) {
        g_online_count++;
    } else if (g_online_count > 0) {
        g_online_count--;
    }
}

uint32_t cpu_count(void) {
    return g_cpu_count ? g_cpu_count : 1;
}

uint32_t cpu_online_count(void) {
    return g_online_count ? g_online_count : 1;
}

uint32_t cpu_apic_id(uint32_t cpu_id) {
    if (cpu_id >= g_cpu_count) return g_cpus[g_bsp_id].apic_id;
    return g_cpus[cpu_id].apic_id;
}

static uint32_t cpu_index_for_apic(uint32_t apic_id) {
    for (uint32_t i = 0; i < g_cpu_count; i++) {
        if (g_cpus[i].present && g_cpus[i].apic_id == apic_id) return i;
    }
    return g_bsp_id;
}

uint32_t cpu_current_id(void) {
    if (!g_apic_ready) return g_bsp_id;
    uint32_t apic = apic_id();
    return cpu_index_for_apic(apic);
}
