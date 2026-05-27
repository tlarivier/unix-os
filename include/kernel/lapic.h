#ifndef KERNEL_LAPIC_H
#define KERNEL_LAPIC_H

#include <stdint.h>

#define LAPIC_PHYS_DEFAULT 0xFEE00000
#define LAPIC_TIMER_VECTOR 0xEF

int lapic_init(void);
void lapic_send_ipi(uint32_t target_apic_id, uint8_t vector);
void lapic_send_init(uint32_t target_apic_id);
void lapic_send_startup(uint32_t target_apic_id, uint8_t trampoline_vector);
void lapic_eoi(void);
void lapic_timer_start(uint32_t initial_count);
void lapic_timer_calibrate_setup(void);
void lapic_timer_calibrate_reload(void);
uint32_t lapic_timer_calibrate_remaining(void);
void lapic_timer_calibrate_stop(void);

#endif
