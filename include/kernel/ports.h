#ifndef KERNEL_PORTS_H
#define KERNEL_PORTS_H

#define PIC1_CMD        0x20
#define PIC1_DATA       0x21
#define PIC2_CMD        0xA0
#define PIC2_DATA       0xA1
#define PIC_EOI         0x20

#define KB_DATA_PORT    0x60
#define KB_STATUS_PORT  0x64
#define KB_CMD_PORT     0x64

#define PIT_CH0_DATA    0x40
#define PIT_CH1_DATA    0x41
#define PIT_CH2_DATA    0x42
#define PIT_CMD         0x43

#define VGA_MISC_READ   0x3CC
#define VGA_MISC_WRITE  0x3C2
#define VGA_SEQ_INDEX   0x3C4
#define VGA_SEQ_DATA    0x3C5
#define VGA_CRTC_INDEX  0x3D4
#define VGA_CRTC_DATA   0x3D5
#define VGA_GC_INDEX    0x3CE
#define VGA_GC_DATA     0x3CF
#define VGA_AC_INDEX    0x3C0
#define VGA_AC_READ     0x3C1
#define VGA_AC_WRITE    0x3C0
#define VGA_INSTAT_READ 0x3DA
#define VGA_DAC_READ_INDEX  0x3C7
#define VGA_DAC_WRITE_INDEX 0x3C8
#define VGA_DAC_DATA    0x3C9

#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBA_LO      0x1F3
#define ATA_PRIMARY_LBA_MID     0x1F4
#define ATA_PRIMARY_LBA_HI      0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_CMD         0x1F7
#define ATA_PRIMARY_CTRL        0x3F6

#define COM1_PORT       0x3F8
#define COM2_PORT       0x2F8
#define COM3_PORT       0x3E8
#define COM4_PORT       0x2E8

#define PC_SPEAKER_PORT 0x61

#define CMOS_ADDRESS    0x70
#define CMOS_DATA       0x71

#endif 
