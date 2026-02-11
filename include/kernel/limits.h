#ifndef KERNEL_LIMITS_H
#define KERNEL_LIMITS_H

#include <../uapi/resource.h>

int sys_getrlimit(int resource, struct rlimit *rlim);
int sys_setrlimit(int resource, const struct rlimit *rlim);

#endif 
