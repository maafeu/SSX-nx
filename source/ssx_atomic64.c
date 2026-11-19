/* ssx_atomic64.c -- EA::Thread's 64-bit "atomics" as real ones.
 *
 * libgame.so was built for armeabi (ARMv5TE), which has no 64-bit atomic
 * instructions, so EA::Thread does its 64-bit atomic read, swap and
 * compare-exchange (android_fake_atomic_read_64 / _swap_64 / _cmpxchg_64)
 * under one of 32 pthread mutexes picked by address bits 3-7, around a plain
 * ldrd / strd. On the console the General Job Threads spent 23% of a race
 * waiting for mutexes, FGGameSim 14%, FGGameRender 4%, much of it under these
 * (profile of Curnoe's Moustache, console run c3, 2026-11-16).
 *
 * The Switch's CPU has LDREXD / STREXD: the three functions are replaced, all
 * together (so they stay atomic with respect to each other), by GCC's
 * sequentially consistent 64-bit atomics, inline exclusive loops. A pointer
 * that is not 8-byte aligned (LDREXD would fault) takes a lock of ours
 * instead. Behaviour is unchanged otherwise: cmpxchg returns 0 when it stored,
 * 1 when the value was not the expected one, as EA's does. MIT.
 */
#include <stdint.h>
#include <switch.h>

#include "ssx_hook.h"
#include "util.h"

#define OFF_Swap64    0x4a92d8 /* EA::Thread::android_fake_atomic_swap_64(long long, long long volatile*) */
#define OFF_Cmpxchg64 0x4a9324 /* EA::Thread::android_fake_atomic_cmpxchg_64(long long, long long, long long volatile*) */
#define OFF_Read64    0x4a9384 /* EA::Thread::android_fake_atomic_read_64(long long volatile*) */

static Mutex g_unaligned; /* only for a pointer LDREXD cannot take */
static volatile uint32_t g_calls, g_unaligned_calls;

static int64_t swap64(int64_t value, volatile int64_t *p) {
  g_calls++;
  if ((uintptr_t)p & 7) {
    g_unaligned_calls++;
    mutexLock(&g_unaligned);
    int64_t old = *p;
    *p = value;
    mutexUnlock(&g_unaligned);
    return old;
  }
  return __atomic_exchange_n(p, value, __ATOMIC_SEQ_CST);
}

static int cmpxchg64(int64_t expected, int64_t value, volatile int64_t *p) {
  g_calls++;
  if ((uintptr_t)p & 7) {
    g_unaligned_calls++;
    mutexLock(&g_unaligned);
    int stored = *p == expected;
    if (stored)
      *p = value;
    mutexUnlock(&g_unaligned);
    return !stored;
  }
  return !__atomic_compare_exchange_n(p, &expected, value, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

static int64_t read64(volatile int64_t *p) {
  g_calls++;
  if ((uintptr_t)p & 7) {
    g_unaligned_calls++;
    mutexLock(&g_unaligned);
    int64_t v = *p;
    mutexUnlock(&g_unaligned);
    return v;
  }
  return __atomic_load_n(p, __ATOMIC_SEQ_CST);
}

void ssx_atomic64_patch(void) {
  /* all three or none: a mix would leave two mechanisms guarding one value */
  if (!ssx_hook_word_is(OFF_Swap64, 0xe92d43f8) || !ssx_hook_word_is(OFF_Cmpxchg64, 0xe92d43f8) ||
      !ssx_hook_word_is(OFF_Read64, 0xe59f3034)) {
    debugPrintf("[atomic64] EA::Thread's fake 64-bit atomics are not 0.0.8430's: left alone\n");
    return;
  }
  int ok = ssx_hook_jump(OFF_Swap64, 0xe92d43f8, (void *)swap64);
  ok &= ssx_hook_jump(OFF_Cmpxchg64, 0xe92d43f8, (void *)cmpxchg64);
  ok &= ssx_hook_jump(OFF_Read64, 0xe59f3034, (void *)read64);
  debugPrintf("[atomic64] EA::Thread's 64-bit atomics: %s\n",
              ok ? "LDREXD/STREXD instead of a mutex each" : "NOT all replaced (check the hook log)");
}

void ssx_atomic64_report(void) {
  static uint32_t last;
  uint32_t n = g_calls;
  if (n != last)
    debugPrintf("[atomic64] %u calls in 10 s (%u unaligned so far)\n", (unsigned)(n - last),
                (unsigned)g_unaligned_calls);
  last = n;
}
