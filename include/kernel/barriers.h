#ifndef KERNEL_BARRIERS_H
#define KERNEL_BARRIERS_H

#define barrier() __asm__ volatile("" ::: "memory")
#define smp_mb() __asm__ volatile("mfence" ::: "memory")
#define smp_rmb() barrier()
#define smp_wmb() barrier()
#define cpu_relax() __asm__ volatile("pause" ::: "memory")
#define READ_ONCE(x) (*(const volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, v) (*(volatile typeof(x) *)&(x) = (v))

#endif
