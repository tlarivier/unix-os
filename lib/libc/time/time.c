#include <stddef.h>
#include <stdint.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
#define __NR_time          60
#define __NR_nanosleep     61
#define __NR_gettimeofday  62
#define __NR_clock_gettime 63

typedef long time_t;
typedef long suseconds_t;
typedef long clock_t;

struct timeval {
    time_t tv_sec;
    suseconds_t tv_usec;
};

struct timespec {
    time_t tv_sec;
    long tv_nsec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1
#define CLOCKS_PER_SEC  100

time_t time(time_t *tloc) {
    time_t t = _syscall(__NR_time, (long)tloc, 0, 0, 0, 0);
    if (tloc) *tloc = t;
    return t;
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    return _syscall(__NR_gettimeofday, (long)tv, (long)tz, 0, 0, 0);
}

int clock_gettime(int clk_id, struct timespec *tp) {
    return _syscall(__NR_clock_gettime, clk_id, (long)tp, 0, 0, 0);
}

int nanosleep(const struct timespec *req, struct timespec *rem) {
    return _syscall(__NR_nanosleep, (long)req, (long)rem, 0, 0, 0);
}

unsigned int sleep(unsigned int seconds) {
    struct timespec req = { seconds, 0 };
    struct timespec rem = { 0, 0 };
    if (nanosleep(&req, &rem) < 0) return rem.tv_sec;
    return 0;
}

int usleep(unsigned int usec) {
    struct timespec req = { usec / 1000000, (usec % 1000000) * 1000 };
    return nanosleep(&req, NULL);
}

clock_t clock(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return -1;
    return ts.tv_sec * CLOCKS_PER_SEC + ts.tv_nsec / (1000000000 / CLOCKS_PER_SEC);
}
