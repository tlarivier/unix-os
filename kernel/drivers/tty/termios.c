#include <stdint.h>
#include <stddef.h>
#include <kernel/errno.h>
#include <kernel/uaccess.h>
#include <kernel/process.h>
#include <uapi/termios.h>

static struct termios default_termios = {
    .c_iflag = ICRNL | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CS8 | CREAD | CLOCAL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
    .c_line = 0,
    .c_cc = {
        [VINTR]    = 3,    /* ^C */
        [VQUIT]    = 28,   /* ^\ */
        [VERASE]   = 127,  /* DEL */
        [VKILL]    = 21,   /* ^U */
        [VEOF]     = 4,    /* ^D */
        [VTIME]    = 0,
        [VMIN]     = 1,
        [VSWTC]    = 0,
        [VSTART]   = 17,   /* ^Q */
        [VSTOP]    = 19,   /* ^S */
        [VSUSP]    = 26,   /* ^Z */
        [VEOL]     = 0,
        [VREPRINT] = 18,   /* ^R */
        [VDISCARD] = 15,   /* ^O */
        [VWERASE]  = 23,   /* ^W */
        [VLNEXT]   = 22,   /* ^V */
        [VEOL2]    = 0,
    },
    .c_ispeed = B9600,
    .c_ospeed = B9600,
};

static struct termios console_termios;
static struct winsize console_winsize = {
    .ws_row    = 25,
    .ws_col    = 80,
    .ws_xpixel = 640,
    .ws_ypixel = 400,
};
static pid_t console_pgrp = 1;

void termios_init(void) {
    console_termios = default_termios;
}

static int is_tty(int fd) {
    return (fd >= 0 && fd <= 2);
}

int32_t sys_tcgetattr(uint32_t fd, uint32_t termios_ptr, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u3; (void)u4; (void)u5;
    
    if (!is_tty((int)fd)) {
        return -ENOTTY;
    }
    
    if (!termios_ptr) {
        return -EINVAL;
    }
    
    int rc = copy_to_user((void*)termios_ptr, &console_termios, sizeof(struct termios));
    if (IS_ERROR(rc)) {
        return rc;
    }
    
    return 0;
}

int32_t sys_tcsetattr(uint32_t fd, uint32_t actions, uint32_t termios_ptr, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!is_tty((int)fd)) {
        return -ENOTTY;
    }
    
    if (!termios_ptr) {
        return -EINVAL;
    }
    
    if (actions != TCSANOW && actions != TCSADRAIN && actions != TCSAFLUSH) {
        return -EINVAL;
    }
    
    struct termios new_termios;
    int rc = copy_from_user(&new_termios, (void*)termios_ptr, sizeof(struct termios));
    if (IS_ERROR(rc)) {
        return rc;
    }
    
    console_termios = new_termios;
    
    return 0;
}

int32_t sys_isatty(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4, uint32_t u5) {
    (void)u2; (void)u3; (void)u4; (void)u5;
    return is_tty((int)fd) ? 1 : 0;
}

int32_t sys_ttyname(uint32_t fd, uint32_t buf, uint32_t buflen, uint32_t u4, uint32_t u5) {
    (void)u4; (void)u5;
    
    if (!is_tty((int)fd)) {
        return -ENOTTY;
    }
    
    const char *name = "/dev/console";
    size_t len = 13;  
    
    if (buflen < len) {
        return -ERANGE;
    }
    
    int rc = copy_to_user((void*)buf, name, len);
    if (IS_ERROR(rc)) {
        return rc;
    }
    
    return 0;
}

int termios_ioctl(int fd, unsigned long request, void *argp) {
    if (!is_tty(fd)) {
        return -ENOTTY;
    }
    
    switch (request) {
        case TCGETS:
            return copy_to_user(argp, &console_termios, sizeof(struct termios));
            
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
            return copy_from_user(&console_termios, argp, sizeof(struct termios));
            
        case TIOCGWINSZ:
            return copy_to_user(argp, &console_winsize, sizeof(struct winsize));
            
        case TIOCSWINSZ: {
            struct winsize ws;
            int rc = copy_from_user(&ws, argp, sizeof(struct winsize));
            if (IS_ERROR(rc)) return rc;
            console_winsize = ws;
            return 0;
        }
        
        case TIOCGPGRP:
            return copy_to_user(argp, &console_pgrp, sizeof(pid_t));
            
        case TIOCSPGRP: {
            pid_t pgrp;
            int rc = copy_from_user(&pgrp, argp, sizeof(pid_t));
            if (IS_ERROR(rc)) return rc;
            console_pgrp = pgrp;
            return 0;
        }
        
        case TIOCSCTTY:
            /* Set controlling terminal - simplified */
            return 0;
            
        default:
            return -EINVAL;
    }
}

struct termios* termios_get_console(void) {
    return &console_termios;
}

int termios_is_canonical(void) {
    return (console_termios.c_lflag & ICANON) != 0;
}

int termios_is_echo(void) {
    return (console_termios.c_lflag & ECHO) != 0;
}

int termios_is_isig(void) {
    return (console_termios.c_lflag & ISIG) != 0;
}

cc_t termios_get_cc(int idx) {
    if (idx < 0 || idx >= NCCS) return 0;
    return console_termios.c_cc[idx];
}
