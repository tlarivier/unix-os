/*
 * sys_time.c — marshalling for the four time syscalls (gettimeofday,
 * nanosleep, clock_gettime, time), delegating all epoch and tick
 * conversion to kernel/drivers/timer/.
 *
 * Invariants:
 *  - Uniform (uint32_t x5) -> int32_t ABI on every wrapper.
 *  - User pointers are accessed only through copy_{from,to}_user.
 *  - Time values come from time_now_{timeval,timespec,epoch_secs}.
 *  - Sleep is delegated to time_sleep_timespec, which writes rem on -EINTR.
 *
 * Not allowed:
 *  - Computing BOOT_EPOCH or ticks-to-µs/ns conversions in this TU.
 *  - Spinning on the PIT or LAPIC timer outside the timer subsystem.
 *  - Returning success without copying the result back to the user buffer.
 */

#include "syscall.h"

#include <kernel/errno.h>
#include <kernel/interrupt.h>
#include <kernel/timer.h>
#include <kernel/uaccess.h>

int32_t sys_gettimeofday(uint32_t tv_ptr, uint32_t tz, uint32_t u3, uint32_t u4,
                         uint32_t u5) {
  (void)tz;
  (void)u3;
  (void)u4;
  (void)u5;
  if (!tv_ptr)
    return -EINVAL;

  struct k_timeval tv;
  time_now_timeval(&tv);

  int rc = copy_to_user((void *)tv_ptr, &tv, sizeof(tv));
  return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_nanosleep(uint32_t req_ptr, uint32_t rem_ptr, uint32_t u3,
                      uint32_t u4, uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  if (!req_ptr)
    return -EINVAL;

  struct k_timespec req;
  int rc = copy_from_user(&req, (void *)req_ptr, sizeof(req));
  if (IS_ERROR(rc))
    return rc;

  struct k_timespec rem = {0, 0};
  time_sleep_timespec(&req, &rem);

  if (rem_ptr)
    copy_to_user((void *)rem_ptr, &rem, sizeof(rem));
  return 0;
}

/* POSIX clock IDs. We don't separate REALTIME and MONOTONIC bookkeeping yet
 * — both read time_now_timespec — but we reject anything we'd silently
 * misinterpret (CPU_TIME, BOOTTIME, etc.). */
#define K_CLOCK_REALTIME  0
#define K_CLOCK_MONOTONIC 1

int32_t sys_clock_gettime(uint32_t clk_id, uint32_t tp, uint32_t u3,
                          uint32_t u4, uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;
  if (!tp)
    return -EINVAL;
  if (clk_id != K_CLOCK_REALTIME && clk_id != K_CLOCK_MONOTONIC)
    return -EINVAL;

  struct k_timespec ts;
  time_now_timespec(&ts);

  int rc = copy_to_user((void *)tp, &ts, sizeof(ts));
  return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_time(uint32_t tloc, uint32_t u2, uint32_t u3, uint32_t u4,
                 uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  uint32_t secs = time_now_epoch_secs();
  if (tloc)
    copy_to_user((void *)tloc, &secs, sizeof(secs));
  return (int32_t)secs;
}
