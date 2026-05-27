/*
 * atoi.c — libc `atoi`: parse a decimal integer with optional sign,
 * skipping leading whitespace.
 *
 * Invariants:
 *  - POSIX semantics: silent wrap on overflow, stop at first non-digit.
 *  - Private isspace_local/isdigit_local helpers keep this TU dependency-free.
 *  - Pure function; no global state.
 *
 * Not allowed:
 *  - Pulling <ctype.h> or any other libc TU (must stay leaf-level).
 *  - Reporting errors via errno (POSIX `atoi` does not set errno).
 */

static int isspace_local(int c) { return c == ' ' || c == '\t' || c == '\n'; }
static int isdigit_local(int c) { return c >= '0' && c <= '9'; }

int atoi(const char *str) {
  int result = 0;
  int sign = 1;
  while (isspace_local(*str))
    str++;
  if (*str == '-') {
    sign = -1;
    str++;
  } else if (*str == '+')
    str++;
  while (isdigit_local(*str)) {
    result = result * 10 + (*str - '0');
    str++;
  }
  return result * sign;
}
