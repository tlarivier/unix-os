#ifndef KERNEL_PCI_H
#define KERNEL_PCI_H

#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

#define PCI_VENDOR_ID 0x00   /* u16 */
#define PCI_DEVICE_ID 0x02   /* u16 */
#define PCI_COMMAND 0x04     /* u16 */
#define PCI_STATUS 0x06      /* u16 */
#define PCI_REVISION_ID 0x08 /* u8  */
#define PCI_PROG_IF 0x09     /* u8  */
#define PCI_SUBCLASS 0x0A    /* u8  */
#define PCI_CLASS 0x0B       /* u8  */
#define PCI_HEADER_TYPE 0x0E /* u8  */
#define PCI_BAR0 0x10
#define PCI_BAR1 0x14
#define PCI_BAR2 0x18
#define PCI_BAR3 0x1C
#define PCI_BAR4 0x20
#define PCI_BAR5 0x24
#define PCI_INTERRUPT_LINE 0x3C /* u8 - IRQ */
#define PCI_INTERRUPT_PIN 0x3D  /* u8 */

#define PCI_COMMAND_IO 0x0001
#define PCI_COMMAND_MEMORY 0x0002
#define PCI_COMMAND_MASTER 0x0004

typedef struct pci_device {
  uint8_t bus;
  uint8_t dev;
  uint8_t func;
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_code;
  uint8_t subclass;
  uint8_t prog_if;
  uint8_t irq;
} pci_device_t;

int pci_find_device(uint16_t vendor, uint16_t device, pci_device_t *out_dev);
uint16_t pci_bar_io(const pci_device_t *d, int bar_index);
void pci_enable_device(const pci_device_t *d);
void pci_init(void);

#endif
