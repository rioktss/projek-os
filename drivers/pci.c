#include "pci.h"

static inline uint32_t config_address(uint8_t bus, uint8_t slot,
                                      uint8_t func, uint8_t offset) {
    return 0x80000000U | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
           ((uint32_t)func << 8) | ((uint32_t)offset & 0xFCU);
}

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value;
    __asm__ volatile ("outl %0, %1" : : "a"(config_address(bus, slot, func, offset)),
                      "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"((uint16_t)0xCFC));
    return value;
}

uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, slot, func, offset);
    return (uint16_t)(value >> (((uint32_t)offset & 2U) * 8U));
}

uint8_t pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t value = pci_read32(bus, slot, func, offset);
    return (uint8_t)(value >> (((uint32_t)offset & 3U) * 8U));
}

void pci_write32(uint8_t bus, uint8_t slot, uint8_t func,
                 uint8_t offset, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(config_address(bus, slot, func, offset)),
                      "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"((uint16_t)0xCFC));
}

void pci_write16(uint8_t bus, uint8_t slot, uint8_t func,
                 uint8_t offset, uint16_t value) {
    uint32_t address = config_address(bus, slot, func, offset);
    uint32_t shift = ((uint32_t)offset & 2U) * 8U;
    uint32_t mask = 0xFFFFU << shift;
    uint32_t old;
    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("inl %1, %0" : "=a"(old) : "Nd"((uint16_t)0xCFC));
    __asm__ volatile ("outl %0, %1" : : "a"((old & ~mask) | ((uint32_t)value << shift)),
                      "Nd"((uint16_t)0xCFC));
}

void pci_write8(uint8_t bus, uint8_t slot, uint8_t func,
                uint8_t offset, uint8_t value) {
    uint32_t address = config_address(bus, slot, func, offset);
    uint32_t shift = ((uint32_t)offset & 3U) * 8U;
    uint32_t mask = 0xFFU << shift;
    uint32_t old;
    __asm__ volatile ("outl %0, %1" : : "a"(address), "Nd"((uint16_t)0xCF8));
    __asm__ volatile ("inl %1, %0" : "=a"(old) : "Nd"((uint16_t)0xCFC));
    __asm__ volatile ("outl %0, %1" : : "a"((old & ~mask) | ((uint32_t)value << shift)),
                      "Nd"((uint16_t)0xCFC));
}

uint32_t pci_read32_unaligned(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    return (uint32_t)pci_read8(bus, slot, func, offset) |
           ((uint32_t)pci_read8(bus, slot, func, (uint8_t)(offset + 1)) << 8) |
           ((uint32_t)pci_read8(bus, slot, func, (uint8_t)(offset + 2)) << 16) |
           ((uint32_t)pci_read8(bus, slot, func, (uint8_t)(offset + 3)) << 24);
}

uintptr_t pci_bar_base(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index) {
    if (bar_index > 5) return 0;
    uint8_t offset = (uint8_t)(PCI_CFG_BAR0 + bar_index * 4U);
    uint32_t low = pci_read32(bus, slot, func, offset);
    if (low == 0 || low == 0xFFFFFFFFU) return 0;
    if ((low & 1U) != 0) return (uintptr_t)(low & ~3U); /* I/O BAR */
    if ((low & 6U) == 4U) { /* 64-bit BAR: high half lives in the next dword. */
        uint32_t high = pci_read32(bus, slot, func, (uint8_t)(offset + 4));
        return (uintptr_t)(((uint64_t)high << 32) | (uint64_t)(low & ~0xFU));
    }
    return (uintptr_t)(low & ~0xFU); /* 32-bit memory BAR */
}

int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *bus, uint8_t *slot, uint8_t *func) {
    for (uint32_t b = 0; b < 256; ++b) {
        for (uint32_t s = 0; s < 32; ++s) {
            for (uint32_t f = 0; f < 8; ++f) {
                uint32_t id = pci_read32((uint8_t)b, (uint8_t)s, (uint8_t)f, 0);
                if ((id & 0xFFFFU) != vendor ||
                    ((id >> 16) & 0xFFFFU) != device) continue;
                if ((id & 0xFFFFU) == 0xFFFFU) continue;
                *bus = (uint8_t)b;
                *slot = (uint8_t)s;
                *func = (uint8_t)f;
                return 0;
            }
        }
    }
    return -1;
}

int pci_find_vendor_cap(uint8_t bus, uint8_t slot, uint8_t func,
                        uint8_t cfg_type, uint8_t *cap_offset, uint8_t *cap_length) {
    uint8_t cap = pci_read8(bus, slot, func, PCI_CFG_CAP_PTR) & 0xFCU;
    for (int i = 0; i < 32 && cap >= 0x40; ++i) {
        uint8_t id = pci_read8(bus, slot, func, cap);
        uint8_t next = pci_read8(bus, slot, func, (uint8_t)(cap + 1));
        uint8_t length = pci_read8(bus, slot, func, (uint8_t)(cap + 2));
        if (id == PCI_CAP_ID_VENDOR &&
            pci_read8(bus, slot, func, (uint8_t)(cap + 3)) == cfg_type) {
            *cap_offset = cap;
            *cap_length = length;
            return 0;
        }
        if (next == 0) break;
        cap = next & 0xFCU;
    }
    return -1;
}
