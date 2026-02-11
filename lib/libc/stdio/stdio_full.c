#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

extern long _syscall(long num, long a1, long a2, long a3, long a4, long a5);
extern void *malloc(size_t);
extern void free(void*);

#define __NR_read  10
#define __NR_write 11
#define __NR_open  12
#define __NR_close 13
#define __NR_lseek 14

typedef struct _FILE {
    int fd;
    int eof;
    int error;
    int mode;
} FILE;

static FILE _stdin  = { 0, 0, 0, 0 };
static FILE _stdout = { 1, 0, 0, 1 };
static FILE _stderr = { 2, 0, 0, 1 };

FILE *stdin  = &_stdin;
FILE *stdout = &_stdout;
FILE *stderr = &_stderr;

int fputc(int c, FILE *stream) {
    char ch = (char)c;
    if (_syscall(__NR_write, stream->fd, (long)&ch, 1, 0, 0) != 1) return -1;
    return c;
}

int putchar(int c) { return fputc(c, stdout); }
int putc(int c, FILE *stream) { return fputc(c, stream); }

int fputs(const char *s, FILE *stream) {
    while (*s) if (fputc(*s++, stream) < 0) return -1;
    return 0;
}

int puts(const char *s) {
    if (fputs(s, stdout) < 0) return -1;
    return fputc('\n', stdout) < 0 ? -1 : 0;
}

int fgetc(FILE *stream) {
    char c;
    int n = _syscall(__NR_read, stream->fd, (long)&c, 1, 0, 0);
    if (n <= 0) { stream->eof = 1; return -1; }
    return (unsigned char)c;
}

int getchar(void) { return fgetc(stdin); }
int getc(FILE *stream) { return fgetc(stream); }

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    while (i < size - 1) {
        int c = fgetc(stream);
        if (c < 0) break;
        s[i++] = c;
        if (c == '\n') break;
    }
    if (i == 0) return NULL;
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
    if (rd <= 0) { stream->eof = 1; return 0; }
    return rd / size;
}

int feof(FILE *stream) { return stream->eof; }
int ferror(FILE *stream) { return stream->error; }
void clearerr(FILE *stream) { stream->eof = 0; stream->error = 0; }
int fileno(FILE *stream) { return stream->fd; }

FILE *fopen(const char *path, const char *mode) {
    int flags = 0;
    if (mode[0] == 'r') flags = 0;
    else if (mode[0] == 'w') flags = 0x0501;
    else if (mode[0] == 'a') flags = 0x0901;
    
    int fd = _syscall(__NR_open, (long)path, flags, 0644, 0, 0);
    if (fd < 0) return NULL;
    
    FILE *f = malloc(sizeof(FILE));
    if (!f) { _syscall(__NR_close, fd, 0, 0, 0, 0); return NULL; }
    f->fd = fd; f->eof = 0; f->error = 0; f->mode = (mode[0] != 'r');
    return f;
}

int fclose(FILE *stream) {
    if (!stream || stream == stdin || stream == stdout || stream == stderr) return -1;
    _syscall(__NR_close, stream->fd, 0, 0, 0, 0);
    free(stream);
    return 0;
}

int fflush(FILE *stream) { (void)stream; return 0; }

int fseek(FILE *stream, long offset, int whence) {
    return _syscall(__NR_lseek, stream->fd, offset, whence, 0, 0) < 0 ? -1 : 0;
}

long ftell(FILE *stream) {
    return _syscall(__NR_lseek, stream->fd, 0, 1, 0, 0);
}

void rewind(FILE *stream) { fseek(stream, 0, 0); clearerr(stream); }

static void print_num(FILE *f, unsigned long val, int base, int is_signed, int width, char pad) {
    char buf[32]; int i = 0, neg = 0;
    if (is_signed && (long)val < 0) { neg = 1; val = -(long)val; }
    if (val == 0) buf[i++] = '0';
    else while (val) { int d = val % base; buf[i++] = (d < 10) ? '0' + d : 'a' + d - 10; val /= base; }
    int len = i + neg;
    while (len < width) { fputc(pad, f); len++; }
    if (neg) fputc('-', f);
    while (i > 0) fputc(buf[--i], f);
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    int count = 0;
    while (*fmt) {
        if (*fmt != '%') { fputc(*fmt++, stream); count++; continue; }
        fmt++;
        char pad = ' '; int width = 0;
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        if (*fmt == 'l') fmt++;
        switch (*fmt) {
            case 'd': case 'i': print_num(stream, va_arg(ap, int), 10, 1, width, pad); break;
            case 'u': print_num(stream, va_arg(ap, unsigned), 10, 0, width, pad); break;
            case 'x': print_num(stream, va_arg(ap, unsigned), 16, 0, width, pad); break;
            case 'p': fputs("0x", stream); print_num(stream, (unsigned long)va_arg(ap, void*), 16, 0, 8, '0'); break;
            case 's': { const char *s = va_arg(ap, const char*); fputs(s ? s : "(null)", stream); break; }
            case 'c': fputc(va_arg(ap, int), stream); break;
            case '%': fputc('%', stream); break;
            default: fputc('%', stream); fputc(*fmt, stream);
        }
        fmt++;
    }
    return count;
}

int fprintf(FILE *stream, const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(stream, fmt, ap); va_end(ap); return r; }
int printf(const char *fmt, ...) { va_list ap; va_start(ap, fmt); int r = vfprintf(stdout, fmt, ap); va_end(ap); return r; }
int vprintf(const char *fmt, va_list ap) { return vfprintf(stdout, fmt, ap); }

int sprintf(char *str, const char *fmt, ...) { (void)str; (void)fmt; return 0; }
int snprintf(char *str, size_t size, const char *fmt, ...) { (void)str; (void)size; (void)fmt; return 0; }
int vsprintf(char *str, const char *fmt, va_list ap) { (void)str; (void)fmt; (void)ap; return 0; }
int vsnprintf(char *str, size_t size, const char *fmt, va_list ap) { (void)str; (void)size; (void)fmt; (void)ap; return 0; }
int scanf(const char *fmt, ...) { (void)fmt; return 0; }
int fscanf(FILE *stream, const char *fmt, ...) { (void)stream; (void)fmt; return 0; }
int sscanf(const char *str, const char *fmt, ...) { (void)str; (void)fmt; return 0; }

void perror(const char *s) {
    extern int errno;
    extern char *strerror(int);
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs(strerror(errno), stderr);
    fputc('\n', stderr);
}

int errno = 0;

int remove(const char *path) {
    return _syscall(24, (long)path, 0, 0, 0, 0);  /* __NR_unlink */
}

int rename(const char *oldpath, const char *newpath) {
    return _syscall(25, (long)oldpath, (long)newpath, 0, 0, 0);  /* __NR_rename */
}
