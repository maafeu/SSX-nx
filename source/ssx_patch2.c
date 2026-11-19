/* ssx_patch2.c -- SSX's frame rate (and the other patches that run after the
 * essentials of ssx_patch.c).
 *
 * 60 FPS. The game paces itself at 30 frames a second, in three places:
 *   - EA::Blast::FpsLimiter: made at 60, then set to 30 by the game; its
 *     OnEndOfFrame sleeps until 1000/limit ms have passed. Kept at 60 (or
 *     more): vsync still caps at 60.
 *   - the render loop (LoopRender) and the simulation (LoopSimulation) are
 *     GameKernel::Loop objects made at a hard-coded 30.0 Hz; Loop::Run sleeps
 *     until a period has passed since the last iteration. A thread here sets
 *     their rates (Loop::SetLoopFrequency) as the game goes.
 * In menus both run at [display] menu_fps (60). A race's simulation advances
 * game time by exactly one period per step and catches up at most one period,
 * so a simulation that cannot make its steps a second plays the race in slow
 * motion (the riders' animation jobs, EA::Ant, are the step's long pole).
 * [display] race_sim_hz picks its rate: auto (default) 60 Hz, and 30 Hz for
 * the rest of a race the console cannot keep up with (race_sim_rate below).
 * The render loop runs at [display] race_fps (60): at a 30 Hz simulation the
 * render objects interpolate between two simulation states (Model / Skel /
 * ScopeRenderObjState::InterpolateState), so the
 * frames in between show motion, not repeats. A race is when its world is
 * loaded: one ~24.6 MB block of the GAMEWORLD_SLOTALLOC heap (25 MB), from
 * the loading screen to the menus after the results.
 * (All of this was worked out by the PS Vita port, source/ssx/fpscap.c; the
 * offsets are libgame.so 0.0.8430's.) MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_SetFpsLimit 0x10bd6c      /* EA::Blast::FpsLimiter::SetFpsLimit(unsigned) */
#define OFF_OnEndOfFrame 0x10bcb8     /* EA::Blast::FpsLimiter::OnEndOfFrame() */
#define OFF_Loop_ctor 0x105a03c       /* GameKernel::Loop::Loop(float hz, ProcessType, unsigned) */
#define OFF_SetLoopFrequency 0x105a190 /* GameKernel::Loop::SetLoopFrequency(float) */
#define OFF_GetFirstAllocator 0x15d904 /* MemoryFramework::GetFirstAllocator(sEnumAllocator&, MemoryAllocatorUsage&) */
#define OFF_GetNextAllocator 0x15d798

#define LOOP_SIM 0
#define LOOP_RENDER 1
#define LOOP_HZ(l) (*(volatile float *)((uint8_t *)(l) + 28))
#define LOOP_ITERATIONS(l) (*(volatile uint32_t *)((uint8_t *)(l) + 96))
#define LOOP_AVG_NS(l) (*(volatile int64_t *)((uint8_t *)(l) + 128))
#define WORLD_HEAP "GAMEWORLD_SLOTALLOC"
#define WORLD_LOADED (12u * 1024 * 1024) /* its largest free block under this: a race world is in */

typedef struct {
  uint64_t frame_ms;
} FpsLimiter;
typedef struct {
  const char *name;
  void *allocator;
  uint32_t allocated, high_water, size, pad[3];
} MemoryAllocatorUsage;
typedef struct {
  int index;
  uint32_t pad[3];
} sEnumAllocator;

static void (*o_set_fps_limit)(FpsLimiter *, unsigned);
static void (*o_on_end_of_frame)(FpsLimiter *);
static void *(*o_loop_ctor)(void *, float, int, unsigned);
static void *volatile g_sim_loop, *volatile g_render_loop;
static volatile int g_in_race = -1;
static unsigned g_min_fps = 60;

static void set_fps_limit(FpsLimiter *self, unsigned fps) {
  debugPrintf("[fps] the game sets its frame limit to %u%s\n", fps, fps < g_min_fps ? " (kept at the menu rate)" : "");
  o_set_fps_limit(self, fps < g_min_fps ? g_min_fps : fps);
}

static void on_end_of_frame(FpsLimiter *self) {
  const uint64_t ms = 1000 / g_min_fps;
  if (self->frame_ms > ms)
    self->frame_ms = ms;
  o_on_end_of_frame(self);
}

static void *loop_ctor(void *self, float hz, int type, unsigned arg) {
  debugPrintf("[fps] game loop %s made at %.1f Hz\n", type == LOOP_SIM ? "simulation" : type == LOOP_RENDER ? "render" : "?", hz);
  if (type == LOOP_SIM)
    g_sim_loop = self;
  else if (type == LOOP_RENDER)
    g_render_loop = self;
  return o_loop_ctor(self, hz, type, arg);
}

static void *find_allocator(const char *name) {
  int (*first)(sEnumAllocator *, MemoryAllocatorUsage *) = (void *)ssx_text(OFF_GetFirstAllocator);
  int (*next)(sEnumAllocator *, MemoryAllocatorUsage *) = (void *)ssx_text(OFF_GetNextAllocator);
  sEnumAllocator e;
  MemoryAllocatorUsage u;
  memset(&e, 0, sizeof e);
  memset(&u, 0, sizeof u);
  if (first(&e, &u)) {
    do {
      if (u.name && u.allocator && !strcmp(u.name, name))
        return u.allocator;
      memset(&u, 0, sizeof u);
    } while (next(&e, &u));
  }
  return NULL;
}

static unsigned largest_free(void *allocator) {
  uint32_t (**vtbl)(void *) = *(uint32_t (***)(void *))allocator; /* IAllocator slot 16: GetLargestFreeBlock() */
  return vtbl[16](allocator);
}

static void heaps_sample(void);

/* [display] race_sim_hz = auto: races at 60 simulation steps a second, back
 * to 30 for the rest of a race the console cannot keep up with. At 60 every
 * frame is a real step (input to picture one frame sooner, no interpolated
 * frames); at 30 a race that cannot make 60 steps still plays at full speed.
 * Measured once a second, from the loops' iteration counts only (the loop's
 * "ms each" field is no step's cost: 0.7 ms in the middle of a descent on the
 * console). Behind means: 5 to 53 steps a second (0 is the title; the pause
 * menu keeps stepping) while frames are drawn (>= 45 a second: not HOME, not
 * a load). "Armed" once the race made >= 57 steps a second for 2 s (the load
 * and the fly-in never do); armed, 3 s behind in a row drop it to 30. A race
 * that never arms drops after 20 s behind with smooth frames (>= 55: loading
 * screens draw fewer). It stays at 30 until the next race (no flip-flopping).
 * Hardware (2026-11-16, the first drop's trick tutorial, one rider): 60 steps
 * a second at 10-11 ms each, render 60 at 8-10 ms. */
static int race_sim_rate(int race, int new_race) {
  static int hz, armed, good, slow, smooth_slow;
  static uint32_t s_prev, r_prev;
  static u64 t_prev;
  static unsigned ticks;
  const DcrConfig *c = dcr_config();
  if (!race)
    return c->menu_fps;
  if (new_race) {
    hz = c->race_sim_hz, armed = good = slow = smooth_slow = 0, ticks = 0, t_prev = 0;
  }
  if (!c->race_sim_auto || hz != 60 || ++ticks % 10)
    return hz;
  const u64 now = armGetSystemTick();
  const uint32_t s = LOOP_ITERATIONS(g_sim_loop), r = LOOP_ITERATIONS(g_render_loop);
  if (t_prev && LOOP_HZ(g_sim_loop) == 60.0f) {
    const double secs = (double)armTicksToNs(now - t_prev) / 1e9;
    const double sps = (s - s_prev) / secs, fps = (r - r_prev) / secs;
    const int behind = sps < 54 && sps >= 5 && fps >= 45;
    if (!armed) {
      good = sps >= 57 ? good + 1 : 0;
      armed = good >= 2;
      smooth_slow = (behind && fps >= 55) ? smooth_slow + 1 : 0;
      if (smooth_slow >= 20) {
        hz = 30;
        debugPrintf("[fps] race simulation 60 -> 30 Hz: %.1f steps a second for 20 s (never at full speed)\n", sps);
      }
    } else {
      slow = behind ? slow + 1 : 0;
      if (slow >= 3) {
        hz = 30;
        debugPrintf("[fps] race simulation 60 -> 30 Hz: %.1f steps a second (%.1f frames) for 3 s\n", sps, fps);
      }
    }
  }
  t_prev = now, s_prev = s, r_prev = r;
  return hz;
}

static void loop_rate_thread(void *arg) {
  void (*set_freq)(void *, float) = (void *)ssx_text(OFF_SetLoopFrequency);
  void *world = NULL;
  for (unsigned tick = 0;; tick++) {
    svcSleepThread(100000000ll);
    if (!g_sim_loop || !g_render_loop)
      continue;
    if (tick % 10 == 0)
      heaps_sample();
    if (!world && !(world = find_allocator(WORLD_HEAP)))
      continue;
    const int race = largest_free(world) < WORLD_LOADED;
    /* [display] race_sim_hz: auto (above), 30 (the game's own) or 60 (every
     * frame a simulation step: needs 60 steps a second, or the race slows
     * down; the 10 s [fps] line shows the speed) */
    const float sim = (float)race_sim_rate(race, race && g_in_race != 1);
    const float render = race ? (float)dcr_config()->race_fps : (float)dcr_config()->menu_fps;
    if (LOOP_HZ(g_sim_loop) != sim || LOOP_HZ(g_render_loop) != render) {
      set_freq(g_sim_loop, sim);
      set_freq(g_render_loop, render);
      debugPrintf("[fps] simulation %.0f Hz, render %.0f Hz (%s)\n", sim, render, race ? "a race" : "menus");
    }
    g_in_race = race;
  }
}

/* EA's heaps (MemoryFramework): the game keeps no use counters in this
 * build (the usage's allocated / high-water fields stay 0), so each heap's
 * largest free block is sampled every second (loop_rate_thread) and the
 * least seen kept: size minus that is the most the heap held (an upper
 * bound: fragmentation counts as use). debug.log every 60 s: "NAME
 * held/size" in KB, to size memory.cfg's heaps from this port's own numbers
 * (ssx_assetpatch.c). */
#define MAX_HEAPS 48
static struct {
  const char *name;
  void *allocator;
  uint32_t size, least_free;
} g_heaps[MAX_HEAPS];
static volatile int g_nheaps;

static void heaps_sample(void) {
  int (*first)(sEnumAllocator *, MemoryAllocatorUsage *) = (void *)ssx_text(OFF_GetFirstAllocator);
  int (*next)(sEnumAllocator *, MemoryAllocatorUsage *) = (void *)ssx_text(OFF_GetNextAllocator);
  sEnumAllocator e;
  MemoryAllocatorUsage u;
  memset(&e, 0, sizeof e);
  memset(&u, 0, sizeof u);
  if (!first(&e, &u))
    return;
  do {
    if (u.name && u.allocator && u.size) {
      int i = 0;
      while (i < g_nheaps && g_heaps[i].allocator != u.allocator)
        i++;
      if (i == g_nheaps && i < MAX_HEAPS) {
        g_heaps[i].name = u.name, g_heaps[i].allocator = u.allocator, g_heaps[i].size = u.size;
        g_heaps[i].least_free = u.size;
        g_nheaps = i + 1;
      }
      if (i < g_nheaps) {
        const uint32_t f = largest_free(u.allocator);
        if (f < g_heaps[i].least_free)
          g_heaps[i].least_free = f;
      }
    }
    memset(&u, 0, sizeof u);
  } while (next(&e, &u));
}

static void heaps_report(void) {
  static unsigned calls;
  if (calls++ % 6 || !g_nheaps)
    return;
  char line[1536];
  size_t n = (size_t)snprintf(line, sizeof line, "[heaps] most held / size (KB):");
  for (int i = 0; i < g_nheaps && n < sizeof line - 64; i++)
    n += (size_t)snprintf(line + n, sizeof line - n, " %s %u/%u", g_heaps[i].name,
                          (unsigned)((g_heaps[i].size - g_heaps[i].least_free) >> 10), (unsigned)(g_heaps[i].size >> 10));
  debugPrintf("%s\n", line);
}

/* For the frame-rate counter (ssx_overlay.c): the simulation loop's
 * iterations and rate; returns 1 in a race, 0 elsewhere, -1 before the
 * loops exist. */
int ssx_loop_stats(uint32_t *sim_iterations, float *sim_hz) {
  if (!g_sim_loop)
    return -1;
  *sim_iterations = LOOP_ITERATIONS(g_sim_loop);
  *sim_hz = LOOP_HZ(g_sim_loop);
  return g_in_race == 1;
}

/* The window ssx_fps_report just closed, for the profiler (ssx_prof.c: a
 * race window at full speed that drew under 59 frames a second gets its own
 * profile). 0 before the first window. */
float g_ssx_window_render_fps;
int g_ssx_window_race_full;

void ssx_fps_report(void) {
  static uint32_t sim_prev, render_prev;
  static u64 t_prev;
  if (g_ssx_frames_presented > 0)
    heaps_report();
  if (!g_sim_loop || !g_render_loop)
    return;
  u64 now = armGetSystemTick();
  uint32_t s = LOOP_ITERATIONS(g_sim_loop), r = LOOP_ITERATIONS(g_render_loop);
  if (t_prev) {
    double secs = (double)armTicksToNs(now - t_prev) / 1e9;
    double sr = (s - sim_prev) / secs, rr = (r - render_prev) / secs;
    debugPrintf("[fps] simulation %.0f Hz: %.1f steps a second (game at %.0f%% speed), %.1f ms each; "
                "render %.0f Hz: %.1f a second, %.1f ms each%s\n",
                LOOP_HZ(g_sim_loop), sr, LOOP_HZ(g_sim_loop) > 0 ? sr / LOOP_HZ(g_sim_loop) * 100 : 0,
                LOOP_AVG_NS(g_sim_loop) / 1e6, LOOP_HZ(g_render_loop), rr, LOOP_AVG_NS(g_render_loop) / 1e6,
                g_in_race == 1 ? " (race)" : "");
    g_ssx_window_render_fps = (float)rr;
    g_ssx_window_race_full = g_in_race == 1 && LOOP_HZ(g_sim_loop) > 0 && sr / LOOP_HZ(g_sim_loop) >= 0.995;
  }
  t_prev = now, sim_prev = s, render_prev = r;
}

void ssx_hwskin_patch(void); /* ssx_hwskin.c */

void ssx_patch_more(void) {
  ssx_hwskin_patch();
  g_min_fps = dcr_config()->menu_fps;
  o_set_fps_limit = ssx_hook(OFF_SetFpsLimit, 0xe92d4010, (void *)set_fps_limit, "FpsLimiter::SetFpsLimit");
  o_on_end_of_frame = ssx_hook(OFF_OnEndOfFrame, 0xe92d40f0, (void *)on_end_of_frame, "FpsLimiter::OnEndOfFrame");
  o_loop_ctor = ssx_hook(OFF_Loop_ctor, 0xe92d41f0, (void *)loop_ctor, "GameKernel::Loop::Loop");
  if (o_loop_ctor) {
    static Thread t;
    if (R_SUCCEEDED(threadCreate(&t, loop_rate_thread, NULL, NULL, 0x4000, 0x2C, -2)))
      threadStart(&t);
  }
  void ssx_terraincull_patch(void); /* ssx_terraincull.c */
  ssx_terraincull_patch();
  void ssx_detail_patch(void); /* ssx_detail.c */
  ssx_detail_patch();
  void ssx_boardpress_patch(void); /* ssx_boardpress.c */
  ssx_boardpress_patch();
  void ssx_ikdebug_patch(void); /* ssx_ikdebug.c */
  ssx_ikdebug_patch();
  void ssx_spinwait_patch(void); /* ssx_spinwait.c */
  ssx_spinwait_patch();
  if (dcr_config()->native_atomics) {
    void ssx_atomic64_patch(void); /* ssx_atomic64.c */
    ssx_atomic64_patch();
  }
}
