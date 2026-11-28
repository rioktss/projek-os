#pragma once
#include <stdint.h>

/* PCI configuration space offsets (type 0 header). */
#define PCI_CFG_VENDOR_ID    0x00
#define PCI_CFG_DEVICE_ID    0x02
#define PCI_CFG_COMMAND      0x04
#define PCI_CFG_HEADER_TYPE  0x0E
#define PCI_CFG_BAR0         0x10
#define PCI_CFG_CAP_PTR      0x34

#define PCI_CAP_ID_VENDOR    0x09

/* Vendor-specific capability cfg_type values used by virtio-pci. */
#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t  pci_read8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t value);
void pci_write16(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint16_t value);
void pci_write8(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint8_t value);

/* Read a 32-bit field that is not aligned to a 4-byte config boundary. */
uint32_t pci_read32_unaligned(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);

/* Return the physical base address encoded in a BAR, or 0 when unimplemented. */
uintptr_t pci_bar_base(uint8_t bus, uint8_t slot, uint8_t func, uint8_t bar_index);

/* Find the first device with the given vendor/device ID. */
int pci_find_device(uint16_t vendor, uint16_t device,
                    uint8_t *bus, uint8_t *slot, uint8_t *func);

/* Find a vendor capability of the requested cfg_type; return its config offset. */
int pci_find_vendor_cap(uint8_t bus, uint8_t slot, uint8_t func,
                        uint8_t cfg_type, uint8_t *cap_offset, uint8_t *cap_length);
