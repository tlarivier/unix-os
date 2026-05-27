/*
 * virtio_blk.c — Legacy virtio-blk driver (port I/O, polling) that negotiates
 * the device, lays out the virtqueue, and registers READ/WRITE/FLUSH as the
 * "vda" block_device_t.
 *
 * Invariants:
 *  - virtio_blk_lock serialises every submission: a single descriptor chain
 *    is in flight at any time across all CPUs.
 *  - Memory shared with the device (vq_avail_*, vq_used_*, g_status) is
 *    accessed through volatile pointers.
 *  - The submit/poll busy-wait is bounded (VIRTIO_BLK_POLL_LIMIT) and the
 *    device acks via memory, never via a peer CPU, so the lock cannot deadlock.
 *  - VIRTIO_BLK_F_FLUSH is negotiated; flush is a real device round-trip.
 *
 * Not allowed:
 *  - Calling schedule(), wait_*, or any signal/process API from here.
 *  - Dropping virtio_blk_lock between prepare and poll of a chain.
 */

#include <kernel/block.h>
#include <kernel/io.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/pci.h>
#include <kernel/spinlock.h>
#include <kernel/virtio_blk.h>
#include <stddef.h>
#include <stdint.h>

static spinlock_t virtio_blk_lock = SPINLOCK_INIT("virtio_blk");

#define VIRTIO_VENDOR 0x1AF4
#define VIRTIO_BLK_DEVICE 0x1001

/* Legacy virtio header. */
#define VIO_DEVICE_FEATURES 0x00 /* u32 RO */
#define VIO_DRIVER_FEATURES 0x04 /* u32 W  */
#define VIO_QUEUE_ADDR 0x08      /* u32 W  - phys/4096 of queue */
#define VIO_QUEUE_SIZE 0x0C      /* u16 RO */
#define VIO_QUEUE_SELECT 0x0E    /* u16 W  */
#define VIO_QUEUE_NOTIFY 0x10    /* u16 W  */
#define VIO_DEVICE_STATUS 0x12   /* u8  RW */
#define VIO_ISR_STATUS 0x13      /* u8  RC */
#define VIO_BLK_CONFIG 0x14      /* device-specific config */

#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FAILED 128

#define VIRTIO_BLK_T_IN 0
#define VIRTIO_BLK_T_OUT 1
#define VIRTIO_BLK_T_FLUSH 4

/* In legacy virtio */
#define MAX_QUEUE_SIZE 256

#define VIRTIO_BLK_POLL_LIMIT 5000000

/* virtio queue layout (legacy, page-aligned). */
struct virtq_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

struct virtq_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

struct virtio_blk_req {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} __attribute__((packed));

static uint8_t virtq_storage[16384] __attribute__((aligned(4096)));

static struct virtq_desc *vq_desc;
static volatile uint16_t *vq_avail_flags;
static volatile uint16_t *vq_avail_idx;
static volatile uint16_t *vq_avail_ring;
static volatile uint16_t *vq_used_flags;
static volatile uint16_t *vq_used_idx;
static volatile struct virtq_used_elem *vq_used_ring;
static uint16_t g_qsize = 0;

static uint16_t g_iobase = 0;
static uint64_t g_capacity = 0;
static uint16_t g_avail_idx = 0;

static block_device_t g_vio_bd;

static struct virtio_blk_req g_hdr __attribute__((aligned(16)));
static uint8_t g_status __attribute__((aligned(16)));

uint64_t virtio_blk_capacity(void) { return g_capacity; }

static void layout_queue(uint16_t q_size) {
  g_qsize = q_size;
  for (uint32_t i = 0; i < sizeof(virtq_storage); i++)
    virtq_storage[i] = 0;

  vq_desc = (struct virtq_desc *)(virtq_storage + 0);

  uint8_t *avail = virtq_storage + sizeof(struct virtq_desc) * q_size;
  vq_avail_flags = (volatile uint16_t *)(avail + 0);
  vq_avail_idx = (volatile uint16_t *)(avail + 2);
  vq_avail_ring = (volatile uint16_t *)(avail + 4);

  uintptr_t after_avail = (uintptr_t)(avail + 6 + 2 * q_size);
  after_avail = (after_avail + 4095) & ~4095UL;
  uint8_t *used = (uint8_t *)after_avail;
  vq_used_flags = (volatile uint16_t *)(used + 0);
  vq_used_idx = (volatile uint16_t *)(used + 2);
  vq_used_ring = (volatile struct virtq_used_elem *)(used + 4);
}

int virtio_blk_init(void) {
  pci_device_t dev;
  if (pci_find_device(VIRTIO_VENDOR, VIRTIO_BLK_DEVICE, &dev) != 0) {
    kprintf("virtio-blk: no device found\n");
    return -1;
  }
  pci_enable_device(&dev);

  g_iobase = pci_bar_io(&dev, 0);
  if (!g_iobase) {
    kprintf("virtio-blk: BAR0 is not I/O\n");
    return -1;
  }
  kprintf("virtio-blk: found at PCI %x.%x.%x iobase=%x irq=%x\n", dev.bus,
          dev.dev, dev.func, g_iobase, dev.irq);

  outb(g_iobase + VIO_DEVICE_STATUS, 0);
  outb(g_iobase + VIO_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
  outb(g_iobase + VIO_DEVICE_STATUS,
       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

  uint32_t feats = inl(g_iobase + VIO_DEVICE_FEATURES);
  uint32_t driver_feats = 0;
  if (feats & (1u << 9))
    driver_feats |= (1u << 9);
  outl(g_iobase + VIO_DRIVER_FEATURES, driver_feats);
  kprintf("virtio-blk: features offered=%x accepted=%x flush=%s\n", feats,
          driver_feats, (driver_feats & (1u << 9)) ? "yes" : "no");

  outw(g_iobase + VIO_QUEUE_SELECT, 0);
  uint16_t qsize = inw(g_iobase + VIO_QUEUE_SIZE);
  if (qsize == 0) {
    kprintf("virtio-blk: queue 0 has size 0\n");
    return -1;
  }
  if (qsize > MAX_QUEUE_SIZE)
    qsize = MAX_QUEUE_SIZE;
  kprintf("virtio-blk: queue size = %x\n", qsize);

  layout_queue(qsize);
  outl(g_iobase + VIO_QUEUE_ADDR, ((uint32_t)(uintptr_t)virtq_storage) >> 12);

  uint32_t cap_lo = inl(g_iobase + VIO_BLK_CONFIG + 0);
  uint32_t cap_hi = inl(g_iobase + VIO_BLK_CONFIG + 4);
  g_capacity = ((uint64_t)cap_hi << 32) | cap_lo;
  kprintf("virtio-blk: capacity = %x sectors (high=%x)\n", (uint32_t)cap_lo,
          (uint32_t)cap_hi);

  outb(g_iobase + VIO_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                         VIRTIO_STATUS_DRIVER |
                                         VIRTIO_STATUS_DRIVER_OK);

  g_vio_bd.total_blocks = g_capacity;
  block_device_register(&g_vio_bd);
  return 0;
}

static int submit_chain(uint16_t head) {
  vq_avail_ring[g_avail_idx % g_qsize] = head;
  __asm__ volatile("" ::: "memory");
  g_avail_idx++;
  *vq_avail_idx = g_avail_idx;
  __asm__ volatile("" ::: "memory");

  outw(g_iobase + VIO_QUEUE_NOTIFY, 0);

  for (int i = 0; i < VIRTIO_BLK_POLL_LIMIT; i++) {
    if (*vq_used_idx == g_avail_idx) {
      return (g_status == 0) ? 0 : -1;
    }
  }
  kprintf("virtio-blk: timeout (status=%x avail_idx=%x used_idx=%x)\n",
          g_status, g_avail_idx, *vq_used_idx);
  return -1;
}

static int submit_request(uint32_t type, uint64_t lba, void *buf,
                          uint32_t bytes) {
  if (!g_iobase)
    return -1;

  spin_lock(&virtio_blk_lock);

  g_hdr.type = type;
  g_hdr.reserved = 0;
  g_hdr.sector = lba;
  g_status = 0xFF;

  vq_desc[0].addr = (uint64_t)(uintptr_t)&g_hdr;
  vq_desc[0].len = sizeof(g_hdr);
  vq_desc[0].flags = VIRTQ_DESC_F_NEXT;
  vq_desc[0].next = 1;

  vq_desc[1].addr = (uint64_t)(uintptr_t)buf;
  vq_desc[1].len = bytes;
  vq_desc[1].flags =
      VIRTQ_DESC_F_NEXT | (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0);
  vq_desc[1].next = 2;

  vq_desc[2].addr = (uint64_t)(uintptr_t)&g_status;
  vq_desc[2].len = 1;
  vq_desc[2].flags = VIRTQ_DESC_F_WRITE;
  vq_desc[2].next = 0;

  int rc = submit_chain(0);
  spin_unlock(&virtio_blk_lock);
  return rc;
}

static int virtio_blk_read_sectors(uint64_t lba, uint16_t count, void *buf) {
  return submit_request(VIRTIO_BLK_T_IN, lba, buf, (uint32_t)count * 512u);
}

static int virtio_blk_write_sectors(uint64_t lba, uint16_t count,
                                    const void *buf) {
  return submit_request(VIRTIO_BLK_T_OUT, lba, (void *)buf,
                        (uint32_t)count * 512u);
}

static int virtio_blk_flush(void) {
  if (!g_iobase)
    return -1;

  spin_lock(&virtio_blk_lock);

  g_hdr.type = VIRTIO_BLK_T_FLUSH;
  g_hdr.reserved = 0;
  g_hdr.sector = 0;
  g_status = 0xFF;

  vq_desc[0].addr = (uint64_t)(uintptr_t)&g_hdr;
  vq_desc[0].len = sizeof(g_hdr);
  vq_desc[0].flags = VIRTQ_DESC_F_NEXT;
  vq_desc[0].next = 1;

  vq_desc[1].addr = (uint64_t)(uintptr_t)&g_status;
  vq_desc[1].len = 1;
  vq_desc[1].flags = VIRTQ_DESC_F_WRITE;
  vq_desc[1].next = 0;

  int rc = submit_chain(0);
  spin_unlock(&virtio_blk_lock);
  return rc;
}

static int vio_read(block_device_t *bd, uint64_t lba, uint32_t count,
                    void *buf) {
  (void)bd;
  uint8_t *p = (uint8_t *)buf;
  while (count > 0) {
    uint32_t chunk = count > 256 ? 256 : count;
    if (virtio_blk_read_sectors(lba, (uint16_t)chunk, p) != 0)
      return -1;
    lba += chunk;
    p += chunk * 512;
    count -= chunk;
  }
  return 0;
}
static int vio_write(block_device_t *bd, uint64_t lba, uint32_t count,
                     const void *buf) {
  (void)bd;
  const uint8_t *p = (const uint8_t *)buf;
  while (count > 0) {
    uint32_t chunk = count > 256 ? 256 : count;
    if (virtio_blk_write_sectors(lba, (uint16_t)chunk, p) != 0)
      return -1;
    lba += chunk;
    p += chunk * 512;
    count -= chunk;
  }
  return 0;
}

static int vio_flush(block_device_t *bd) {
  (void)bd;
  return virtio_blk_flush();
}

static block_device_t g_vio_bd = {
    .name = "vda",
    .block_size = 512,
    .read_blocks = vio_read,
    .write_blocks = vio_write,
    .flush = vio_flush,
};
