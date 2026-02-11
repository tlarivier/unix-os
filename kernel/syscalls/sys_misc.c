#include "syscall.h"
#include <kernel/kstring.h>
#include <kernel/io.h>
#include <kernel/framebuffer.h>
#include <kernel/timer.h>
#include <kernel/keyboard.h>

#define KDGKBMODE 0x4B44  
#define KDSKBMODE 0x4B45  
#define K_RAW     0x00    
#define K_XLATE   0x01    

int32_t sys_uname(uint32_t buf, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    
    if (!buf) return -EINVAL;
    
    struct utsname uts = {0};
    kstrncpy(uts.sysname,  "UnixOS",    sizeof(uts.sysname));
    kstrncpy(uts.nodename, "localhost", sizeof(uts.nodename));
    kstrncpy(uts.release,  "0.4.0",     sizeof(uts.release));
    kstrncpy(uts.version,  "#1 SMP",    sizeof(uts.version));
    kstrncpy(uts.machine,  "i686",      sizeof(uts.machine));
    
    int rc = copy_to_user((void*)buf, &uts, sizeof(uts));
    return IS_ERROR(rc) ? rc : 0;
}

int32_t sys_ioctl(uint32_t fd, uint32_t req, uint32_t arg, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    /* Framebuffer ioctls (fd would be /dev/fb0) */
    if (req >= 0x4600 && req <= 0x4620) {
        return fb_ioctl(req, (void*)arg);
    }
    
    switch (req) {
        case KDSKBMODE:
            keyboard_set_raw_mode(arg == K_RAW);
            return 0;
        case KDGKBMODE:
            return keyboard_get_raw_mode() ? K_RAW : K_XLATE;
    }
    
    switch (req) {
        case TCGETS:
        case TCSETS:
            return 0;
        case TIOCGWINSZ:
            if (arg) {
                struct winsize ws = {25, 80, 640, 400};
                copy_to_user((void*)arg, &ws, sizeof(ws));
            }
            return 0;
        case TIOCSWINSZ:
            return 0;
        default:
            return -ENOTTY;
    }
    
    (void)fd;
}

extern uint32_t stdin_flags;

int32_t sys_fcntl(uint32_t fd, uint32_t cmd, uint32_t arg, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    switch (cmd) {
        case F_DUPFD:
            return sys_dup(fd, 0, 0, 0, 0);
        case F_GETFD:
        case F_SETFD:
            return 0;
        case F_GETFL:
            if (fd == 0) return (int32_t)stdin_flags;
            return 0;
        case F_SETFL:
            if (fd == 0) { stdin_flags = arg; return 0; }
            return 0;
        default:
            return -EINVAL;
    }
}

int32_t sys_reboot(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    __asm__ volatile("cli");
    outb(0x64, 0xFE);
    while (1) { __asm__ volatile("hlt"); }
    return 0;
}

int32_t sys_select(uint32_t nfds, uint32_t readfds, uint32_t writefds, uint32_t exceptfds, uint32_t timeout) {
    (void)nfds; (void)readfds; (void)writefds; (void)exceptfds;
    if (timeout) {
        struct timeval tv;
        if (copy_from_user(&tv, (void*)timeout, sizeof(tv)) == 0) {
            uint32_t ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
            if (ms > 0) sleep_ms(ms);
        }
    }
    return 0;
}

int32_t sys_poll(uint32_t fds, uint32_t nfds, uint32_t timeout_ms, uint32_t u4, uint32_t u5) {
    (void)fds; (void)nfds; (void)u4; (void)u5;
    if (timeout_ms > 0 && timeout_ms < 60000) sleep_ms(timeout_ms);
    return 0;
}

int32_t sys_getenv(uint32_t name, uint32_t buf, uint32_t size, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    if (!name || !buf || size == 0) return -EINVAL;
    
    char kname[64];
    if (copy_from_user(kname, (void*)name, 63) < 0) return -EFAULT;
    kname[63] = '\0';
    
    const char* val = NULL;
    if (kstrcmp(kname, "HOME") == 0) val = "/";
    else if (kstrcmp(kname, "PATH") == 0) val = "/bin:/sbin";
    else if (kstrcmp(kname, "TERM") == 0) val = "linux";
    else return 0;
    
    uint32_t len = kstrlen(val) + 1;
    if (len > size) len = size;
    copy_to_user((void*)buf, val, len);
    return (int32_t)(len - 1);
}

#include "../../uapi/abi_version.h"

int32_t sys_abi_version(uint32_t u1, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u1; (void)u2; (void)u3; (void)u4; (void)u5;
    return UNIXOS_ABI_VERSION;
}
