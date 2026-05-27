#ifndef KERNEL_SIGNAL_H
#define KERNEL_SIGNAL_H

#include <uapi/signal.h>

#define NSIG_HANDLED 32

struct process;

void process_init_signals(struct process *proc);
int process_send_signal(pid_t pid, int signal);
int process_set_signal_handler(struct process *proc, int signal,
                               sig_handler_t handler);

int signal_pending_check(void);

#endif
