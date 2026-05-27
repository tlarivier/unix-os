/*
 * plasma.c — 320x200 VGA Mode 13h demo: precomputes a Q0 sine table, mmaps the
 * framebuffer, animates 200 frames of 4-sine plasma with palette rotation to
 * stress sys_gfx_mode + MAP_FIXED mmap + sys_gfx_palette.
 *
 * Invariants:
 *  - All math is integer (no libm); sin values lie in [-127, +127].
 *  - Framebuffer is mmap'd at 0xA0000 with PROT_READ|WRITE, MAP_FIXED.
 *  - sys_gfx_mode(0) restores text mode before exit on every path.
 *
 * Not allowed:
 *  - Floating-point ops (no FPU enable in early userspace).
 */

#include <stdio.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#define __NR_gfx_mode 245
#define __NR_gfx_palette 246
#define __NR_nanosleep 61

#define VGA_FB 0xA0000U
#define VGA_W 320
#define VGA_H 200
#define VGA_FB_SIZE (VGA_W * VGA_H)
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define PROT_READ 0x1
#define PROT_WRITE 0x2

static signed char sin_table[256];

static void build_sin_table(void) {
  for (int i = 0; i < 256; i++) {
    int deg = (i * 360) / 256;
    int sign = 1;
    if (deg >= 180) {
      deg -= 180;
      sign = -1;
    }
    long num = 4L * deg * (180 - deg);
    long denom = 40500L - deg * (180 - deg);
    long val = (num * 127L) / denom;
    sin_table[i] = (signed char)(sign * val);
  }
}

static inline int sin_q(int x) { return sin_table[(unsigned)x & 0xFF]; }

static void make_palette(unsigned char pal[256 * 3], int phase) {
  for (int i = 0; i < 256; i++) {
    int h = (i + phase) & 0xFF;
    int r = (sin_q(h) + 127);
    int g = (sin_q(h + 85) + 127);
    int b = (sin_q(h + 170) + 127);
    pal[i * 3 + 0] = (unsigned char)(r >> 1);
    pal[i * 3 + 1] = (unsigned char)(g >> 1);
    pal[i * 3 + 2] = (unsigned char)(b >> 1);
  }
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  puts("[plasma] init");
  build_sin_table();

  if (__syscall1(__NR_gfx_mode, 1) < 0) {
    puts("gfx_mode failed");
    return 1;
  }
  long fb = __syscall5(__NR_mmap, VGA_FB, VGA_FB_SIZE, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_PRIVATE | MAP_ANON, (long)-1);
  if (fb < 0 || (unsigned long)fb != VGA_FB) {
    printf("mmap failed: %d\n", (int)fb);
    __syscall1(__NR_gfx_mode, 0);
    return 2;
  }
  volatile unsigned char *p = (volatile unsigned char *)(unsigned long)fb;

  static unsigned char palette[256 * 3];

  const int FRAMES = 200;
  struct {
    long s;
    long ns;
  } req = {0, 200000000L}; /* 5 fps */

  for (int f = 0; f < FRAMES; f++) {
    make_palette(palette, f * 3);
    __syscall3(__NR_gfx_palette, (long)palette, 0, 256);

    int t = f * 2;
    for (int y = 0; y < VGA_H; y++) {
      for (int x = 0; x < VGA_W; x++) {
        int v = sin_q(x + t) + sin_q(y - t) + sin_q((x + y) / 2 + t) +
                sin_q((x - y) / 2 + t / 2);
        p[y * VGA_W + x] = (unsigned char)((v + 512) >> 2);
      }
    }
    __syscall2(__NR_nanosleep, (long)&req, 0);
  }

  puts("[plasma] holding final frame for 20s");
  struct {
    long s;
    long ns;
  } hold = {20, 0};
  __syscall2(__NR_nanosleep, (long)&hold, 0);

  __syscall1(__NR_gfx_mode, 0);
  puts("[plasma] done");
  return 0;
}
