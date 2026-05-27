/*
 * itoa.c — non-POSIX `itoa`: convert `value` to a string in the requested
 * base (decimal/hex/oct) into a caller-provided buffer.
 *
 * Invariants:
 *  - Caller owns the buffer and must size it for the worst case (>=12 bytes
 *    for int32 plus sign and NUL).
 *  - Decimal output emits a leading '-' for negative values; other bases
 *    treat the value as unsigned.
 *  - Returns the count of digits written (excluding sign and NUL).
 *
 * Not allowed:
 *  - Allocating; no malloc, no static buffer.
 *  - Touching errno.
 */

int itoa(int value, char *str, int base) {
  char *p = str;
  char *p1, *p2;
  int digits = 0;
  unsigned int v;

  if (value < 0 && base == 10) {
    *p++ = '-';
    v = -value;
  } else {
    v = (unsigned int)value;
  }

  do {
    int remainder = v % base;
    *p++ = (remainder < 10) ? '0' + remainder : 'a' + remainder - 10;
    digits++;
  } while (v /= base);

  *p = '\0';

  p1 = str;
  if (*p1 == '-')
    p1++;
  p2 = p - 1;
  while (p1 < p2) {
    char tmp = *p1;
    *p1++ = *p2;
    *p2-- = tmp;
  }

  return digits;
}
