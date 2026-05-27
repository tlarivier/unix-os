/*
 * sys_misc.c — four leaf syscalls with no natural sibling subsystem:
 * sys_gfx_mode (VGA 13h toggle), sys_gfx_palette (256-entry block upload),
 * sys_kb_event (raw keyboard event pop), sys_reboot (EUID-checked halt).
 *
 * Invariants:
 *  - Uniform (uint32_t x5) -> int32_t ABI on every wrapper.
 *  - sys_reboot requires euid == 0; returns -EPERM otherwise.
 *  - Palette upload copies the full RGB block once (no per-entry
 * copy_from_user).
 *  - User pointers are accessed only through copy_{from,to}_user.
 *
 * Not allowed:
 *  - Driving VGA/keyboard registers from inside the wrapper beyond the
 *    documented reboot fallback (8042 + cli/hlt).
 *  - Returning success without going through the driver entry point.
 */

#include "syscall.h"

#include <kernel/errno.h>
#include <kernel/io.h>
#include <kernel/keyboard.h>
#include <kernel/process.h>
#include <kernel/uaccess.h>
#include <kernel/vga_graphics.h>

int32_t sys_gfx_mode(uint32_t mode, uint32_t u2, uint32_t u3, uint32_t u4,
                     uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  if (mode == 1)
    return fb_open(); /* Mode 13h + default palette */
  if (mode == 0)
    return fb_close(); /* back to text */
  return -EINVAL;
}

int32_t sys_gfx_palette(uint32_t buf, uint32_t start, uint32_t count,
                        uint32_t u4, uint32_t u5) {
  (void)u4;
  (void)u5;
  if (!buf || count == 0)
    return -EINVAL;
  if (start >= 256 || start + count > 256)
    return -EINVAL;

  uint8_t rgb[256 * 3]; /* 768 bytes — fits on syscall kernel stack */
  if (copy_from_user(rgb, (const void *)buf, count * 3) < 0)
    return -EFAULT;

  for (uint32_t i = 0; i < count; i++) {
    vga_set_palette((uint8_t)(start + i), rgb[i * 3 + 0], rgb[i * 3 + 1],
                    rgb[i * 3 + 2]);
  }
  return 0;
}

int32_t sys_kb_event(uint32_t buf, uint32_t u2, uint32_t u3, uint32_t u4,
                     uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  if (!buf)
    return -EINVAL;
  uint8_t evt[2];
  if (!kb_event_pop(&evt[0], &evt[1]))
    return 0;
  return copy_to_user((void *)buf, evt, 2) < 0 ? -EFAULT : 1;
}

int32_t sys_reboot(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u1;
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  process_t *cur = get_current_process();
  if (cur->euid != 0)
    return -EPERM;
  __asm__ volatile("cli");
  outb(0x64, 0xFE); /* PS/2 reset */
  while (1) {
    __asm__ volatile("hlt");
  }
  return 0;
}
