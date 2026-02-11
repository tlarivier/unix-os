#include <kernel/ata.h>
#include <kernel/kernel.h>
#include <kernel/memory.h>
#include <kernel/errno.h>
#include <kernel/kstring.h>
#include <kernel/io.h>

static void ata_delay_400ns(uint16_t base) {
    inb(base + ATA_REG_STATUS);
    inb(base + ATA_REG_STATUS);
    inb(base + ATA_REG_STATUS);
    inb(base + ATA_REG_STATUS);
}

static struct ata_device ata_devices[4];
static int ata_initialized = 0;

uint8_t ata_status_wait(uint16_t base, uint8_t mask, uint8_t value, uint32_t timeout) {
    uint8_t status = 0;
    for (uint32_t i = 0; i < timeout; i++) {
        status = inb(base + ATA_REG_STATUS);
        if ((status & mask) == value) return status;
        for (volatile int j = 0; j < 1000; j++);
    }
    return status;
}

void ata_select_drive(uint16_t base, uint8_t slave) {
    outb(base + ATA_REG_HDDEVSEL, 0xA0 | (slave << 4));
    ata_delay_400ns(base);
}

void ata_setup_lba(uint16_t base, uint32_t lba, uint8_t count) {
    outb(base + ATA_REG_SECCOUNT0, count);
    outb(base + ATA_REG_LBA0, (uint8_t)(lba & 0xFF));
    outb(base + ATA_REG_LBA1, (uint8_t)((lba >> 8) & 0xFF));
    outb(base + ATA_REG_LBA2, (uint8_t)((lba >> 16) & 0xFF));
}

int ata_identify(uint8_t drive) {
    if (drive >= 4) return -EINVAL;
    
    struct ata_device *dev = &ata_devices[drive];
    uint16_t base = dev->base;
    
    ata_select_drive(base, dev->slave);
    
    outb(base + ATA_REG_SECCOUNT0, 0);
    outb(base + ATA_REG_LBA0, 0);
    outb(base + ATA_REG_LBA1, 0);
    outb(base + ATA_REG_LBA2, 0);
    
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0) {
        /* Drive does not exist */
        dev->present = 0;
        return -ENODEV;
    }
    
    status = ata_status_wait(base, ATA_SR_BSY, 0, 10000);
    if (status & ATA_SR_BSY) {
        dev->present = 0;
        return -ETIMEDOUT;
    }
    
    if (status & ATA_SR_ERR) {
        dev->present = 0;
        return -EIO;
    }
    
    status = ata_status_wait(base, ATA_SR_DRQ, ATA_SR_DRQ, 10000);
    if (!(status & ATA_SR_DRQ)) {
        dev->present = 0;
        return -EIO;
    }
    
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(base + ATA_REG_DATA);
    }
    
    for (int i = 0; i < 20; i++) {
        uint16_t word = identify_data[27 + i];
        dev->model[i * 2] = (word >> 8) & 0xFF;
        dev->model[i * 2 + 1] = word & 0xFF;
    }
    dev->model[40] = '\0';
    
    dev->size = ((uint32_t)identify_data[61] << 16) | identify_data[60];
    
    dev->present = 1;
    
    kprintf("ATA Drive found\n");
    
    return 0;
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer) {
    if (drive >= 4 || !ata_devices[drive].present) {
        return -ENODEV;
    }
    
    if (!buffer || count == 0) {
        return -EINVAL;
    }
    
    struct ata_device *dev = &ata_devices[drive];
    uint16_t base = dev->base;
    uint16_t *buf = (uint16_t *)buffer;
    
    ata_select_drive(base, dev->slave);
    
    ata_setup_lba(base, lba, count);
    
    outb(base + ATA_REG_HDDEVSEL, 0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F));
    
    outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
    
    for (int sector = 0; sector < count; sector++) {
        uint8_t status = ata_status_wait(base, ATA_SR_BSY | ATA_SR_DRQ, ATA_SR_DRQ, 10000);
        
        if (status & ATA_SR_BSY) {
            kprintf("ATA read timeout on sector %d\n", sector);
            return -ETIMEDOUT;
        }
        
        if (status & ATA_SR_ERR) {
            uint8_t error = inb(base + ATA_REG_ERROR);
            kprintf("ATA read error 0x%x on sector %d\n", error, sector);
            return -EIO;
        }
        
        if (!(status & ATA_SR_DRQ)) {
            kprintf("ATA DRQ not set for sector %d\n", sector);
            return -EIO;
        }
        
        for (int i = 0; i < 256; i++) {
            buf[sector * 256 + i] = inw(base + ATA_REG_DATA);
        }
        
        ata_delay_400ns(base);
    }
    
    return count;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buffer) {
    if (drive >= 4 || !ata_devices[drive].present) {
        return -ENODEV;
    }
    
    if (!buffer || count == 0) {
        return -EINVAL;
    }
    
    struct ata_device *dev = &ata_devices[drive];
    uint16_t base = dev->base;
    const uint16_t *buf = (const uint16_t *)buffer;
    
    ata_select_drive(base, dev->slave);
    
    ata_setup_lba(base, lba, count);
    
    outb(base + ATA_REG_HDDEVSEL, 0xE0 | (dev->slave << 4) | ((lba >> 24) & 0x0F));
    
    outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
    
    for (int sector = 0; sector < count; sector++) {
        uint8_t status = ata_status_wait(base, ATA_SR_BSY | ATA_SR_DRQ, ATA_SR_DRQ, 10000);
        
        if (status & ATA_SR_BSY) {
            kprintf("ATA write timeout on sector %d\n", sector);
            return -ETIMEDOUT;
        }
        
        if (status & ATA_SR_ERR) {
            uint8_t error = inb(base + ATA_REG_ERROR);
            kprintf("ATA write error 0x%x on sector %d\n", error, sector);
            return -EIO;
        }
        
        if (!(status & ATA_SR_DRQ)) {
            kprintf("ATA DRQ not set for sector %d\n", sector);
            return -EIO;
        }
        
        for (int i = 0; i < 256; i++) {
            outw(base + ATA_REG_DATA, buf[sector * 256 + i]);
        }
        
        ata_delay_400ns(base);
        
        ata_status_wait(base, ATA_SR_BSY, 0, 10000);
    }
    
    return count;
}

static int ata_controller_present(uint16_t base) {
    outb(base + ATA_REG_SECCOUNT0, 0x55);
    outb(base + ATA_REG_LBA0, 0xAA);
    
    uint8_t sc = inb(base + ATA_REG_SECCOUNT0);
    uint8_t lb = inb(base + ATA_REG_LBA0);
    
    if (sc == 0x55 && lb == 0xAA) {
        return 1;
    }
    
    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0xFF || status == 0x00) {
        /* 0xFF = floating bus (no controller) */
        /* 0x00 = possibly no controller */
        return 0;
    }
    
    return 1;
}

int ata_init(void) {
    if (ata_initialized) {
        return 0;
    }
    
    for (int i = 0; i < 4; i++) {
        ata_devices[i].present = 0;
        ata_devices[i].size = 0;
    }
    
    ata_devices[0].base  = ATA_PRIMARY_BASE;
    ata_devices[0].ctrl  = ATA_PRIMARY_CTRL;
    ata_devices[0].slave = 0;
    
    ata_devices[1].base  = ATA_PRIMARY_BASE;
    ata_devices[1].ctrl  = ATA_PRIMARY_CTRL;
    ata_devices[1].slave = 1;
    
    if (!ata_controller_present(ATA_PRIMARY_BASE)) {
        ata_initialized = 1;
        return 0;
    }
    
    for (int i = 0; i < 2; i++) {
        ata_identify((uint8_t)i);
    }
    
    ata_initialized = 1;
    return 0;
}
