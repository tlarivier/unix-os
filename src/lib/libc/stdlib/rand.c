/*
 * rand.c — libc `rand`/`srand`: glibc-style LCG PRNG (a=1103515245, c=12345).
 *
 * Invariants:
 *  - Single global seed `rand_seed`; initial value 1 per POSIX startup.
 *  - rand() returns a value in [0, RAND_MAX] with RAND_MAX == 0x7fff.
 *  - Deterministic given any srand() seed.
 *
 * Not allowed:
 *  - Claiming cryptographic strength — this is a non-secure PRNG.
 *  - Adding thread synchronization (userspace is single-threaded).
 */

static unsigned int rand_seed = 1;

void srand(unsigned int seed) { rand_seed = seed; }

int rand(void) {
  rand_seed = rand_seed * 1103515245 + 12345;
  return (rand_seed >> 16) & 0x7fff;
}
