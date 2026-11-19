/* ssx_fpmode.c -- the floating-point mode of the game's threads (an
 * experiment, off unless [debug] fp_runfast is on).
 *
 * The riders' bone matrices came out NaN under Ryujinx (docs/SSX-notes.md,
 * "Open"). On an ARMv8 core in AArch32 state, Advanced SIMD (NEON) always
 * flushes denormals to zero and answers the default NaN, whatever FPSCR says;
 * scalar VFP follows FPSCR (FZ and DN clear by default, as on Android). An
 * emulator that does not apply that to NEON computes edge cases differently.
 * With fp_runfast, every game thread starts with FPSCR.FZ and FPSCR.DN set,
 * so the emulator's scalar and vector arithmetic both behave the NEON way:
 * if the NaN poses go away, the emulator's NEON denormal handling is the
 * cause (the console's NEON already does this). The bionic pthread layer
 * calls port_thread_tag_enter on every thread it starts. MIT.
 */
#include <stdint.h>

#include "rt_cfg.h"
#include "util.h"

static inline void runfast(void) {
  uint32_t v;
  __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
  v |= (1u << 24) | (1u << 25); /* FZ, DN */
  __asm__ volatile("vmsr fpscr, %0" : : "r"(v));
}

static int g_on = -1;
static int on(void) {
  if (g_on < 0)
    g_on = rt_config_value("debug.fp_runfast", 0);
  return g_on;
}

/* bionic_pthread.c: on each new game thread */
void port_thread_tag_enter(uintptr_t tag) {
  if (on())
    runfast();
}

void ssx_fpmode_main(void) {
  if (on()) {
    runfast();
    debugPrintf("[fp] experiment: FPSCR flush-to-zero + default NaN on every game thread\n");
  }
}
