#include <kernel/process.h>
#include <kernel/errno.h>
#include <../uapi/resource.h>

void process_init_limits(process_t *proc) {
    (void)proc;
}

int sys_getrlimit(int resource, struct rlimit *rlim) {
    if (resource < 0 || resource >= RLIM_NLIMITS || !rlim) return -EINVAL;
    
    struct rlimit krlim = { RLIM_INFINITY, RLIM_INFINITY };
    extern int copy_to_user(void* dst, const void* src, size_t n);
    int rc = copy_to_user(rlim, &krlim, sizeof(krlim));
    return (rc < 0) ? rc : 0;
}

int sys_setrlimit(int resource, const struct rlimit *rlim) {
    if (resource < 0 || resource >= RLIM_NLIMITS || !rlim) return -EINVAL;
    return 0;  /* Accept but ignore - no per-process limits */
}
