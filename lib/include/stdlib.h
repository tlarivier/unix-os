/*
 * stdlib.h - Standard library for userspace
 */

#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H

#include <stddef.h>
#include <stdint.h>

#define NULL ((void*)0)
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#define RAND_MAX 0x7fff

/* Memory allocation */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void free(void *ptr);

/* Process control */
void exit(int status);
void abort(void);
void _exit(int status);

/* String conversion */
int atoi(const char *s);
long atol(const char *s);
double atof(const char *s);
long strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
char *itoa(int value, char *str, int base);

/* Random numbers */
int rand(void);
void srand(unsigned int seed);

/* Sorting and searching */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void*, const void*));
void *bsearch(const void *key, const void *base, size_t nmemb, size_t size,
              int (*compar)(const void*, const void*));

/* Math */
int abs(int j);
long labs(long j);

/* Environment */
char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int system(const char *command);

#endif /* _LIBC_STDLIB_H */
