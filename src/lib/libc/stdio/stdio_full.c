/*
 * stdio_full.c — full libc <stdio.h>: FILE*, stdin/stdout/stderr,
 * fopen/fclose/fread/fwrite, the printf/sprintf/snprintf family (callback
 * vformat core), perror, remove/rename. TODO: split into printf.c + stream.c.
 *
 * Invariants:
 *  - All real I/O goes through _syscall (read/write/open/close/lseek) — no
 *    direct int $0x80 here.
 *  - vformat is callback-driven (emit_to_file / emit_to_sbuf) so printf and
 *    snprintf share the same parser.
 *  - sprintf has no size bound by design (POSIX); snprintf truncates safely.
 *
 * Not allowed:
 *  - Holding any global lock (single-threaded userspace).
 *  - Calling allocator paths from inside the format core (only via fopen).
 */

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
extern void *malloc(size_t);
extern void free(void *);

#define __NR_read 10
#define __NR_write 11
#define __NR_open 12
#define __NR_close 13
#define __NR_lseek 14

typedef struct _FILE {
  int fd;
  int eof;
  int error;
  int mode;
} FILE;

static FILE _stdin = {0, 0, 0, 0};
static FILE _stdout = {1, 0, 0, 1};
static FILE _stderr = {2, 0, 0, 1};

FILE *stdin = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

int fputc(int c, FILE *stream) {
  char ch = (char)c;
  if (_syscall(__NR_write, stream->fd, (long)&ch, 1, 0, 0) != 1)
    return -1;
  return c;
}

int putchar(int c) { return fputc(c, stdout); }
int putc(int c, FILE *stream) { return fputc(c, stream); }

int fputs(const char *s, FILE *stream) {
  while (*s)
    if (fputc(*s++, stream) < 0)
      return -1;
  return 0;
}

int puts(const char *s) {
  if (fputs(s, stdout) < 0)
    return -1;
  return fputc('\n', stdout) < 0 ? -1 : 0;
}

int fgetc(FILE *stream) {
  char c;
  int n = _syscall(__NR_read, stream->fd, (long)&c, 1, 0, 0);
  if (n <= 0) {
    stream->eof = 1;
    return -1;
  }
  return (unsigned char)c;
}

int getchar(void) { return fgetc(stdin); }
int getc(FILE *stream) { return fgetc(stream); }

char *fgets(char *s, int size, FILE *stream) {
  int i = 0;
  while (i < size - 1) {
    int c = fgetc(stream);
    if (c < 0)
      break;
    s[i++] = c;
    if (c == '\n')
      break;
  }
  if (i == 0)
    return NULL;
  s[i] = '\0';
  return s;
}

char *gets(char *s) { return fgets(s, 1024, stdin); }

size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t total = size * nmemb;
  long written = _syscall(__NR_write, stream->fd, (long)ptr, total, 0, 0);
  return (written > 0) ? written / size : 0;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t total = size * nmemb;
  long rd = _syscall(__NR_read, stream->fd, (long)ptr, total, 0, 0);
  if (rd <= 0) {
    stream->eof = 1;
    return 0;
  }
  return rd / size;
}

int feof(FILE *stream) { return stream->eof; }
int ferror(FILE *stream) { return stream->error; }
void clearerr(FILE *stream) {
  stream->eof = 0;
  stream->error = 0;
}
int fileno(FILE *stream) { return stream->fd; }

FILE *fopen(const char *path, const char *mode) {
  int flags = 0;
  if (mode[0] == 'r')
    flags = 0;
  else if (mode[0] == 'w')
    flags = 0x241; /* O_WRONLY|O_CREAT|O_TRUNC */
  else if (mode[0] == 'a')
    flags = 0x441; /* O_WRONLY|O_CREAT|O_APPEND */

  int fd = _syscall(__NR_open, (long)path, flags, 0644, 0, 0);
  if (fd < 0)
    return NULL;

  FILE *f = malloc(sizeof(FILE));
  if (!f) {
    _syscall(__NR_close, fd, 0, 0, 0, 0);
    return NULL;
  }
  f->fd = fd;
  f->eof = 0;
  f->error = 0;
  f->mode = (mode[0] != 'r');
  return f;
}

int fclose(FILE *stream) {
  if (!stream || stream == stdin || stream == stdout || stream == stderr)
    return -1;
  _syscall(__NR_close, stream->fd, 0, 0, 0, 0);
  free(stream);
  return 0;
}

int fflush(FILE *stream) {
  (void)stream;
  return 0;
}

int fseek(FILE *stream, long offset, int whence) {
  return _syscall(__NR_lseek, stream->fd, offset, whence, 0, 0) < 0 ? -1 : 0;
}

long ftell(FILE *stream) {
  return _syscall(__NR_lseek, stream->fd, 0, 1, 0, 0);
}

void rewind(FILE *stream) {
  fseek(stream, 0, 0);
  clearerr(stream);
}

typedef int (*emit_fn)(void *ctx, char c);

static void emit_num(emit_fn emit, void *ctx, int *count, unsigned long val,
                     int base, int upper, int is_signed, int width, int prec,
                     char pad) {
  char buf[32];
  int i = 0, neg = 0;
  if (is_signed && (long)val < 0) {
    neg = 1;
    val = (unsigned long)(-(long)val);
  }
  if (val == 0 && prec != 0)
    buf[i++] = '0';
  while (val) {
    int d = (int)(val % (unsigned)base);
    buf[i++] = (char)(d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10);
    val /= (unsigned)base;
  }
  /* Zero-pad to precision. */
  while (prec > i) {
    buf[i++] = '0';
    prec--;
  }
  int len = i + neg;
  if (pad == ' ') {
    if (neg) {
      emit(ctx, '-');
      (*count)++;
    }
    while (len < width) {
      emit(ctx, pad);
      (*count)++;
      len++;
    }
  } else {
    while (len < width) {
      emit(ctx, pad);
      (*count)++;
      len++;
    }
    if (neg) {
      emit(ctx, '-');
      (*count)++;
    }
  }
  while (i > 0) {
    emit(ctx, buf[--i]);
    (*count)++;
  }
}

static int vformat(emit_fn emit, void *ctx, const char *fmt, va_list ap) {
  int count = 0;
  while (*fmt) {
    if (*fmt != '%') {
      emit(ctx, *fmt++);
      count++;
      continue;
    }
    fmt++;
    int left_align = 0;
    char pad = ' ';
    for (;;) {
      if (*fmt == '-') {
        left_align = 1;
        fmt++;
      } else if (*fmt == '0') {
        pad = '0';
        fmt++;
      } else if (*fmt == '+' || *fmt == ' ' || *fmt == '#')
        fmt++;
      else
        break;
    }
    int width = 0;
    if (*fmt == '*') {
      width = va_arg(ap, int);
      fmt++;
    } else
      while (*fmt >= '0' && *fmt <= '9') {
        width = width * 10 + (*fmt - '0');
        fmt++;
      }
    int prec = -1;
    if (*fmt == '.') {
      fmt++;
      prec = 0;
      if (*fmt == '*') {
        prec = va_arg(ap, int);
        fmt++;
      } else
        while (*fmt >= '0' && *fmt <= '9') {
          prec = prec * 10 + (*fmt - '0');
          fmt++;
        }
    }
    int longish = 0;
    while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' ||
           *fmt == 't') {
      if (*fmt == 'l')
        longish++;
      fmt++;
    }
    if (left_align)
      pad = ' ';

    switch (*fmt) {
    case 'd':
    case 'i': {
      long v = longish ? va_arg(ap, long) : (long)va_arg(ap, int);
      emit_num(emit, ctx, &count, (unsigned long)v, 10, 0, 1, width, prec, pad);
      break;
    }
    case 'u': {
      unsigned long v = longish ? va_arg(ap, unsigned long)
                                : (unsigned long)va_arg(ap, unsigned);
      emit_num(emit, ctx, &count, v, 10, 0, 0, width, prec, pad);
      break;
    }
    case 'o': {
      unsigned long v = longish ? va_arg(ap, unsigned long)
                                : (unsigned long)va_arg(ap, unsigned);
      emit_num(emit, ctx, &count, v, 8, 0, 0, width, prec, pad);
      break;
    }
    case 'x':
    case 'X': {
      unsigned long v = longish ? va_arg(ap, unsigned long)
                                : (unsigned long)va_arg(ap, unsigned);
      emit_num(emit, ctx, &count, v, 16, *fmt == 'X', 0, width, prec, pad);
      break;
    }
    case 'p': {
      emit(ctx, '0');
      count++;
      emit(ctx, 'x');
      count++;
      emit_num(emit, ctx, &count, (unsigned long)va_arg(ap, void *), 16, 0, 0,
               8, prec, '0');
      break;
    }
    case 's': {
      const char *s = va_arg(ap, const char *);
      if (!s)
        s = "(null)";
      int slen = 0;
      while (s[slen] && (prec < 0 || slen < prec))
        slen++;
      if (!left_align)
        while (slen < width) {
          emit(ctx, ' ');
          count++;
          width--;
        }
      for (int i = 0; i < slen; i++) {
        emit(ctx, s[i]);
        count++;
      }
      if (left_align)
        while (slen < width) {
          emit(ctx, ' ');
          count++;
          width--;
        }
      break;
    }
    case 'c': {
      char c = (char)va_arg(ap, int);
      if (!left_align)
        while (width > 1) {
          emit(ctx, ' ');
          count++;
          width--;
        }
      emit(ctx, c);
      count++;
      if (left_align)
        while (width > 1) {
          emit(ctx, ' ');
          count++;
          width--;
        }
      break;
    }
    case '%':
      emit(ctx, '%');
      count++;
      break;
    case '\0':
      return count; /* trailing '%' — undefined, just bail */
    default:
      emit(ctx, '%');
      emit(ctx, *fmt);
      count += 2;
      break;
    }
    fmt++;
  }
  return count;
}

static int emit_to_file(void *ctx, char c) {
  fputc(c, (FILE *)ctx);
  return 0;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
  return vformat(emit_to_file, stream, fmt, ap);
}

int fprintf(FILE *stream, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(stream, fmt, ap);
  va_end(ap);
  return r;
}
int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

struct sbuf {
  char *buf;
  size_t size;
  size_t pos;
};
static int emit_to_sbuf(void *ctx, char c) {
  struct sbuf *s = (struct sbuf *)ctx;
  if (s->pos + 1 < s->size)
    s->buf[s->pos] = c;
  s->pos++;
  return 0;
}

int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) {
  struct sbuf s = {str, size, 0};
  int n = vformat(emit_to_sbuf, &s, fmt, ap);
  if (str && size > 0)
    str[s.pos < size ? s.pos : size - 1] = '\0';
  return n;
}

int snprintf(char *str, size_t size, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(str, size, fmt, ap);
  va_end(ap);
  return n;
}

int vsprintf(char *str, const char *fmt, va_list ap) {
  return vsnprintf(str, (size_t)-1, fmt, ap);
}

int sprintf(char *str, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsprintf(str, fmt, ap);
  va_end(ap);
  return n;
}

static int sc_isdigit_base(int c, int base) {
  if (base <= 10)
    return c >= '0' && c < '0' + base;
  if (!isxdigit(c))
    return 0;
  if (isdigit(c))
    return 1;
  int v = (c | 0x20) - 'a' + 10;
  return v < base;
}

static int sc_digit_value(int c) {
  if (isdigit(c))
    return c - '0';
  return (c | 0x20) - 'a' + 10;
}

static int sc_parse_int(const char *src, int *pi, int base, int width,
                        int is_signed, unsigned long *out) {
  int i = *pi;
  int start = i;
  int neg = 0;
  int consumed = 0;

  if (is_signed && (src[i] == '+' || src[i] == '-')) {
    if (src[i] == '-')
      neg = 1;
    i++;
    consumed++;
    if (width > 0 && consumed >= width)
      goto done_prefix;
  }

  if (base == 0 || base == 16) {
    if (src[i] == '0' && (src[i + 1] == 'x' || src[i + 1] == 'X') &&
        (width <= 0 || consumed + 2 <= width) &&
        sc_isdigit_base((unsigned char)src[i + 2], 16)) {
      base = 16;
      i += 2;
      consumed += 2;
    } else if (base == 0 && src[i] == '0') {
      base = 8;
    } else if (base == 0) {
      base = 10;
    }
  }

done_prefix:;
  unsigned long val = 0;
  int any = 0;
  while (src[i] && (width <= 0 || consumed < width) &&
         sc_isdigit_base((unsigned char)src[i], base)) {
    val =
        val * (unsigned)base + (unsigned)sc_digit_value((unsigned char)src[i]);
    i++;
    consumed++;
    any = 1;
  }

  if (!any) {
    *pi = start;
    return 0;
  }

  if (is_signed && neg)
    val = (unsigned long)(-(long)val);
  *out = val;
  *pi = i;
  return 1;
}

static int vsscanf_core(const char *src, const char *fmt, va_list ap,
                        int *consumed_out) {
  int si = 0;
  int matches = 0;

  while (*fmt) {
    if (isspace((unsigned char)*fmt)) {
      while (isspace((unsigned char)src[si]))
        si++;
      fmt++;
      continue;
    }

    if (*fmt != '%') {
      if (src[si] != *fmt)
        goto out;
      si++;
      fmt++;
      continue;
    }

    fmt++;

    if (*fmt == '%') {
      if (src[si] != '%')
        goto out;
      si++;
      fmt++;
      continue;
    }

    int suppress = 0;
    if (*fmt == '*') {
      suppress = 1;
      fmt++;
    }

    int width = 0;
    while (isdigit((unsigned char)*fmt)) {
      width = width * 10 + (*fmt - '0');
      fmt++;
    }

    while (*fmt == 'l' || *fmt == 'h' || *fmt == 'z' || *fmt == 'j' ||
           *fmt == 't')
      fmt++;

    char spec = *fmt;
    if (spec == '\0')
      goto out;

    if (spec != 'c' && spec != 'n') {
      while (isspace((unsigned char)src[si]))
        si++;
    }

    switch (spec) {
    case 'd':
    case 'i':
    case 'u':
    case 'o':
    case 'x':
    case 'X': {
      int base = (spec == 'd' || spec == 'u')   ? 10
                 : (spec == 'o')                ? 8
                 : (spec == 'x' || spec == 'X') ? 16
                                                : 0;
      int is_signed = (spec == 'd' || spec == 'i');
      unsigned long val = 0;
      if (src[si] == '\0')
        goto out;
      if (!sc_parse_int(src, &si, base, width, is_signed, &val))
        goto out;
      if (!suppress) {
        int *dst = va_arg(ap, int *);
        if (is_signed)
          *dst = (int)(long)val;
        else
          *dst = (int)(unsigned)val;
        matches++;
      }
      break;
    }

    case 'c': {
      int n = width > 0 ? width : 1;
      if (src[si] == '\0')
        goto out;
      if (suppress) {
        for (int k = 0; k < n && src[si]; k++)
          si++;
      } else {
        char *dst = va_arg(ap, char *);
        for (int k = 0; k < n && src[si]; k++)
          dst[k] = src[si++];
        matches++;
      }
      break;
    }

    case 's': {
      if (src[si] == '\0')
        goto out;
      if (suppress) {
        int k = 0;
        while (src[si] && !isspace((unsigned char)src[si]) &&
               (width <= 0 || k < width)) {
          si++;
          k++;
        }
      } else {
        char *dst = va_arg(ap, char *);
        int k = 0;
        while (src[si] && !isspace((unsigned char)src[si]) &&
               (width <= 0 || k < width)) {
          dst[k++] = src[si++];
        }
        dst[k] = '\0';
        if (k == 0)
          goto out;
        matches++;
      }
      break;
    }

    case 'n': {
      if (!suppress) {
        int *dst = va_arg(ap, int *);
        *dst = si;
      }
      break;
    }

    default:
      if (consumed_out)
        *consumed_out = si;
      return matches == 0 ? -1 : matches;
    }

    fmt++;
  }

out:
  if (consumed_out)
    *consumed_out = si;
  return matches;
}

int sscanf(const char *str, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsscanf_core(str, fmt, ap, NULL);
  va_end(ap);
  return n;
}

int fscanf(FILE *stream, const char *fmt, ...) {
  char buf[256];
  long got = _syscall(__NR_read, stream->fd, (long)buf, sizeof(buf) - 1, 0, 0);
  if (got <= 0) {
    stream->eof = 1;
    return -1;
  }
  buf[got] = '\0';

  va_list ap;
  va_start(ap, fmt);
  int consumed = 0;
  int n = vsscanf_core(buf, fmt, ap, &consumed);
  va_end(ap);

  long unread = got - consumed;
  if (unread > 0)
    _syscall(__NR_lseek, stream->fd, -unread, 1, 0, 0);
  return n;
}

int scanf(const char *fmt, ...) {
  char buf[256];
  int len = 0;
  while (len < (int)sizeof(buf) - 1) {
    int c = fgetc(stdin);
    if (c < 0)
      break;
    buf[len++] = (char)c;
    if (c == '\n')
      break;
  }
  buf[len] = '\0';

  va_list ap;
  va_start(ap, fmt);
  int n = vsscanf_core(buf, fmt, ap, NULL);
  va_end(ap);
  return (len == 0) ? -1 : n;
}

void perror(const char *s) {
  extern int errno;
  extern char *strerror(int);
  if (s && *s) {
    fputs(s, stderr);
    fputs(": ", stderr);
  }
  fputs(strerror(errno), stderr);
  fputc('\n', stderr);
}

int remove(const char *path) {
  return _syscall(24, (long)path, 0, 0, 0, 0); /* __NR_unlink */
}

int rename(const char *oldpath, const char *newpath) {
  return _syscall(25, (long)oldpath, (long)newpath, 0, 0, 0); /* __NR_rename */
}
