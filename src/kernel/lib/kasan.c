/*
 * kasan.c — KASAN-lite heap corruption detector: 8B canaries head/tail,
 *           poison on free, validate on next alloc of the same slot.
 *
 * Invariants:
 *  - Head canary = (0xDEAD5A5A, user_size); tail canary =
 *    (0x5A5ADEAD, 0x5A5ADEAD); both 8 bytes.
 *  - Freed user region is filled with 0xDEADBEEF (head + user + tail).
 *  - Any canary or poison mismatch triggers immediate panic — no
 *    soft-warn mode.
 *  - No shadow byte; only alloc/free events are instrumented.
 *
 * Not allowed:
 *  - Calling kmalloc/slub_alloc/heap_alloc (would recurse with heap).
 *  - Taking any lock (callable from IRQ via kfree).
 *  - Returning without panicking on corruption.
 */

#include <kernel/kasan.h>
#include <kernel/kernel.h>
#include <kernel/kprintf.h>
#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_KASAN_LITE

static void write_head_canary(uint8_t *raw, uint32_t user_size) {
  ((uint32_t *)raw)[0] = KASAN_CANARY_HEAD;
  ((uint32_t *)raw)[1] = user_size;
}

static void write_tail_canary(uint8_t *tail) {
  ((uint32_t *)tail)[0] = KASAN_CANARY_TAIL;
  ((uint32_t *)tail)[1] = KASAN_CANARY_TAIL;
}

static int validate_head_canary(uint8_t *raw, uint32_t *out_user_size) {
  uint32_t magic = ((uint32_t *)raw)[0];
  if (magic != KASAN_CANARY_HEAD)
    return 0;
  if (out_user_size)
    *out_user_size = ((uint32_t *)raw)[1];
  return 1;
}

static int validate_tail_canary(uint8_t *tail) {
  return ((uint32_t *)tail)[0] == KASAN_CANARY_TAIL &&
         ((uint32_t *)tail)[1] == KASAN_CANARY_TAIL;
}

static void poison_region(uint8_t *p, size_t bytes) {
  uint32_t *p32 = (uint32_t *)p;
  size_t n32 = bytes / 4;
  for (size_t i = 0; i < n32; i++)
    p32[i] = KASAN_POISON_WORD;
  uint8_t *rest = p + n32 * 4;
  for (size_t i = 0; i < bytes - n32 * 4; i++)
    rest[i] = (KASAN_POISON_WORD >> (i * 8)) & 0xFF;
}

void *kasan_alloc_register(void *raw_block, size_t raw_size, size_t user_size) {
  if (!raw_block)
    return NULL;
  if (raw_size < user_size + 2 * KASAN_CANARY_BYTES) {
    kprintf("kasan: raw block %p too small (%u) for user %u + canaries\n",
            raw_block, (unsigned)raw_size, (unsigned)user_size);
    kernel_panic("kasan: undersized allocation", __FILE__, __LINE__);
  }

  uint8_t *raw = (uint8_t *)raw_block;
  write_head_canary(raw, (uint32_t)user_size);

  uint8_t *user = raw + KASAN_CANARY_BYTES;
  uint8_t *tail = user + user_size;
  write_tail_canary(tail);

  return user;
}

void kasan_check_poison(void *raw_block, size_t bytes) {
  if (!raw_block || bytes == 0)
    return;
  uint32_t *p32 = (uint32_t *)raw_block;
  size_t n32 = bytes / 4;
  for (size_t i = 0; i < n32; i++) {
    if (p32[i] != KASAN_POISON_WORD) {
      kprintf("\n*** KASAN: use-after-free at %p (word %u) ***\n",
              (void *)&p32[i], (unsigned)i);
      kprintf("  expected poison: %x, found: %x\n", KASAN_POISON_WORD, p32[i]);
      kernel_panic("kasan: use-after-free", __FILE__, __LINE__);
    }
  }
}

void *kasan_free_check(void *user_ptr, size_t *out_raw_size) {
  if (!user_ptr) {
    if (out_raw_size)
      *out_raw_size = 0;
    return NULL;
  }

  uint8_t *user = (uint8_t *)user_ptr;
  uint8_t *raw = user - KASAN_CANARY_BYTES;

  uint32_t user_size = 0;
  if (!validate_head_canary(raw, &user_size)) {
    kprintf("\n*** KASAN: head canary corrupted at %p ***\n", (void *)raw);
    kprintf("  expected magic: %x, found: %x\n", KASAN_CANARY_HEAD,
            ((uint32_t *)raw)[0]);
    kernel_panic("kasan: head canary", __FILE__, __LINE__);
  }

  uint8_t *tail = user + user_size;
  if (!validate_tail_canary(tail)) {
    kprintf(
        "\n*** KASAN: tail canary corrupted at %p (user_ptr %p, size %u) ***\n",
        (void *)tail, user_ptr, user_size);
    kprintf("  expected: %x %x, found: %x %x\n", KASAN_CANARY_TAIL,
            KASAN_CANARY_TAIL, ((uint32_t *)tail)[0], ((uint32_t *)tail)[1]);
    kernel_panic("kasan: buffer overflow", __FILE__, __LINE__);
  }

  poison_region(raw, user_size + 2 * KASAN_CANARY_BYTES);

  if (out_raw_size)
    *out_raw_size = user_size + 2 * KASAN_CANARY_BYTES;
  return raw;
}

void kasan_poison_range(void *p, size_t bytes) {
  if (!p || bytes == 0)
    return;
  poison_region((uint8_t *)p, bytes);
}

#endif /* CONFIG_KASAN_LITE */
