#include "arch/x86_64/mp.h"
#include "console.h"
#include "lib.h"

#define MP_FLOATING_SIGNATURE 0x5F504D5FUL /* _MP_ */
#define MP_CONFIG_SIGNATURE   0x504D4350UL /* PCMP */

typedef struct {
    uint32_t signature;
    uint32_t config_table;
    uint8_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    uint8_t feature[5];
} __attribute__((packed)) mp_floating_t;

typedef struct {
    uint32_t signature;
    uint16_t length;
    uint8_t spec_rev;
    uint8_t checksum;
    char oem_id[8];
    char product_id[12];
    uint32_t oem_table;
    uint16_t oem_size;
    uint16_t entry_count;
    uint32_t lapic_addr;
    uint16_t ext_length;
    uint8_t ext_checksum;
    uint8_t reserved;
} __attribute__((packed)) mp_config_t;

typedef struct {
    uint8_t type;
    uint8_t apic_id;
    uint8_t apic_version;
    uint8_t cpu_flags;
    uint32_t cpu_signature;
    uint32_t feature_flags;
    uint32_t reserved[2];
} __attribute__((packed)) mp_proc_t;

static uint8_t checksum_ok(const uint8_t* data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++) sum = (uint8_t)(sum + data[i]);
    return sum == 0;
}

static const mp_floating_t* mp_scan_range(uintptr_t start, size_t len) {
    const uint8_t* p = (const uint8_t*)start;
    for (size_t off = 0; off + sizeof(mp_floating_t) <= len; off += 16) {
        const mp_floating_t* mp = (const mp_floating_t*)(p + off);
        if (mp->signature == MP_FLOATING_SIGNATURE &&
            checksum_ok((const uint8_t*)mp, mp->length * 16)) {
            return mp;
        }
    }
    return 0;
}

static const mp_floating_t* mp_find_floating(void) {
    uint16_t ebda_seg = *(uint16_t*)(uintptr_t)0x40E;
    uintptr_t ebda = (uintptr_t)ebda_seg << 4;
    if (ebda) {
        const mp_floating_t* mp = mp_scan_range(ebda, 1024);
        if (mp) return mp;
    }

    uint16_t base_kb = *(uint16_t*)(uintptr_t)0x413;
    uintptr_t base = ((uintptr_t)base_kb << 10) - 1024;
    if (base) {
        const mp_floating_t* mp = mp_scan_range(base, 1024);
        if (mp) return mp;
    }

    return mp_scan_range(0xF0000, 0x10000);
}

bool mp_init(mp_info_t* info) {
    if (!info) return false;
    memset(info, 0, sizeof(*info));

    const mp_floating_t* mp = mp_find_floating();
    if (!mp || mp->config_table == 0) {
        console_write("[mp] no MP table found\n");
        return false;
    }

    const mp_config_t* cfg = (const mp_config_t*)(uintptr_t)mp->config_table;
    if (cfg->signature != MP_CONFIG_SIGNATURE ||
        !checksum_ok((const uint8_t*)cfg, cfg->length)) {
        console_write("[mp] invalid MP config table\n");
        return false;
    }

    info->lapic_addr = cfg->lapic_addr;

    const uint8_t* entry = (const uint8_t*)(cfg + 1);
    for (uint16_t i = 0; i < cfg->entry_count; i++) {
        uint8_t type = entry[0];
        if (type == 0) {
            const mp_proc_t* proc = (const mp_proc_t*)entry;
            if (proc->cpu_flags & 0x1) {
                if (info->cpu_count < MAX_CPUS) {
                    info->cpu_apic_ids[info->cpu_count++] = proc->apic_id;
                }
                if (proc->cpu_flags & 0x2) {
                    info->bsp_apic_id = proc->apic_id;
                }
            }
            entry += sizeof(mp_proc_t);
        } else if (type == 1 || type == 2 || type == 3 || type == 4) {
            entry += 8;
        } else {
            console_write("[mp] unknown entry type\n");
            return false;
        }
    }

    console_write("[mp] CPUs detected=");
    console_write_dec_u64(info->cpu_count);
    console_write(" BSP APIC=");
    console_write_dec_u64(info->bsp_apic_id);
    console_write("\n");
    return info->cpu_count > 0;
}
