/*
 * math.c — libc <math.h> implementations (fabs, floor/ceil/fmod, sqrt,
 * sin/cos/tan/atan/atan2 via Taylor series, pow/log/exp, frexp/ldexp/modf).
 *
 * Invariants:
 *  - Pure functions; no global state, no allocation, no syscalls.
 *  - Trig is Taylor-series based: accuracy degrades for large arguments
 *    (Doom's use is bounded so this is acceptable).
 *  - No NaN/Inf/denormal handling — callers must avoid pathological inputs.
 *
 * Not allowed:
 *  - Pulling in <stdio.h> or any allocator (math must stay leaf-level).
 *  - Claiming IEEE-754 semantics for edge cases.
 */

#include <stdint.h>

double fabs(double x) { return x < 0 ? -x : x; }
float fabsf(float x) { return x < 0 ? -x : x; }

double floor(double x) {
  int64_t i = (int64_t)x;
  return (double)(x < i ? i - 1 : i);
}

double ceil(double x) {
  int64_t i = (int64_t)x;
  return (double)(x > i ? i + 1 : i);
}

double fmod(double x, double y) {
  if (y == 0)
    return 0;
  return x - (int64_t)(x / y) * y;
}

double sqrt(double x) {
  if (x <= 0)
    return 0;
  double guess = x / 2;
  for (int i = 0; i < 20; i++) {
    guess = (guess + x / guess) / 2;
  }
  return guess;
}

float sqrtf(float x) { return (float)sqrt((double)x); }

static double _sin_taylor(double x) {
  const double PI = 3.14159265358979323846;
  while (x > PI)
    x -= 2 * PI;
  while (x < -PI)
    x += 2 * PI;

  double term = x, sum = x, x2 = x * x;
  for (int i = 1; i < 10; i++) {
    term *= -x2 / ((2 * i) * (2 * i + 1));
    sum += term;
  }
  return sum;
}

double sin(double x) { return _sin_taylor(x); }
double cos(double x) { return _sin_taylor(x + 3.14159265358979323846 / 2); }
float sinf(float x) { return (float)sin((double)x); }
float cosf(float x) { return (float)cos((double)x); }

double tan(double x) {
  double c = cos(x);
  return (c == 0) ? 0 : sin(x) / c;
}

double atan(double x) {
  const double PI = 3.14159265358979323846;
  if (x > 1)
    return PI / 2 - atan(1 / x);
  if (x < -1)
    return -PI / 2 - atan(1 / x);

  double sum = 0, term = x, x2 = x * x;
  for (int i = 0; i < 15; i++) {
    sum += term / (2 * i + 1);
    term *= -x2;
  }
  return sum;
}

double atan2(double y, double x) {
  const double PI = 3.14159265358979323846;
  if (x > 0)
    return atan(y / x);
  if (x < 0 && y >= 0)
    return atan(y / x) + PI;
  if (x < 0 && y < 0)
    return atan(y / x) - PI;
  if (x == 0 && y > 0)
    return PI / 2;
  if (x == 0 && y < 0)
    return -PI / 2;
  return 0;
}

double pow(double base, double exp) {
  if (exp == 0)
    return 1;
  if (base == 0)
    return 0;
  if (exp == (int)exp && exp > 0 && exp < 100) {
    double result = 1;
    int n = (int)exp;
    while (n > 0) {
      if (n & 1)
        result *= base;
      base *= base;
      n >>= 1;
    }
    return result;
  }
  return base;
}

double log(double x) {
  if (x <= 0)
    return 0;
  double y = x - 1, sum = 0, term = y;
  for (int i = 1; i < 20; i++) {
    sum += term / i;
    term *= -y;
  }
  return sum;
}

double exp(double x) {
  double sum = 1, term = 1;
  for (int i = 1; i < 20; i++) {
    term *= x / i;
    sum += term;
  }
  return sum;
}

double log10(double x) { return log(x) / 2.302585092994046; }
double frexp(double x, int *exp) {
  *exp = 0;
  return x;
}
double ldexp(double x, int exp) {
  while (exp-- > 0)
    x *= 2;
  return x;
}
double modf(double x, double *iptr) {
  *iptr = floor(x);
  return x - *iptr;
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
