#ifndef _LIBC_MATH_H
#define _LIBC_MATH_H

#include <stdint.h>

#define M_PI       3.14159265358979323846
#define M_PI_2     1.57079632679489661923
#define M_PI_4     0.78539816339744830962
#define M_E        2.71828182845904523536
#define M_LN2      0.69314718055994530942
#define M_LN10     2.30258509299404568402

#define HUGE_VAL   1e308
#define INFINITY   __builtin_inff()
#define NAN        __builtin_nanf("")

double fabs(double x);
float fabsf(float x);
double floor(double x);
double ceil(double x);
double fmod(double x, double y);
double sqrt(double x);
float sqrtf(float x);
double round(double x);
double trunc(double x);

double sin(double x);
double cos(double x);
double tan(double x);
float sinf(float x);
float cosf(float x);
float tanf(float x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);

double sinh(double x);
double cosh(double x);
double tanh(double x);

double pow(double base, double exp);
double log(double x);
double log10(double x);
double log2(double x);
double exp(double x);
double exp2(double x);
double frexp(double x, int *exp);
double ldexp(double x, int exp);
double modf(double x, double *iptr);

#endif 
