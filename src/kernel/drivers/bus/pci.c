/*
 * pci.c — PCI configuration space access via mechanism #1 (ports 0xCF8/0xCFC):
 * enumerate bus 0 at boot, find by (vendor, device), decode an I/O BAR, and
 * enable IO/MEM/MASTER bits in the COMMAND register.
 *
 * Invariants:
 *  - All config cycles use the legacy (outl 0xCF8 ; inl/outl 0xCFC) sequence;
 *    callers run on the BSP before SMP is up, so no pci_lock is needed today.
 *  - 16/8-bit reads and writes are done as read-modify-write on the 32-bit
 *    dword window, preserving the surrounding bytes.
 *  - Enumeration is single-bus (QEMU pc); bridges and buses > 0 are not walked.
 *
 * Not allowed:
 *  - Calling vfs_*, schedule(), or any wait/signal primitive from here.
 *  - Defining local inb/outb/inl/outl: <kernel/io.h> provides them.
 */

#include <kernel/io.h>
#include <kernel/kprintf.h>
#include <kernel/pci.h>
#include <stdint.h>

static uint32_t addr(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
  return (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
         ((uint32_t)func << 8) | (offset & 0xFC);
}

static uint32_t pci_config_read32(uint8_t bus, uint8_t dev, uint8_t func,
                                  uint8_t offset) {
  outl(PCI_CONFIG_ADDRESS, addr(bus, dev, func, offset));
  return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_config_read16(uint8_t bus, uint8_t dev, uint8_t func,
                                  uint8_t offset) {
  uint32_t v = pci_config_read32(bus, dev, func, offset);
  return (uint16_t)((v >> ((offset & 2) * 8)) & 0xFFFF);
}

static uint8_t pci_config_read8(uint8_t bus, uint8_t dev, uint8_t func,
                                uint8_t offset) {
  uint32_t v = pci_config_read32(bus, dev, func, offset);
  return (uint8_t)((v >> ((offset & 3) * 8)) & 0xFF);
}

static void pci_config_write32(uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, uint32_t v) {
  outl(PCI_CONFIG_ADDRESS, addr(bus, dev, func, offset));
  outl(PCI_CONFIG_DATA, v);
}

static void pci_config_write16(uint8_t bus, uint8_t dev, uint8_t func,
                               uint8_t offset, uint16_t v) {
  uint8_t aligned = offset & 0xFC;
  uint32_t dw = pci_config_read32(bus, dev, func, aligned);
  int shift = (offset & 2) * 8;
  dw &= ~(0xFFFFu << shift);
  dw |= ((uint32_t)v << shift);
  pci_config_write32(bus, dev, func, aligned, dw);
}

static void fill_device(uint8_t bus, uint8_t dev, uint8_t func,
                        pci_device_t *out) {
  uint32_t vd = pci_config_read32(bus, dev, func, 0x00);
  out->bus = bus;
  out->dev = dev;
  out->func = func;
  out->vendor_id = (uint16_t)(vd & 0xFFFF);
  out->device_id = (uint16_t)(vd >> 16);
  uint32_t cc = pci_config_read32(bus, dev, func, 0x08);
  out->prog_if = (cc >> 8) & 0xFF;
  out->subclass = (cc >> 16) & 0xFF;
  out->class_code = (cc >> 24) & 0xFF;
  out->irq = pci_config_read8(bus, dev, func, PCI_INTERRUPT_LINE);
}

int pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out_dev) {
  for (uint8_t bus = 0; bus < 1; bus++) {
    for (uint8_t dev = 0; dev < 32; dev++) {
      for (uint8_t func = 0; func < 8; func++) {
        uint16_t v = pci_config_read16(bus, dev, func, PCI_VENDOR_ID);
        if (v == 0xFFFF) {
          if (func == 0)
            break;
          continue;
        }
        uint16_t did = pci_config_read16(bus, dev, func, PCI_DEVICE_ID);
        if (v == vendor && did == device) {
          fill_device(bus, dev, func, out_dev);
          return 0;
        }
        if (func == 0) {
          uint8_t hdr = pci_config_read8(bus, dev, 0, PCI_HEADER_TYPE);
          if (!(hdr & 0x80))
            break;
        }
      }
    }
  }
  return -1;
}

uint16_t pci_bar_io(const pci_device_t *d, int bar_index) {
  uint8_t off = (uint8_t)(PCI_BAR0 + bar_index * 4);
  uint32_t bar = pci_config_read32(d->bus, d->dev, d->func, off);
  if (!(bar & 0x1))
    return 0;
  return (uint16_t)(bar & 0xFFFC);
}

void pci_enable_device(const pci_device_t *d) {
  uint16_t cmd = pci_config_read16(d->bus, d->dev, d->func, PCI_COMMAND);
  cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
  pci_config_write16(d->bus, d->dev, d->func, PCI_COMMAND, cmd);
}

void pci_init(void) {
  kprintf("PCI: scanning bus 0...\n");
  int count = 0;
  for (uint8_t dev = 0; dev < 32; dev++) {
    for (uint8_t func = 0; func < 8; func++) {
      uint16_t v = pci_config_read16(0, dev, func, PCI_VENDOR_ID);
      if (v == 0xFFFF) {
        if (func == 0)
          break;
        continue;
      }
      uint16_t did = pci_config_read16(0, dev, func, PCI_DEVICE_ID);
      uint8_t cls = pci_config_read8(0, dev, func, 0x0B);
      uint8_t sub = pci_config_read8(0, dev, func, 0x0A);
      kprintf("PCI:  00:%x.%x  vendor=%x device=%x class=%x sub=%x\n", dev,
              func, v, did, cls, sub);
      count++;
      if (func == 0) {
        uint8_t hdr = pci_config_read8(0, dev, 0, PCI_HEADER_TYPE);
        if (!(hdr & 0x80))
          break;
      }
    }
  }
  kprintf("PCI: %d devices found\n", count);
}
