#include <kernel/timer.h>
#include <kernel/io.h>
#include <kernel/ports.h>
#include <kernel/scheduler.h>
#include <kernel/process.h>
#include <stdint.h>

#define SIGALRM 14

#define TIMER_FREQUENCY_HZ 100

static volatile uint32_t timer_ticks = 0;
static volatile uint32_t seconds = 0;

static inline uint32_t atomic_read_ticks(void) {
    uint32_t ticks;
    uint32_t flags;
    __asm__ volatile("pushf; cli; pop %0" : "=r"(flags));
    ticks = timer_ticks;
    __asm__ volatile("push %0; popf" : : "r"(flags));
    return ticks;
}

static inline uint32_t atomic_read_seconds(void) {
    uint32_t secs;
    uint32_t flags;
    __asm__ volatile("pushf; cli; pop %0" : "=r"(flags));
    secs = seconds;
    __asm__ volatile("push %0; popf" : : "r"(flags));
    return secs;
}

void timer_init(uint32_t frequency) {
    uint32_t divisor = 1193180 / frequency;
    
    outb(PIT_CMD, 0x36);
    outb(PIT_CH0_DATA, divisor & 0xFF);
    outb(PIT_CH0_DATA, (divisor >> 8) & 0xFF);
}

void timer_handle_irq(void) {
    timer_ticks++;
    
    if (timer_ticks % TIMER_FREQUENCY_HZ == 0) {
        seconds++;
    }
    
    process_t *proc = get_current_process();
    if (proc && proc->alarm_ticks > 0) {
        proc->alarm_ticks--;
        if (proc->alarm_ticks == 0) {
            proc->signal_pending |= (1U << SIGALRM);
        }
    }
    
    extern void timer_tick(void);
    timer_tick();
}

uint32_t get_timer_ticks(void) {
    return atomic_read_ticks();
}

uint32_t get_seconds(void) {
    return atomic_read_seconds();
}

void sleep_ms(uint32_t ms) {
    if (ms == 0) return;
    uint32_t start = atomic_read_ticks();
    uint32_t ticks_to_wait = (ms * TIMER_FREQUENCY_HZ) / 1000;
    if (ticks_to_wait == 0) ticks_to_wait = 1;
    
    while ((atomic_read_ticks() - start) < ticks_to_wait) {
        __asm__ volatile("hlt");
    }
}

void sleep_ticks(uint32_t ticks) {
    if (ticks == 0) return;
    uint32_t start = atomic_read_ticks();
    while ((atomic_read_ticks() - start) < ticks) {
        __asm__ volatile("hlt");
    }
}
