#ifndef UAPI_SIGACTION_H
#define UAPI_SIGACTION_H

#include <stdint.h>
#include <kernel/types.h>

#define SA_NOCLDSTOP  0x00000001  /* Don't send SIGCHLD when children stop */
#define SA_NOCLDWAIT  0x00000002  /* Don't create zombies */
#define SA_SIGINFO    0x00000004  /* Use sa_sigaction instead of sa_handler */
#define SA_ONSTACK    0x08000000  /* Use alternate signal stack */
#define SA_RESTART    0x10000000  /* Restart syscall on signal return */
#define SA_NODEFER    0x40000000  /* Don't block signal during handler */
#define SA_RESETHAND  0x80000000  /* Reset to SIG_DFL on entry */

#ifndef _SIGINFO_T_DEFINED
#define _SIGINFO_T_DEFINED
typedef struct siginfo siginfo_t;
#endif

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t*, void*);
    };
    sigset_t   sa_mask;    
    int        sa_flags;   
    void     (*sa_restorer)(void);  
};

typedef struct sigaltstack {
    void  *ss_sp;     
    int    ss_flags;  
    size_t ss_size;   
} stack_t;

#define SS_ONSTACK  1  
#define SS_DISABLE  2  

#define MINSIGSTKSZ 2048
#define SIGSTKSZ    8192

#define sigemptyset(set)      (*(set)  = 0)
#define sigfillset(set)       (*(set)  = ~(sigset_t)0)
#define sigaddset(set, sig)   (*(set) |= (1U  << ((sig) - 1)))
#define sigdelset(set, sig)   (*(set) &= ~(1U << ((sig) - 1)))
#define sigismember(set, sig) ((*(set) & (1U  << ((sig) - 1))) != 0)

#endif 
