/* ssx_mathguard.c -- the game's inverse trigonometry kept inside its domain,
 * and the math library's NaN results counted.
 *
 * The riders' bone matrices came out NaN under Ryujinx: a few bones per mesh
 * piece (one, in some), the board's always fine (docs/SSX-notes.md, "Open").
 * A NaN in a few joints is what an IK or roll solver gives when acos/asin
 * gets a cosine a rounding error past 1 (acosf(1.0000001) is NaN), and the
 * emulator computes NEON estimates (VRSQRTE/VRECPE) and some float
 * operations with x86 instructions that round differently from the ARM
 * core: a quaternion normalised there can land a hair over 1.
 *
 * acos/asin (float and double) get their argument clamped to [-1, 1], which
 * is what the caller means (the angle of two unit vectors); NaN goes through
 * as NaN. Harmless where nothing is out of range (the console, the Vita).
 * powf/logf/fmodf/expf are only watched: NaN out of non-NaN arguments, and
 * expf overflowing, are counted. Every 10 s debug.log gets a [math] line
 * when anything was counted. MIT.
 */
#include <math.h>
#include <stdint.h>

#include "util.h"

static volatile uint32_t g_acos_calls, g_acos_clamped, g_asin_clamped, g_nan_in;
static volatile uint32_t g_pow_nan, g_log_nan, g_fmod_nan, g_exp_inf;
static volatile float g_max_excess;

static inline float clamp1f(float x, volatile uint32_t *count) {
  if (x > 1.0f) {
    (*count)++;
    if (x - 1.0f > g_max_excess)
      g_max_excess = x - 1.0f;
    return 1.0f;
  }
  if (x < -1.0f) {
    (*count)++;
    if (-1.0f - x > g_max_excess)
      g_max_excess = -1.0f - x;
    return -1.0f;
  }
  if (x != x)
    g_nan_in++;
  return x;
}

static inline double clamp1(double x, volatile uint32_t *count) {
  if (x > 1.0 || x < -1.0) {
    (*count)++;
    const float e = (float)(x > 0 ? x - 1.0 : -1.0 - x);
    if (e > g_max_excess)
      g_max_excess = e;
    return x > 0 ? 1.0 : -1.0;
  }
  if (x != x)
    g_nan_in++;
  return x;
}

/* newlib's acosf/asinf kernels on the FPU (ssx_vfpmathf.c): in [-1, 1] and
 * for NaN newlib's wrappers return the kernel's value unchanged */
float ssx_fpu_acosf(float x);
float ssx_fpu_asinf(float x);
float b_acosf(float x) {
  g_acos_calls++;
  return ssx_fpu_acosf(clamp1f(x, &g_acos_clamped));
}
float b_asinf(float x) { return ssx_fpu_asinf(clamp1f(x, &g_asin_clamped)); }
double b_acos(double x) {
  g_acos_calls++;
  return acos(clamp1(x, &g_acos_clamped));
}
double b_asin(double x) { return asin(clamp1(x, &g_asin_clamped)); }

float b_powf(float x, float y) {
  const float r = powf(x, y);
  if (r != r && x == x && y == y)
    g_pow_nan++;
  return r;
}
float b_logf(float x) {
  const float r = logf(x);
  if (r != r && x == x)
    g_log_nan++;
  return r;
}
float b_fmodf(float x, float y) {
  const float r = fmodf(x, y);
  if (r != r && x == x && y == y)
    g_fmod_nan++;
  return r;
}
float b_expf(float x) {
  const float r = expf(x);
  if (isinf(r) && !isinf(x))
    g_exp_inf++;
  return r;
}

/* sqrt/sqrtf, watched: the runtime's shims are single instructions
 * (bionic_math.c); these, laid over them for the game's imports
 * (port_imports, imports.h), also count negative, zero and NaN arguments --
 * a zero-length vector normalised (x / sqrt(0)) or a rounding error under
 * zero are the usual first NaN. */
static volatile uint32_t g_sqrt_calls, g_sqrt_neg, g_sqrt_zero, g_sqrt_nan;
static float watch_sqrtf(float x) {
  g_sqrt_calls++;
  if (x < 0.0f)
    g_sqrt_neg++;
  else if (x == 0.0f)
    g_sqrt_zero++;
  else if (x != x)
    g_sqrt_nan++;
  float r;
  __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
  return r;
}
static double watch_sqrt(double x) {
  g_sqrt_calls++;
  if (x < 0.0)
    g_sqrt_neg++;
  else if (x == 0.0)
    g_sqrt_zero++;
  else if (x != x)
    g_sqrt_nan++;
  double r;
  __asm__("vsqrt.f64 %P0, %P1" : "=w"(r) : "w"(x));
  return r;
}

#include "imports.h"
const DynLibFunction port_imports[] = {
    {"sqrt", (uintptr_t)watch_sqrt},
    {"sqrtf", (uintptr_t)watch_sqrtf},
};
const int port_imports_count = sizeof port_imports / sizeof port_imports[0];

void ssx_mathguard_report(void) {
  static uint32_t last;
  const uint32_t sum = g_acos_clamped + g_asin_clamped + g_nan_in + g_pow_nan + g_log_nan + g_fmod_nan + g_exp_inf +
                       g_sqrt_neg + g_sqrt_nan;
  if (sum == last)
    return;
  last = sum;
  debugPrintf("[math] acos %u calls, %u clamped into [-1,1] (worst %.3g over), asin %u clamped; NaN arguments %u; "
              "NaN results: powf %u, logf %u, fmodf %u; expf overflows %u; sqrt %u calls: %u negative, %u zero, "
              "%u NaN\n",
              (unsigned)g_acos_calls, (unsigned)g_acos_clamped, (double)g_max_excess, (unsigned)g_asin_clamped,
              (unsigned)g_nan_in, (unsigned)g_pow_nan, (unsigned)g_log_nan, (unsigned)g_fmod_nan,
              (unsigned)g_exp_inf, (unsigned)g_sqrt_calls, (unsigned)g_sqrt_neg, (unsigned)g_sqrt_zero,
              (unsigned)g_sqrt_nan);
}
