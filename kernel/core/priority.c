#include <kernel/process.h>
#include <kernel/errno.h>

void process_init_priority(process_t *proc) {
    if (!proc) return;
    proc->priority = 20;  /* Default nice 0 */
    proc->time_slice = 100;
}

int sys_nice(int increment) {
    process_t *p = get_current_process();
    if (!p) return -ESRCH;
    int new_prio = (int)p->priority + increment;
    if (new_prio < 0) new_prio = 0;
    if (new_prio > 39) new_prio = 39;
    p->priority = (uint32_t)new_prio;
    return new_prio - 20;  /* Return nice value */
}

int sys_getpriority(int which, int who) {
    (void)which; (void)who;
    process_t *p = get_current_process();
    if (!p) return -ESRCH;
    return 20 - (int)p->priority;
}

int sys_setpriority(int which, int who, int prio) {
    (void)which; (void)who;
    process_t *p = get_current_process();
    if (!p) return -ESRCH;
    if (prio < 0) prio = 0;
    if (prio > 39) prio = 39;
    p->priority = (uint32_t)prio;
    return 0;
}
