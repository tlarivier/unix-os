#ifndef USERSPACE_LIBC_H
#define USERSPACE_LIBC_H

#include <lib/types.h>
#include <stddef.h>
#include <stdint.h>

size_t strlen(const char *str);
char *strcpy(char *dest, const char *src);
char *strncpy(char *dest, const char *src, size_t n);
int strcmp(const char *str1, const char *str2);
int strncmp(const char *str1, const char *str2, size_t n);
char *strcat(char *dest, const char *src);
char *strncat(char *dest, const char *src, size_t n);
char *strchr(const char *str, int c);
char *strrchr(const char *str, int c);
char *strstr(const char *haystack, const char *needle);
char *strdup(const char *src);

void *memset(void *ptr, int value, size_t num);
void *memcpy(void *dest, const void *src, size_t num);
void *memmove(void *dest, const void *src, size_t num);
int memcmp(const void *ptr1, const void *ptr2, size_t num);

void *malloc(size_t size);
void *calloc(size_t num, size_t size);
void *realloc(void *ptr, size_t new_size);
void free(void *ptr);

int isalnum(int c);
int isalpha(int c);
int isdigit(int c);
int islower(int c);
int isupper(int c);
int isspace(int c);
int ispunct(int c);
int isprint(int c);

int tolower(int c);
int toupper(int c);

int atoi(const char *str);
long atol(const char *str);
double atof(const char *str);

int itoa(int value, char *str, int base);
int sprintf(char *buffer, const char *format, ...);
int snprintf(char *buffer, size_t size, const char *format, ...);

int printf(const char *format, ...);
int fprintf(int fd, const char *format, ...);
int puts(const char *str);
int putchar(int c);
int getchar(void);
char *gets(char *str);
char *fgets(char *str, int n, int fd);

int open(const char *path, int flags);
int close(int fd);
ssize_t read(int fd, void *buffer, size_t count);
ssize_t write(int fd, const void *buffer, size_t count);
int lseek(int fd, int offset, int whence);

ssize_t syscall_write(int fd, const void *buffer, size_t count);
int syscall_opendir(const char *path);
ssize_t syscall_readdir(int fd, void *buffer, size_t count);
int syscall_close(int fd);

int fork(void);
int exec(const char *path, char *const argv[]);
void exit(int status);
int wait(int *status);
int waitpid(int pid, int *status, int options);
int getpid(void);
int getppid(void);

typedef void (*signal_handler_t)(int);
signal_handler_t signal(int signum, signal_handler_t handler);
int kill(int pid, int signal);

void sleep(unsigned int seconds);
void usleep(unsigned int microseconds);
unsigned int time(unsigned int *t);

extern int errno;
char *strerror(int errnum);
void perror(const char *prefix);

#endif
