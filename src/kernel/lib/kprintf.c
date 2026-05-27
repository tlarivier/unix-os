/*
 * kprintf.c — formatted kernel logger: parses fmt in a stack buffer,
 *             emits to VGA+COM1 under kprintf_lock (irqsave), with a
 *             lock-free kprintf_panic for NMI/panic contexts.
 *
 * Invariants:
 *  - Format parsing uses a 512-byte stack buffer with no shared state.
 *  - The critical section holds spin_lock_irqsave around emission only,
 *    never around parsing.
 *  - Before kprintf_enable_locking(), the lock path is bypassed (BSP
 *    single-CPU early-boot).
 *  - va_end(ap) is called exactly once on every return path.
 *
 * Not allowed:
 *  - Dynamic allocation.
 *  - Re-entry from an NMI/double-fault while the same CPU holds the
 *    lock (use kprintf_panic instead).
 *  - Format strings with unhandled `%` specifiers (emitted raw).
 */

#include <kernel/kprintf.h>
#include <kernel/serial.h>
#include <kernel/spinlock.h>
#include <kernel/vga.h>
#include <stdarg.h>

static spinlock_t kprintf_lock = SPINLOCK_INIT("kprintf");

static volatile int kprintf_lock_ready = 0;

#define KPRINTF_BUFSZ 512

static void buf_putc(char *buf, int *idx, char c) {
  if (*idx < KPRINTF_BUFSZ - 1) {
    buf[(*idx)++] = c;
  }
}

static void buf_put_uint(char *buf, int *idx, uint32_t val, uint32_t base) {
  char tmp[33];
  const char *digits = "0123456789ABCDEF";
  int i = 0;

  if (val == 0) {
    buf_putc(buf, idx, '0');
    return;
  }

  while (val && i < 32) {
    tmp[i++] = digits[val % base];
    val /= base;
  }

  while (i--) {
    buf_putc(buf, idx, tmp[i]);
  }
}

static void buf_put_int(char *buf, int *idx, int32_t v) {
  if (v < 0) {
    buf_putc(buf, idx, '-');
    if (v == -2147483648) {
      buf_put_uint(buf, idx, 2147483648U, 10);
    } else {
      buf_put_uint(buf, idx, (uint32_t)(-v), 10);
    }
  } else {
    buf_put_uint(buf, idx, (uint32_t)v, 10);
  }
}

void kprintf(const char *format, ...) {
  char out[KPRINTF_BUFSZ];
  int n = 0;
  va_list ap;
  va_start(ap, format);

  for (const char *p = format; *p; ++p) {
    if (*p != '%') {
      buf_putc(out, &n, *p);
      continue;
    }

    ++p;
    if (*p == '\0')
      break;

    switch (*p) {
    case '%':
      buf_putc(out, &n, '%');
      break;
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s)
        s = "(null)";
      while (*s)
        buf_putc(out, &n, *s++);
      break;
    }
    case 'u':
      buf_put_uint(out, &n, va_arg(ap, uint32_t), 10);
      break;
    case 'x':
      buf_put_uint(out, &n, va_arg(ap, uint32_t), 16);
      break;
    case 'p': {
      uint32_t ptr = (uint32_t)va_arg(ap, void *);
      buf_putc(out, &n, '0');
      buf_putc(out, &n, 'x');
      buf_put_uint(out, &n, ptr, 16);
      break;
    }
    case 'd':
      buf_put_int(out, &n, va_arg(ap, int32_t));
      break;
    default:
      buf_putc(out, &n, '%');
      buf_putc(out, &n, *p);
      break;
    }
  }

  va_end(ap);

  if (!kprintf_lock_ready) {
    for (int i = 0; i < n; ++i) {
      vga_putchar(out[i]);
      serial_putc(out[i]);
    }
    return;
  }

  uint32_t flags;
  spin_lock_irqsave(&kprintf_lock, &flags);
  for (int i = 0; i < n; ++i) {
    vga_putchar(out[i]);
    serial_putc(out[i]);
  }
  spin_unlock_irqrestore(&kprintf_lock, flags);
}

void kprintf_enable_locking(void) { kprintf_lock_ready = 1; }

void kprintf_panic(const char *msg) {
  if (!msg)
    return;
  while (*msg) {
    serial_putc(*msg++);
  }
}
