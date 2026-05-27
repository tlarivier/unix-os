#ifndef KERNEL_CRED_H
#define KERNEL_CRED_H

#include <kernel/process.h>
#include <kernel/types.h>

int cred_set_uid(process_t *proc, uid_t uid);
int cred_set_gid(process_t *proc, gid_t gid);
int cred_set_euid(process_t *proc, uid_t euid);
int cred_set_egid(process_t *proc, gid_t egid);

#endif /* KERNEL_CRED_H */
