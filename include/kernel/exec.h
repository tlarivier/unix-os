#ifndef KERNEL_EXEC_H
#define KERNEL_EXEC_H

#include <kernel/process.h>
#include <stdint.h>

int setup_exec_user_stack(process_t *proc, char **argv, int argc, char **envp,
                          int envc);

#endif
