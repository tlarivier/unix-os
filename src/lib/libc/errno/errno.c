/*
 * errno.c — single global libc `errno` (process-wide, single-threaded).
 *
 * Invariants:
 *  - Exactly one definition of `errno` in the libc; all wrappers use this.
 *  - Set only by POSIX wrappers (__syscall_with_errno and friends) when the
 *    kernel returns a value in [-4095, -1]; never set by the raw _syscall path.
 *  - Initial value is 0 (per POSIX startup contract).
 *
 * Not allowed:
 *  - Writing `errno` from low-level inline __syscallN helpers (they return
 * raw).
 *  - Per-thread `errno` (single-threaded userspace until __thread storage
 * exists).
 */

int errno = 0;
