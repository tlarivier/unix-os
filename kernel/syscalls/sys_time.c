#include "syscall.h"
#include <kernel/interrupt.h>
#include <kernel/drivers.h>
#include <kernel/timer.h>

struct timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};

static uint32_t boot_time = 1737280000; 

int32_t sys_gettimeofday(uint32_t tv_ptr, uint32_t tz, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)tz; (void)u3; (void)u4; (void)u5;
    
    if (!tv_ptr) return -EINVAL;
    
    uint32_t ticks = get_timer_ticks();
    uint32_t secs = get_seconds();
    
    struct timeval tv = {
        .tv_sec  = boot_time + secs,
        .tv_usec = (ticks % 100) * 10000
    };
    
    int rc = copy_to_user((void*)tv_ptr, &tv, sizeof(tv));
    return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_nanosleep(uint32_t req_ptr, uint32_t rem_ptr, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!req_ptr) return -EINVAL;
    
    struct timespec req;
    int rc = copy_from_user(&req, (void*)req_ptr, sizeof(req));
    if (IS_ERROR(rc)) return rc;
    
    uint32_t ms = req.tv_sec * 1000 + req.tv_nsec / 1000000;
    if (ms == 0 && req.tv_nsec > 0) ms = 1;  /* Minimum 1ms */
    
    sleep_ms(ms);
    
    if (rem_ptr) {
        struct timespec rem = {0, 0};
        copy_to_user((void*)rem_ptr, &rem, sizeof(rem));
    }
    
    return 0;
}

int32_t sys_clock_gettime(uint32_t clk_id, uint32_t tp, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)clk_id; (void)u3; (void)u4; (void)u5;
    
    if (!tp) return -EINVAL;
    
    uint32_t ticks = get_timer_ticks();
    uint32_t secs = get_seconds();
    
    struct timespec ts = {
        .tv_sec  = boot_time + secs,
        .tv_nsec = (ticks % 100) * 10000000
    };
    
    int rc = copy_to_user((void*)tp, &ts, sizeof(ts));
    return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_time(uint32_t tloc, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    uint32_t secs = boot_time + get_seconds();
    
    if (tloc) {
        copy_to_user((void*)tloc, &secs, sizeof(secs));
    }
    
    return (int32_t)secs;
}

#define SIGALRM  14  
#define TIMER_HZ 100 

int32_t sys_alarm(uint32_t seconds, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    process_t *proc = get_current_process();
    if (!proc) return -ESRCH;
    
    uint32_t remaining = 0;
    if (proc->alarm_ticks > 0) {
        remaining = proc->alarm_ticks / TIMER_HZ;
    }
    
    if (seconds > 0) {
        proc->alarm_ticks = seconds * TIMER_HZ;
    } else {
        proc->alarm_ticks = 0;
    }
    
    return (int32_t)remaining;
}
