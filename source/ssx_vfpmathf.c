/* ssx_vfpmathf.c -- sinf, cosf, acosf and asinf on the console's FPU, with
 * newlib's own results.
 *
 * devkitARM's default newlib multilib is soft-float: its libm computes every
 * float operation in integer code (__aeabi_fadd, __mulsf3, ...). The riders'
 * animation (EA::Rigamate, EA::Ant) calls sinf/cosf/acosf on the General Job
 * Threads: on the console ~20% of those threads' busy race time went to
 * __kernel_cosf, __kernel_sinf and __ieee754_acosf in soft float (profile of
 * The Dagger, session nc3, 2026-11-18), the same story as pow (ssx_vfpmath.c).
 *
 * This time the FPU code is newlib's own: devkitARM ships the same libm built
 * for its FPU multilib (armv6k/fpu, VFPv2: no fused multiply-add, so every
 * operation rounds as the soft-float one does), and the Makefile takes that
 * build's fdlibm kernels (kf_sin, kf_cos, ef_rem_pio2, kf_rem_pio2, ef_acos,
 * ef_asin, and the sqrtf/fabsf/floorf/scalbnf they call) with their symbols
 * renamed vfp_* (nothing else links to them) and their ABI tag dropped: they
 * take and return floats in VFP registers, hence pcs("aapcs-vfp") below.
 * That multilib's own sinf/cosf are a different algorithm (ARM's optimized
 * routines), so the two wrappers here are newlib's soft-float ones, as its
 * libm_a-sf_sin.o / sf_cos.o disassemble: |x| <= pi/4 (0x3f490fd8) straight
 * to the kernel, inf/NaN x - x, else the argument reduction and the kernel
 * of its quadrant. acosf/asinf: the game's calls come clamped to [-1, 1]
 * (ssx_mathguard.c), where newlib's wrappers return the kernel's value as
 * it is.
 *
 * [debug] math_selftest compares them with newlib's soft-float functions bit
 * for bit at start-up (ssx_vfpmathf_selftest). MIT.
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <switch.h>

#include "util.h"

#define VFP __attribute__((pcs("aapcs-vfp")))
float vfp___kernel_sinf(float x, float y, int iy) VFP;
float vfp___kernel_cosf(float x, float y) VFP;
int vfp___ieee754_rem_pio2f(float x, float *y) VFP;
float vfp___ieee754_acosf(float x) VFP;
float vfp___ieee754_asinf(float x) VFP;

static inline uint32_t bits(float f) {
  uint32_t u;
  memcpy(&u, &f, 4);
  return u;
}

float b_sinf(float x) {
  const uint32_t ix = bits(x) & 0x7fffffffu;
  if (ix <= 0x3f490fd8u)
    return vfp___kernel_sinf(x, 0.0f, 0);
  if (ix > 0x7f7fffffu)
    return x - x;
  float y[2];
  const int n = vfp___ieee754_rem_pio2f(x, y);
  switch (n & 3) {
  case 0:
    return vfp___kernel_sinf(y[0], y[1], 1);
  case 1:
    return vfp___kernel_cosf(y[0], y[1]);
  case 2:
    return -vfp___kernel_sinf(y[0], y[1], 1);
  default:
    return -vfp___kernel_cosf(y[0], y[1]);
  }
}

float b_cosf(float x) {
  const uint32_t ix = bits(x) & 0x7fffffffu;
  if (ix <= 0x3f490fd8u)
    return vfp___kernel_cosf(x, 0.0f);
  if (ix > 0x7f7fffffu)
    return x - x;
  float y[2];
  const int n = vfp___ieee754_rem_pio2f(x, y);
  switch (n & 3) {
  case 0:
    return vfp___kernel_cosf(y[0], y[1]);
  case 1:
    return -vfp___kernel_sinf(y[0], y[1], 1);
  case 2:
    return -vfp___kernel_cosf(y[0], y[1]);
  default:
    return vfp___kernel_sinf(y[0], y[1], 1);
  }
}

/* For ssx_mathguard.c, whose arguments are in [-1, 1] or NaN. */
float ssx_fpu_acosf(float x) { return vfp___ieee754_acosf(x); }
float ssx_fpu_asinf(float x) { return vfp___ieee754_asinf(x); }

/* ------------------------------------------------------------ the self-test */
static uint32_t g_rng = 0x2545f491u;
static inline uint32_t rnd(void) {
  g_rng ^= g_rng << 13, g_rng ^= g_rng >> 17, g_rng ^= g_rng << 5;
  return g_rng;
}
static inline float from_bits(uint32_t u) {
  float f;
  memcpy(&f, &u, 4);
  return f;
}

typedef float (*F1)(float);
/* n arguments of one kind; same bits, or both NaN, counts as equal */
static void compare(const char *name, F1 ours, F1 newlib, int unit, unsigned n) {
  unsigned diff = 0, nan_payload = 0;
  uint32_t first_x = 0, first_a = 0, first_b = 0;
  u64 t_ours = 0, t_lib = 0;
  for (unsigned i = 0; i < n; i += 256) {
    float xs[256], a[256], b[256];
    for (int k = 0; k < 256; k++) {
      uint32_t u;
      if (unit) /* [-1, 1] uniform, and the ends */
        u = bits(((float)(rnd() >> 8) / 16777216.0f) * 2.0f - 1.0f);
      else if (k & 1) /* every float bit pattern alike: tiny, huge, inf, NaN */
        u = rnd();
      else /* the angles a game uses: |x| up to ~1000 radians */
        u = bits(((float)(rnd() >> 8) / 16777216.0f - 0.5f) * (float)(1u << (rnd() % 12)));
      if (i == 0 && k < 8) {
        static const uint32_t edge[8] = {0x00000000u, 0x80000000u, 0x3f800000u, 0xbf800000u,
                                         0x3f490fd8u, 0x3f490fd9u, 0x7f800000u, 0x7fc00000u};
        u = edge[k];
      }
      xs[k] = from_bits(u);
    }
    u64 t0 = armGetSystemTick();
    for (int k = 0; k < 256; k++)
      a[k] = ours(xs[k]);
    u64 t1 = armGetSystemTick();
    for (int k = 0; k < 256; k++)
      b[k] = newlib(xs[k]);
    u64 t2 = armGetSystemTick();
    t_ours += t1 - t0, t_lib += t2 - t1;
    for (int k = 0; k < 256; k++) {
      const uint32_t ba = bits(a[k]), bb = bits(b[k]);
      if (ba == bb)
        continue;
      if (a[k] != a[k] && b[k] != b[k]) {
        nan_payload++;
        continue;
      }
      if (!diff++)
        first_x = bits(xs[k]), first_a = ba, first_b = bb;
    }
  }
  debugPrintf("[math] self-test %s: %u arguments, %u differ from newlib%s (first: x %08x -> %08x, newlib %08x), "
              "%u NaNs with another payload; %.1f ns a call, newlib %.1f\n",
              name, n, diff, diff ? " (!)" : "", (unsigned)first_x, (unsigned)first_a, (unsigned)first_b, nan_payload,
              (double)armTicksToNs(t_ours) / n, (double)armTicksToNs(t_lib) / n);
}

static float lib_acosf(float x) { return acosf(x); }
static float lib_asinf(float x) { return asinf(x); }
static float lib_sinf(float x) { return sinf(x); }
static float lib_cosf(float x) { return cosf(x); }

void ssx_vfpmathf_selftest(void) {
  compare("sinf", b_sinf, lib_sinf, 0, 1u << 20);
  compare("cosf", b_cosf, lib_cosf, 0, 1u << 20);
  compare("acosf", ssx_fpu_acosf, lib_acosf, 1, 1u << 20);
  compare("asinf", ssx_fpu_asinf, lib_asinf, 1, 1u << 20);
}
