#include <kernel/capability.h>
#include <kernel/process.h>
#include <kernel/errno.h>

bool capable(int cap) {
    process_t *current = get_current_process();
    if (!current) return false;
    if (current->uid == 0) return true;
    
    switch (cap) {
        case CAP_SYS_NICE: return false;
        case CAP_KILL: return true;
        case CAP_SYS_ADMIN:
        case CAP_SETUID:
        case CAP_SETGID: return false;
        default: return false;
    }
}

void capability_init(void) { }

int cap_grant(process_t *proc, int cap) {
    if (!proc || cap < 0 || cap > CAP_LAST_CAP) return -EINVAL;
    return 0;
}

int cap_revoke(process_t *proc, int cap) {
    if (!proc || cap < 0 || cap > CAP_LAST_CAP) return -EINVAL;
    return 0;
}
