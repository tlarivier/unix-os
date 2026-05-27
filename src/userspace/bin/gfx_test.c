/*
 * gfx_test.c — linear smoke-test of the graphics stack: gfx_mode(1) -> mmap
 * framebuffer -> grayscale palette -> horizontal gradient -> nanosleep ->
 * gfx_mode(0); logs each step to serial for headless runs.
 *
 * Invariants:
 *  - Maps exactly 64000 bytes at 0xA0000 via mmap MAP_FIXED.
 *  - Always returns to text mode (gfx_mode(0)) before exit.
 *
 * Not allowed:
 *  - Animating or looping (static gradient only — see plasma.c for animation).
 */

#include <stdio.h>
#include <sys/types.h>
#include <syscall.h>
#include <unistd.h>

#define __NR_gfx_mode 245
#define __NR_gfx_palette 246

#define VGA_FB 0xA0000U
#define VGA_FB_SIZE 64000U /* 320 * 200 */
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define PROT_READ 0x1
#define PROT_WRITE 0x2

static void log_step(const char *msg) {
  write(1, msg, 0); /* harmless probe to keep ordering */
  puts(msg);
}

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  log_step("[gfx] step 1: switching to VGA Mode 13h");
  long rc = __syscall1(__NR_gfx_mode, 1);
  if (rc < 0) {
    printf("[gfx] gfx_mode(1) failed: %d\n", (int)rc);
    return 1;
  }

  log_step("[gfx] step 2: mmap 0xA0000 as user-writable");
  long fb = __syscall5(__NR_mmap, VGA_FB, VGA_FB_SIZE, PROT_READ | PROT_WRITE,
                       MAP_FIXED | MAP_PRIVATE | MAP_ANON, (long)-1);
  if (fb < 0 || fb == 0) {
    printf("[gfx] mmap framebuffer failed: %d\n", (int)fb);
    __syscall1(__NR_gfx_mode, 0);
    return 2;
  }
  if ((unsigned long)fb != VGA_FB) {
    printf("[gfx] mmap returned wrong address %x (expected %x)\n", (unsigned)fb,
           (unsigned)VGA_FB);
    __syscall1(__NR_gfx_mode, 0);
    return 3;
  }

  log_step("[gfx] step 3: pushing grayscale palette");
  static unsigned char pal[256 * 3];
  for (int i = 0; i < 256; i++) {
    pal[i * 3 + 0] = (unsigned char)i; /* R */
    pal[i * 3 + 1] = (unsigned char)i; /* G */
    pal[i * 3 + 2] = (unsigned char)i; /* B */
  }
  rc = __syscall3(__NR_gfx_palette, (long)pal, 0, 256);
  if (rc < 0) {
    printf("[gfx] gfx_palette failed: %d\n", (int)rc);
  }

  log_step("[gfx] step 4: painting gradient");
  volatile unsigned char *p = (volatile unsigned char *)(unsigned long)fb;
  for (int y = 0; y < 200; y++) {
    for (int x = 0; x < 320; x++) {
      p[y * 320 + x] = (unsigned char)(x & 0xFF);
    }
  }

  log_step("[gfx] step 5: holding gradient for ~30 seconds (nanosleep)");
  struct {
    long tv_sec;
    long tv_nsec;
  } req = {30, 0};
  __syscall2(61 /* __NR_nanosleep */, (long)&req, 0);

  log_step("[gfx] step 6: back to text mode");
  __syscall1(__NR_gfx_mode, 0);
  log_step("[gfx] done");
  return 0;
}
