#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

#define TIMER_HZ 100

#define UNIXOS_BOOT_EPOCH_S 1737280000u

void timer_init(uint32_t frequency);
uint32_t get_timer_ticks(void);
uint32_t get_seconds(void);
void sleep_ms(uint32_t ms);
void clocksource_tick(void);
static inline uint32_t timer_subsec_usec(uint32_t ticks) {
  return (ticks % TIMER_HZ) * (1000000u / TIMER_HZ);
}
static inline uint32_t timer_subsec_nsec(uint32_t ticks) {
  return (ticks % TIMER_HZ) * (1000000000u / TIMER_HZ);
}

struct k_timeval {
  int32_t tv_sec;
  int32_t tv_usec;
};

struct k_timespec {
  int32_t tv_sec;
  int32_t tv_nsec;
};

static inline uint32_t time_now_epoch_secs(void) {
  return UNIXOS_BOOT_EPOCH_S + get_seconds();
}

static inline int time_now_timeval(struct k_timeval *tv) {
  uint32_t ticks = get_timer_ticks();
  tv->tv_sec = (int32_t)time_now_epoch_secs();
  tv->tv_usec = (int32_t)timer_subsec_usec(ticks);
  return 0;
}

static inline int time_now_timespec(struct k_timespec *ts) {
  uint32_t ticks = get_timer_ticks();
  ts->tv_sec = (int32_t)time_now_epoch_secs();
  ts->tv_nsec = (int32_t)timer_subsec_nsec(ticks);
  return 0;
}

int time_sleep_timespec(const struct k_timespec *req, struct k_timespec *rem);

#endif
