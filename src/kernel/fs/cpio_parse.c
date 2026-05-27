/*
 * cpio_parse.c — parses the CPIO newc archive baked into the kernel image
 * and stages every entry into ramfs via the VFS API.
 *
 * Invariants:
 *  - Runs once during early userspace bringup with zero block I/O.
 *  - Input bytes come from initramfs_data.h (read-only .rodata blob).
 *  - Each entry is staged with vfs_mkdir (dirs) or vfs_open+vfs_write (files);
 *    parent directories are created on demand via create_parent_dirs.
 *  - All ramfs mutation happens under vfs_lock taken inside the VFS API.
 *
 * Not allowed:
 *  - Calling schedule() during the parse loop.
 *  - Trusting CPIO fields without bounds-checking the embedded archive size.
 *  - Re-running after init userspace is live (boot-only).
 */

#include <kernel/errno.h>
#include <kernel/initramfs.h>
#include <kernel/kprintf.h>
#include <kernel/kstring.h>
#include <kernel/memory.h>
#include <kernel/vfs.h>
#include <kernel/vfs_extra.h>
#include <stdint.h>

#include "initramfs_data.h"

typedef struct {
  char c_magic[6];
  char c_ino[8];
  char c_mode[8];
  char c_uid[8];
  char c_gid[8];
  char c_nlink[8];
  char c_mtime[8];
  char c_filesize[8];
  char c_devmajor[8];
  char c_devminor[8];
  char c_rdevmajor[8];
  char c_rdevminor[8];
  char c_namesize[8];
  char c_check[8];
} cpio_newc_header_t;

#define CPIO_MAGIC "070701"
#define CPIO_TRAILER "TRAILER!!!"

#ifndef O_CREAT
#define O_CREAT 0x0040
#endif
#ifndef O_WRONLY
#define O_WRONLY 1
#endif
#ifndef O_TRUNC
#define O_TRUNC 0x0200
#endif

void create_parent_dirs(const char *path);

static uint32_t parse_hex8(const char *s) {
  uint32_t val = 0;
  for (int i = 0; i < 8; i++) {
    char c = s[i];
    val <<= 4;
    if (c >= '0' && c <= '9')
      val |= (c - '0');
    else if (c >= 'a' && c <= 'f')
      val |= (c - 'a' + 10);
    else if (c >= 'A' && c <= 'F')
      val |= (c - 'A' + 10);
  }
  return val;
}

static int initramfs_load(const void *data, size_t size) {
  const uint8_t *ptr = (const uint8_t *)data;
  const uint8_t *end = ptr + size;
  int file_count = 0;

  if (!data || size < sizeof(cpio_newc_header_t)) {
    return -EINVAL;
  }

  kprintf("Loading initramfs (%d bytes)...\n", (int)size);

  while (1) {
    if ((size_t)(end - ptr) < sizeof(cpio_newc_header_t))
      break;

    const cpio_newc_header_t *hdr = (const cpio_newc_header_t *)ptr;

    if (kstrncmp(hdr->c_magic, CPIO_MAGIC, 6) != 0) {
      kprintf("initramfs: invalid CPIO magic at offset %d\n",
              (int)(ptr - (const uint8_t *)data));
      break;
    }

    uint32_t mode = parse_hex8(hdr->c_mode);
    uint32_t filesize = parse_hex8(hdr->c_filesize);
    uint32_t namesize = parse_hex8(hdr->c_namesize);

    if (namesize == 0 || namesize > 256) {
      kprintf("initramfs: invalid namesize=%u\n", namesize);
      return -EINVAL;
    }

    uint32_t name_padded = (sizeof(cpio_newc_header_t) + namesize + 3) & ~3u;
    if (name_padded > (uint32_t)(end - ptr)) {
      kprintf("initramfs: name area exceeds buffer\n");
      return -EINVAL;
    }

    const char *name = (const char *)(ptr + sizeof(cpio_newc_header_t));

    int name_nul_ok = 0;
    for (uint32_t i = 0; i < namesize; i++) {
      if (name[i] == '\0') {
        name_nul_ok = 1;
        break;
      }
    }
    if (!name_nul_ok) {
      kprintf("initramfs: name not null-terminated within namesize\n");
      return -EINVAL;
    }

    uint32_t data_padded = (filesize + 3) & ~3u;
    const uint8_t *file_data = ptr + name_padded;
    if (filesize > (uint32_t)(end - file_data)) {
      kprintf("initramfs: filesize exceeds buffer\n");
      return -EINVAL;
    }
    if (data_padded > (uint32_t)(end - file_data)) {
      kprintf("initramfs: padded data exceeds buffer\n");
      return -EINVAL;
    }

    if (kstrcmp(name, CPIO_TRAILER) == 0) {
      kprintf("initramfs: found TRAILER, stopping\n");
      break;
    }

    if (name[0] == '.' &&
        (name[1] == '\0' || (name[1] == '/' && name[2] == '\0'))) {
      ptr += name_padded + data_padded;
      continue;
    }

    char path[256];
    int i = 0, j = 1;
    path[i++] = '/';
    while (name[j] && i < 255) {
      if (name[j] == '/' && i == 1) {
        j++;
        continue;
      }
      path[i++] = name[j++];
    }
    path[i] = '\0';

    if ((mode & 0170000) == 0040000) {
      create_parent_dirs(path);
      vfs_mkdir(path, mode & 0777);
    } else if ((mode & 0170000) == 0100000) {
      create_parent_dirs(path);

      int fd = vfs_open(path, O_CREAT | O_WRONLY, mode & 0777);
      if (fd >= 0) {
        if (filesize > 0) {
          ssize_t written = vfs_write(fd, file_data, filesize);
          if (written != (ssize_t)filesize) {
            kprintf("initramfs: write error for %s: wrote %d/%d\n", path,
                    (int)written, (int)filesize);
          }
        }
        vfs_close(fd);
        file_count++;
      } else {
        kprintf("initramfs: failed to open %s: %d\n", path, fd);
      }
    }

    ptr += name_padded + data_padded;
  }

  kprintf("Loaded %d files from initramfs\n", file_count);
  return file_count;
}

int initramfs_init(void) {
  kprintf("Loading from embedded initramfs...\n");
  return initramfs_load(initramfs_data, initramfs_size);
}
