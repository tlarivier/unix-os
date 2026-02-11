#include <stddef.h>

#define SYSCALL_X(name, nr, nargs) [nr] = #name,
static const char *syscall_names[256] = {
#include "../../uapi/syscalls.def"
};
#undef SYSCALL_X

const char *syscall_name(int nr) {
    if (nr < 0 || nr >= 256 || syscall_names[nr] == NULL) {
        return "unknown";
    }
    return syscall_names[nr];
}
