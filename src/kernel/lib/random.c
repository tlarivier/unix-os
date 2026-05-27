/*
 * random.c — non-cryptographic kernel PRNG (xorshift32 + small XOR
 *            entropy pool seeded by rdtsc); intended for ASLR jitter
 *            and canary salts only.
 *
 * Invariants:
 *  - Non-cryptographic: never used for keys, nonces, or MACs.
 *  - __random_state is _Atomic; random32 advances it via a
 *    compare-exchange loop so concurrent CPUs do not lose bits.
 *  - random_init must run once before any consumer call, otherwise the
 *    seed stays at the deterministic 0xDEADBEEF.
 *
 * Not allowed:
 *  - Claiming cryptographic strength (no random_secure alias).
 *  - Allocating, taking a lock, or calling kprintf.
 */

#include <kernel/random.h>
#include <stdint.h>

_Atomic uint32_t __random_state = 0xDEADBEEF;

static uint32_t entropy_pool[4] = {0x12345678, 0x9ABCDEF0, 0xFEDCBA98,
                                   0x76543210};
static uint32_t entropy_index = 0;

void random_init(void) {
  uint32_t lo, hi;

  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));

  __atomic_store_n(&__random_state, lo ^ hi ^ 0xDEADBEEF, __ATOMIC_RELAXED);

  entropy_pool[0] ^= lo;
  entropy_pool[1] ^= hi;
  entropy_pool[2] ^= lo ^ hi;
  entropy_pool[3] ^= (lo << 16) | (hi >> 16);

  for (int i = 0; i < 32; i++) {
    random32();
  }
}

static void random_add_entropy(uint32_t val) {
  uint32_t idx = entropy_index++ & 3;
  entropy_pool[idx] ^= val;
  entropy_pool[idx] ^= entropy_pool[(idx + 1) & 3] >> 7;
  entropy_pool[(idx + 2) & 3] ^= val << 13;

  if ((entropy_index & 0xF) == 0) {
    uint32_t mix =
        entropy_pool[0] ^ entropy_pool[1] ^ entropy_pool[2] ^ entropy_pool[3];
    __atomic_xor_fetch(&__random_state, mix, __ATOMIC_RELAXED);
  }
}

uint32_t random_u32(void) {
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));

  random_add_entropy(lo ^ hi);

  uint32_t r = random32();
  r ^= entropy_pool[r & 3];
  r ^= entropy_pool[(r >> 8) & 3] << 8;

  return r;
}
