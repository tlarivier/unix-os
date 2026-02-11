#ifndef KERNEL_TIMER_H
#define KERNEL_TIMER_H

#include <stdint.h>

void timer_init(uint32_t frequency);
void timer_handle_irq(void);
uint32_t get_timer_ticks(void);
uint32_t get_seconds(void);
void sleep_ms(uint32_t ms);

#endif 
