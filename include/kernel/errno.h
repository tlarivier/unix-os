#ifndef KERNEL_ERRNO_H
#define KERNEL_ERRNO_H

#include <../uapi/errno.h>

#define IS_ERROR(x)     ((x) < 0)
#define IS_SUCCESS(x)   ((x) >= 0)
#define ERROR_CODE(x)   (IS_ERROR(x) ? (x) : 0)

#define ERRNO(err)      (-(err))
#define ABS_ERRNO(err)  (-(err))
#define KERNEL_SUCCESS  0

#endif 
