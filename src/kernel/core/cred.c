/*
 * cred.c — POSIX saved-uid credential mutators (setuid / setgid /
 * seteuid / setegid) operating on an explicit process_t.
 *
 * Invariants:
 *  - All four helpers take `proc` explicitly; they never read
 *    get_current_process() or any per-CPU state.
 *  - Root (euid == 0) may set every {uid, euid, suid} (resp. gid trio)
 *    to the requested value; non-root callers may only target one of
 *    {real, effective, saved}, else -EPERM.
 *  - Pure policy: no heap, no lock, no atomic — uid_t/gid_t stores are
 *    word-sized and assumed naturally atomic on x86.
 *
 * Not allowed:
 *  - Calling vfs / kmalloc / scheduler / signal from here.
 *  - Reading credentials of any process other than the one passed in.
 *  - Introducing implicit current() lookups (breaks unit testability).
 */

#include <kernel/errno.h>
#include <kernel/process.h>
#include <kernel/types.h>

int cred_set_uid(process_t *proc, uid_t uid) {
  if (!proc)
    return -ESRCH;
  if (proc->euid == 0) {
    proc->uid = proc->euid = proc->suid = uid;
    return 0;
  }
  if (uid == proc->uid || uid == proc->euid || uid == proc->suid) {
    proc->euid = uid;
    return 0;
  }
  return -EPERM;
}

int cred_set_gid(process_t *proc, gid_t gid) {
  if (!proc)
    return -ESRCH;
  if (proc->euid == 0) {
    proc->gid = proc->egid = proc->sgid = gid;
    return 0;
  }
  if (gid == proc->gid || gid == proc->egid || gid == proc->sgid) {
    proc->egid = gid;
    return 0;
  }
  return -EPERM;
}

int cred_set_euid(process_t *proc, uid_t euid) {
  if (!proc)
    return -ESRCH;
  if (proc->euid == 0 || euid == proc->uid || euid == proc->suid) {
    proc->euid = euid;
    return 0;
  }
  return -EPERM;
}

int cred_set_egid(process_t *proc, gid_t egid) {
  if (!proc)
    return -ESRCH;
  if (proc->euid == 0 || egid == proc->gid || egid == proc->sgid) {
    proc->egid = egid;
    return 0;
  }
  return -EPERM;
}
