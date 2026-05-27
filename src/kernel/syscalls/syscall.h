#ifndef KERNEL_SYSCALL_PRIVATE_H
#define KERNEL_SYSCALL_PRIVATE_H

#include <stdint.h>

typedef int32_t (*syscall_fn_t)(uint32_t, uint32_t, uint32_t, uint32_t,
                                uint32_t);

#endif /* KERNEL_SYSCALL_PRIVATE_H */
