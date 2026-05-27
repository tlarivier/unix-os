#ifndef UAPI_ERRNO_H
#define UAPI_ERRNO_H

#define ERRNO_X(name, val, msg) name = val,
enum {
#include "errno.def"
  ERRNO_MAX_VALUE
};
#undef ERRNO_X

#define EWOULDBLOCK EAGAIN
#define EDEADLOCK EDEADLK

const char *strerror(int errnum);

#ifdef __KERNEL__
extern int kernel_errno;
#define errno kernel_errno
#else
extern int errno;
#endif

#endif
