/* ssx_sched.c -- [performance] threads_first: SSX's frame threads one priority
 * step above the rest of the game.
 *
 * Every guest thread runs at priority 59, where Horizon time-slices threads of
 * equal priority across cores 0-2 every 10 ms (runtime/source/dcr_sched.c: the
 * rule that keeps spin-waiting engines alive). In a race SSX keeps those three
 * cores 80-90% busy (console run c3, 2026-11-16: FGGameRender up to 90% of a
 * core, FGGameSim ~45%, three General Job Threads ~33% each), so a frame thread
 * that becomes runnable can wait out a job thread's 10 ms slice and miss the
 * vsync. At 58 the frame thread takes a core the moment it is runnable; it
 * blocks every frame (on the vsync, on the simulation), so the job threads
 * still get the rest. Values: off; render (FGGameRender); render_sim (and
 * FGGameSim). Checked every 10 s (ssx_boot.c's report), so a thread is moved
 * within 10 s of being named. MIT.
 */
#include <string.h>
#include <switch.h>

#include "bionic_pthread.h"
#include "dcr_config.h"
#include "util.h"

#define GUEST_PRIO 59
#define FIRST_PRIO 58

typedef struct {
  Handle h[4];
  char name[4][16];
  int n;
} Found;

static void find(BThread *t, void *arg) {
  Found *f = arg;
  if (f->n >= 4 || !t->handle || t->finished || !t->name[0])
    return;
  const int mode = dcr_config()->threads_first;
  if (strcmp(t->name, "FGGameRender") && !(mode >= 2 && !strcmp(t->name, "FGGameSim")))
    return;
  f->h[f->n] = t->handle;
  memcpy(f->name[f->n], t->name, 16);
  f->name[f->n][15] = 0;
  f->n++;
}

void ssx_sched_report(void) {
  if (!dcr_config()->threads_first || dcr_is_emulator())
    return;
  Found f;
  f.n = 0;
  b_thread_foreach(find, &f);
  for (int i = 0; i < f.n; i++) {
    s32 prio = 0;
    if (R_FAILED(svcGetThreadPriority(&prio, f.h[i])) || prio != GUEST_PRIO)
      continue;
    Result rc = svcSetThreadPriority(f.h[i], FIRST_PRIO);
    debugPrintf("[sched] %s: priority %d -> %d%s\n", f.name[i], (int)prio, FIRST_PRIO,
                R_SUCCEEDED(rc) ? "" : " (refused)");
  }
}
