/*
 * ctype.c — libc <ctype.h> classifiers and case conversion (ASCII).
 *
 * Invariants:
 *  - Input is treated as an unsigned ASCII code point (0..127); behavior on
 *    values outside that range mirrors POSIX undefined behavior.
 *  - Pure functions: no global state, no I/O, no allocation.
 *  - isgraph/iscntrl/isxdigit/isblank are folded in from ctype_ext.c.
 *
 * Not allowed:
 *  - Locale-aware behavior (C locale only).
 *  - Calling other libc functions that allocate or syscall.
 */

int isalpha(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int isspace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
         c == '\v';
}
int ispunct(int c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}
int isprint(int c) { return c >= ' ' && c <= '~'; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

int isgraph(int c) { return c > ' ' && c <= '~'; }
int iscntrl(int c) { return (c >= 0 && c < ' ') || c == 127; }
int isxdigit(int c) {
  return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
         (c >= 'A' && c <= 'F');
}
int isblank(int c) { return c == ' ' || c == '\t'; }
