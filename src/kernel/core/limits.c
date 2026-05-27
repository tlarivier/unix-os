/*
 * limits.c — sys_getrlimit / sys_setrlimit stub layer that reports
 *            RLIM_INFINITY and accepts-but-ignores writes (no quota yet).
 *
 * Invariants:
 *  - Every getrlimit answer is RLIM_INFINITY for any valid resource id.
 *  - setrlimit validates the resource id but performs no real bookkeeping;
 *    no per-process storage is consumed today.
 *  - copy_to_user is the only kernel/user boundary touched; no kernel
 *    state is mutated and no other subsystem is reached.
 *
 * Not allowed:
 *  - Allocating per-process limit tables until the design lands.
 *  - Reading from userspace without copy_from_user (none today — the
 *    setter discards its input intentionally).
 *  - Returning success silently on an invalid resource id (must EINVAL).
 */

#include <kernel/errno.h>
#include <kernel/process.h>
#include <kernel/uaccess.h>
#include <stddef.h>
#include <uapi/resource.h>

int sys_getrlimit(int resource, struct rlimit *rlim) {
  if (resource < 0 || resource >= RLIM_NLIMITS || !rlim)
    return -EINVAL;

  struct rlimit krlim = {RLIM_INFINITY, RLIM_INFINITY};
  int rc = copy_to_user(rlim, &krlim, sizeof(krlim));
  return (rc < 0) ? rc : 0;
}

int sys_setrlimit(int resource, const struct rlimit *rlim) {
  if (resource < 0 || resource >= RLIM_NLIMITS || !rlim)
    return -EINVAL;
  return 0;
}
