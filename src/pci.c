#include "pci.h"
#include "io.h"

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_make_addr(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)(
        0x80000000u |
        ((uint32_t)bus  << 16) |
        ((uint32_t)slot << 11) |
        ((uint32_t)func << 8)  |
        (offset & 0xFC)
    );
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, offset);
    return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t v = pci_read32(bus, slot, func, offset);
    return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDR, pci_make_addr(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t old = pci_read32(bus, slot, func, offset);
    uint32_t shift = (uint32_t)((offset & 2) * 8);
    uint32_t mask = 0xFFFFu << shift;
    uint32_t newv = (old & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, slot, func, offset, newv);
}

void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t old = pci_read32(bus, slot, func, offset);
    uint32_t shift = (uint32_t)((offset & 3) * 8);
    uint32_t mask = 0xFFu << shift;
    uint32_t newv = (old & ~mask) | ((uint32_t)value << shift);
    pci_write32(bus, slot, func, offset, newv);
}

void pci_enumerate(pci_enum_cb_t cb, void* user) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint16_t vendor = pci_read16((uint8_t)bus, slot, 0, 0x00);
            if (vendor == 0xFFFF) continue;

            uint8_t header_type = pci_read8((uint8_t)bus, slot, 0, 0x0E);
            uint8_t funcs = (header_type & 0x80) ? 8 : 1;

            for (uint8_t func = 0; func < funcs; func++) {
                vendor = pci_read16((uint8_t)bus, slot, func, 0x00);
                if (vendor == 0xFFFF) continue;

                pci_dev_t dev;
                dev.bus = (uint8_t)bus;
                dev.slot = slot;
                dev.func = func;
                dev.vendor_id = vendor;
                dev.device_id = pci_read16((uint8_t)bus, slot, func, 0x02);

                uint32_t class_reg = pci_read32((uint8_t)bus, slot, func, 0x08);
                dev.class_code = (uint8_t)((class_reg >> 24) & 0xFF);
                dev.subclass   = (uint8_t)((class_reg >> 16) & 0xFF);
                dev.prog_if    = (uint8_t)((class_reg >> 8) & 0xFF);
                dev.header_type = pci_read8((uint8_t)bus, slot, func, 0x0E);

                cb(&dev, user);
            }
        }
    }
}
