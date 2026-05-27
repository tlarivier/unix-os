#ifndef KERNEL_SMP_H
#define KERNEL_SMP_H

#include <stdint.h>

void smp_init(void);
int smp_bring_up_aps(void);
void ap_main(uint32_t logical_cpu_id);

#define IPI_VEC_RESCHED 0xFC
#define IPI_VEC_TLB_FLUSH 0xFD

void smp_ipi_send(uint32_t target_cpu, uint8_t vector);
void ipi_dispatch(uint32_t vector);
void ipi_dispatch(uint32_t vector);
void smp_tlb_flush_all(void);
void ap_idle_loop(void);
uint32_t lapic_timer_calibrate(void);
extern uint32_t g_lapic_timer_initial;

#endif
