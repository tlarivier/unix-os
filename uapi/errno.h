#ifndef UAPI_ERRNO_H
#define UAPI_ERRNO_H

#define ERRNO_X(name, val, msg) name = val,
enum {
#include "errno.def"
    ERRNO_MAX_VALUE
};
#undef ERRNO_X

/* Aliases */
#define EWOULDBLOCK EAGAIN
#define EDEADLOCK   EDEADLK

/*
 * strerror() - Get error message string
 * 
 * Implementation in lib/libc/errno.c uses the same X-macro
 */
const char *strerror(int errnum);

/*
 * errno - Thread-local error number
 * For kernel: simple global (single-threaded for now)
 * For userspace: will be thread-local when threads are implemented
 */
#ifdef __KERNEL__
extern int kernel_errno;
#define errno kernel_errno
#else
extern int errno;
#endif

#endif 
