#ifndef KERNEL_KASAN_H
#define KERNEL_KASAN_H

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_KASAN_LITE

#define KASAN_CANARY_HEAD 0xDEAD5A5A
#define KASAN_CANARY_TAIL 0x5A5ADEAD
#define KASAN_POISON_WORD 0xDEADBEEF
#define KASAN_CANARY_BYTES 8

void *kasan_alloc_register(void *raw_block, size_t raw_size, size_t user_size);
void *kasan_free_check(void *user_ptr, size_t *out_raw_size);
void kasan_check_poison(void *raw_block, size_t bytes);
void kasan_poison_range(void *p, size_t bytes);
static inline size_t kasan_raw_size(size_t user_size) {
  return user_size + 2 * KASAN_CANARY_BYTES;
}

#else /* !CONFIG_KASAN_LITE */

static inline void *kasan_alloc_register(void *raw, size_t rs, size_t us) {
  (void)rs;
  (void)us;
  return raw;
}
static inline void *kasan_free_check(void *user, size_t *out_rs) {
  if (out_rs)
    *out_rs = 0;
  return user;
}
static inline void kasan_check_poison(void *p, size_t n) {
  (void)p;
  (void)n;
}
static inline void kasan_poison_range(void *p, size_t n) {
  (void)p;
  (void)n;
}
static inline size_t kasan_raw_size(size_t user_size) { return user_size; }

#endif /* CONFIG_KASAN_LITE */

#endif
