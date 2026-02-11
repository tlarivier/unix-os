#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

#define ATA_PRIMARY_BASE    0x1F0
#define ATA_PRIMARY_CTRL    0x3F6

#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECCOUNT0   0x02
#define ATA_REG_LBA0        0x03
#define ATA_REG_LBA1        0x04
#define ATA_REG_LBA2        0x05
#define ATA_REG_HDDEVSEL    0x06
#define ATA_REG_COMMAND     0x07
#define ATA_REG_STATUS      0x07

#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_READ_PIO_EXT    0x24
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_WRITE_PIO_EXT   0x34
#define ATA_CMD_IDENTIFY        0xEC

#define ATA_SR_BSY     0x80
#define ATA_SR_DRDY    0x40
#define ATA_SR_DF      0x20
#define ATA_SR_DSC     0x10
#define ATA_SR_DRQ     0x08
#define ATA_SR_CORR    0x04
#define ATA_SR_IDX     0x02
#define ATA_SR_ERR     0x01

#define ATA_ER_BBK      0x80
#define ATA_ER_UNC      0x40
#define ATA_ER_MC       0x20
#define ATA_ER_IDNF     0x10
#define ATA_ER_MCR      0x08
#define ATA_ER_ABRT     0x04
#define ATA_ER_TK0NF    0x02
#define ATA_ER_AMNF     0x01

struct ata_device {
    uint16_t base;
    uint16_t ctrl;
    uint8_t  slave;
    uint32_t size;
    char     model[41];
    uint8_t  present;
};

int ata_init(void);
int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, void *buffer);
int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const void *buffer);
int ata_identify(uint8_t drive);

uint8_t ata_status_wait(uint16_t base, uint8_t mask, uint8_t value, uint32_t timeout);
void ata_select_drive(uint16_t base, uint8_t slave);
void ata_setup_lba(uint16_t base, uint32_t lba, uint8_t count);

#endif 
