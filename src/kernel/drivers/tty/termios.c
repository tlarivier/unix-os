/*
 * termios.c — implements the four TTY syscalls (tcgetattr, tcsetattr, isatty,
 * ttyname) over a single global struct termios shared by fd 0/1/2 (console).
 *
 * Invariants:
 *  - console_termios is the sole termios state; mutated only by sys_tcsetattr
 *    and read by sys_tcgetattr.
 *  - is_tty(fd) treats fd in [0,2] as the console; any other fd returns
 * -ENOTTY.
 *  - All userspace pointer transfers go through copy_to_user/copy_from_user
 *    and check IS_ERROR before committing the result.
 *  - termios_init is called once at boot from usermode.c before pid 1 starts.
 *
 * Not allowed:
 *  - Calling vfs_*, schedule, signal_* from this TU.
 *  - Allocating; the termios state is a file-static struct.
 *  - Re-dispatching to the keyboard/serial drivers (no line discipline yet).
 */

#include <kernel/errno.h>
#include <kernel/process.h>
#include <kernel/termios.h>
#include <kernel/uaccess.h>
#include <stddef.h>
#include <stdint.h>
#include <uapi/termios.h>

static struct termios default_termios = {
    .c_iflag = ICRNL | IXON,
    .c_oflag = OPOST | ONLCR,
    .c_cflag = CS8 | CREAD | CLOCAL,
    .c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN,
    .c_line = 0,
    .c_cc =
        {
            [VINTR] = 3,    /* ^C */
            [VQUIT] = 28,   /* ^\ */
            [VERASE] = 127, /* DEL */
            [VKILL] = 21,   /* ^U */
            [VEOF] = 4,     /* ^D */
            [VTIME] = 0,
            [VMIN] = 1,
            [VSWTC] = 0,
            [VSTART] = 17, /* ^Q */
            [VSTOP] = 19,  /* ^S */
            [VSUSP] = 26,  /* ^Z */
            [VEOL] = 0,
            [VREPRINT] = 18, /* ^R */
            [VDISCARD] = 15, /* ^O */
            [VWERASE] = 23,  /* ^W */
            [VLNEXT] = 22,   /* ^V */
            [VEOL2] = 0,
        },
    .c_ispeed = B9600,
    .c_ospeed = B9600,
};

static struct termios console_termios;

void termios_init(void) { console_termios = default_termios; }

static int is_tty(int fd) { return (fd >= 0 && fd <= 2); }

int32_t sys_tcgetattr(uint32_t fd, uint32_t termios_ptr, uint32_t u3,
                      uint32_t u4, uint32_t u5) {
  (void)u3;
  (void)u4;
  (void)u5;

  if (!is_tty((int)fd)) {
    return -ENOTTY;
  }

  if (!termios_ptr) {
    return -EINVAL;
  }

  int rc = copy_to_user((void *)termios_ptr, &console_termios,
                        sizeof(struct termios));
  if (IS_ERROR(rc)) {
    return rc;
  }

  return 0;
}

int32_t sys_tcsetattr(uint32_t fd, uint32_t actions, uint32_t termios_ptr,
                      uint32_t u4, uint32_t u5) {
  (void)u4;
  (void)u5;

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
  int rc =
      copy_from_user(&new_termios, (void *)termios_ptr, sizeof(struct termios));
  if (IS_ERROR(rc)) {
    return rc;
  }

  console_termios = new_termios;

  return 0;
}

int32_t sys_isatty(uint32_t fd, uint32_t u2, uint32_t u3, uint32_t u4,
                   uint32_t u5) {
  (void)u2;
  (void)u3;
  (void)u4;
  (void)u5;
  return is_tty((int)fd) ? 1 : 0;
}

int32_t sys_ttyname(uint32_t fd, uint32_t buf, uint32_t buflen, uint32_t u4,
                    uint32_t u5) {
  (void)u4;
  (void)u5;

  if (!is_tty((int)fd)) {
    return -ENOTTY;
  }

  const char *name = "/dev/console";
  size_t len = 13;

  if (buflen < len) {
    return -ERANGE;
  }

  int rc = copy_to_user((void *)buf, name, len);
  if (IS_ERROR(rc)) {
    return rc;
  }

  return 0;
}
