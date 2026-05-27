#ifndef UAPI_SIGNAL_H
#define UAPI_SIGNAL_H

#include <../uapi/types.h>

#ifndef _PID_T_DEFINED
#define _PID_T_DEFINED
typedef int32_t pid_t;
#endif

#ifndef _UID_T_DEFINED
#define _UID_T_DEFINED
typedef uint32_t uid_t;
#endif

#define SIGNAL_X(name, num, action, desc) name = num,
enum {
#include "signals.def"
  _NSIG_LAST
};
#undef SIGNAL_X

#define _NSIG 65
#define NSIG _NSIG

#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int)) - 1)

typedef void (*sig_handler_t)(int);

typedef uint32_t sigset_t;

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

struct siginfo {
  int si_signo;
  int si_code;
  pid_t si_pid;
  uid_t si_uid;
};

#endif
