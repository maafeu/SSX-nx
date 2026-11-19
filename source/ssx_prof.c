/* ssx_prof.c -- [debug] profile: where the game's busy threads spend their time,
 * on the console.
 *
 * A thread of ours (not a game thread, so not sampled itself) wakes every
 * ~3 ms, and for each game thread with a CPU-heavy name (FGGameRender,
 * FGGameSim, the General Job Threads, the asset streamer) pauses it
 * (svcSetThreadActivity), reads its registers (svcGetThreadContext3) and lets it
 * go: a few microseconds per thread. The program counter goes into a histogram
 * per thread name, with the return address seen last at it. A thread blocked
 * in the kernel shows its svc stub: that is its idle time.
 *
 * The histograms (ssx_proftab.c): the samples go into a table of their own
 * per 10 s window (ssx_boot.c's report ends each window), and the sampler
 * thread then merges the window into
 *   - the session's table: prof.txt,
 *   - when the window was a race at full speed (ssx_patch2.c's [fps] window),
 *     the race table: prof-race.txt, the race alone without the menus and
 *     loads,
 * each kept at most half full by dropping its least-hit addresses when it gets
 * there (the header's "pruned" counts their samples). The first profiler kept
 * one table, first come first kept: once full, code that only began later (a
 * race after minutes of menus) was never seen at all (console run c3, mt2).
 * Both files are rewritten every 30 s by the sampler thread at a low
 * priority, so the game's threads never wait for it (open, write, close:
 * readable over FTP while the game runs), with the address ranges of
 * libgame.so, libgnustl_shared.so and this program, so the addresses can be
 * put to function names afterwards (tools/prof_symbols.py). Off unless
 * config.ini asks.
 *
 * The samples taken while a frame has been running for LONG_MS or more (after
 * the first START_FRAMES frames) also go to a histogram of their own,
 * prof-long.txt: what the game does in the long frames (the ~0.6 s ones at
 * every drop start and Restart), apart from the ordinary frames. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "bionic_pthread.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "dcr_path.h"
#include "ssx.h"
#include "ssx_proftab.h"
#include "util.h"

void b_pause_lock(void);
void b_pause_unlock(void);

#define WIN_SLOTS 16384  /* one 10 s window: ~30,000 samples, a few thousand addresses */
#define CUM_SLOTS 32768  /* the session's and the race's tables (at most half used) */
#define LONG_SLOTS 16384 /* the long frames' (filled directly, a few thousand addresses) */
#define CLASSES PROF_CLASSES
#define PICK 12          /* threads sampled per tick */
#define LONG_MS 100      /* a frame this long so far: its samples go to prof-long.txt too */
#define START_FRAMES 600 /* ...after start-up (its long frames are the shader compiler's) */
#define WRITE_EVERY 3    /* prof.txt and prof-race.txt every this many windows */
#define PRIO_SAMPLE 0x24 /* above the game's threads: a sample is a few microseconds */
#define PRIO_WRITE 58    /* while merging and writing: only when a core is free */

/* The sampler alone fills and reads the tables; report() only ends a window
 * (g_cur, g_ended, under g_lock). */
static ProfTab g_win[2], g_last, g_cum, g_race, g_long;
static ProfSlot *g_scratch; /* pruning: CUM_SLOTS / 2 entries */
static int g_cur;           /* the sampler fills g_win[g_cur] */
static int g_ended;         /* g_win[!g_cur] is a finished window, not yet merged */
static int g_end_race;      /* ...a race window at full speed */
static float g_end_fps;     /* ...its frames a second */
static char g_cls[CLASSES][16];
static uint32_t g_failed, g_long_frames;
static uint64_t g_long_last_frame;
static Mutex g_lock;
static Thread g_thr;
static volatile int g_run;

static int wanted(const char *name) {
  static const char *const k[] = {"FGGameRender", "FGGameSim", "General Job", "AssetStream", "rwfilesys", "main"};
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strncmp(name, k[i], strlen(k[i])))
      return 1;
  return 0;
}

static int class_of(const char *name) {
  for (int i = 0; i < CLASSES; i++) {
    if (!g_cls[i][0]) {
      snprintf(g_cls[i], sizeof g_cls[i], "%s", name);
      return i;
    }
    if (!strncmp(g_cls[i], name, sizeof g_cls[i]))
      return i;
  }
  return CLASSES - 1;
}

typedef struct {
  Handle h[PICK];
  char name[PICK][16];
  int n;
} Pick;

/* What svcGetThreadContext3 writes, for a 32-bit caller too: the kernel's
 * 64-bit layout, 0x320 bytes (an AArch32 thread's r0-r14 in r[0..14], its pc
 * in pc). libnx32's ThreadContext declares 32-bit registers (~0x198 bytes):
 * the first build passed that and the kernel wrote ~400 bytes past it on the
 * sampler's stack, and every pc read 0 (console run c3). Room to spare here. */
typedef struct {
  u64 r[29];
  u64 fp, lr, sp, pc;
  u32 pstate, pad;
  u64 v[64];
  u32 fpcr, fpsr;
  u64 tpidr;
  u8 spare[0x100];
} __attribute__((aligned(16))) KContext;
_Static_assert(sizeof(KContext) - 0x100 == 0x320, "the kernel's ThreadContext is 0x320 bytes");

static u32 g_raw[0x320 / 4]; /* one context as read, for prof.txt */
static int g_have_raw;

static void pick_thread(BThread *t, void *arg) {
  Pick *p = arg;
  if (p->n < PICK && t->handle && !t->finished && t->name[0] && wanted(t->name)) {
    p->h[p->n] = t->handle;
    memcpy(p->name[p->n], t->name, 16);
    p->name[p->n][15] = 0;
    p->n++;
  }
}

static void write_hist(const char *file, const ProfTab *t);

/* n as a percentage of all of t's samples */
static double share(uint32_t n, const ProfTab *t) {
  uint64_t all = 0;
  for (int c = 0; c < CLASSES; c++)
    all += t->per[c];
  return all ? 100.0 * n / (double)all : 0.0;
}

/* The window report() just ended, if there is one: merged, and the files
 * written. On the sampler thread, at PRIO_WRITE. */
static void finish_window(void) {
  mutexLock(&g_lock);
  const int ended = g_ended, race = g_end_race;
  const float fps = g_end_fps;
  ProfTab *const w = &g_win[!g_cur];
  mutexUnlock(&g_lock);
  if (!ended)
    return;
  static unsigned windows, race_windows, race_written, dips, long_written;
  static uint32_t pruned_logged;
  static int have_last;
  svcSetThreadPriority(CUR_THREAD_HANDLE, PRIO_WRITE);
  proftab_merge(&g_cum, w, g_scratch);
  if (race) {
    proftab_merge(&g_race, w, g_scratch);
    race_windows++;
  }
  /* a race window at full speed that drew under 59 frames a second (a dip)
   * gets its own profile, prof-dip<n>.txt with that window's samples alone
   * and prof-dip<n>-before.txt with the window before it, the first 3 dips of
   * a run: the cumulative files take ~20 s to read over the console's FTP
   * while the game runs, too slow to bracket one 10 s window from outside */
  if (race && fps > 0 && fps < 59.0f && dips < 3 && have_last) {
    dips++;
    char file[48];
    snprintf(file, sizeof file, "prof-dip%u.txt", dips);
    write_hist(file, w);
    snprintf(file, sizeof file, "prof-dip%u-before.txt", dips);
    write_hist(file, &g_last);
    debugPrintf("[prof] a dip (%.1f frames a second): its 10 s in prof-dip%u.txt, the 10 s before in "
                "prof-dip%u-before.txt\n",
                (double)fps, dips, dips);
  }
  ProfSlot *const keep = g_last.s; /* this window becomes the one before */
  g_last = *w;
  g_last.s = keep;
  memcpy(g_last.s, w->s, WIN_SLOTS * sizeof *w->s);
  have_last = 1;
  proftab_clear(w);
  windows++;
  if (g_long_frames != long_written) { /* a new long frame since the last write */
    long_written = g_long_frames;
    write_hist("prof-long.txt", &g_long);
    debugPrintf("[prof] %u long frames so far (past %d ms): %u sampling ticks in them, in prof-long.txt\n",
                (unsigned)g_long_frames, LONG_MS, (unsigned)g_long.ticks);
  }
  if (windows % WRITE_EVERY == 0) {
    write_hist("prof.txt", &g_cum);
    if (race_windows != race_written) {
      race_written = race_windows;
      write_hist("prof-race.txt", &g_race);
    }
    if (g_cum.pruned + g_race.pruned != pruned_logged) {
      pruned_logged = g_cum.pruned + g_race.pruned;
      debugPrintf("[prof] tables at half full keep their most-hit addresses: prof.txt %u kept, %u samples of "
                  "rarer ones dropped (%.2f%%); prof-race.txt (%u race windows) %u kept, %u dropped (%.2f%%)\n",
                  (unsigned)g_cum.used, (unsigned)g_cum.pruned, share(g_cum.pruned, &g_cum), race_windows,
                  (unsigned)g_race.used, (unsigned)g_race.pruned, share(g_race.pruned, &g_race));
    }
  }
  mutexLock(&g_lock);
  g_ended = 0;
  mutexUnlock(&g_lock);
  svcSetThreadPriority(CUR_THREAD_HANDLE, PRIO_SAMPLE);
}

static void sampler(void *arg) {
  (void)arg;
  u64 seed = armGetSystemTick();
  while (g_run) {
    finish_window();
    seed = seed * 6364136223846793005ull + 1442695040888963407ull;
    svcSleepThread(2500000 + (int64_t)((seed >> 33) % 1000000)); /* 2.5-3.5 ms */
    Pick p;
    p.n = 0;
    b_thread_foreach(pick_thread, &p);
    uint64_t frame = 0;
    const int in_long = dcr_boost_frame_running_ms(&frame) >= LONG_MS && frame > START_FRAMES;
    for (int i = 0; i < p.n; i++) {
      static KContext ctx; /* static: this thread alone, and off its stack */
      b_pause_lock();
      Result rc = svcSetThreadActivity(p.h[i], ThreadActivity_Paused);
      Result rc2 = R_SUCCEEDED(rc) ? svcGetThreadContext3((ThreadContext *)(void *)&ctx, p.h[i]) : rc;
      if (R_SUCCEEDED(rc))
        svcSetThreadActivity(p.h[i], ThreadActivity_Runnable);
      b_pause_unlock();
      if (R_FAILED(rc2)) {
        g_failed++;
        continue;
      }
      mutexLock(&g_lock);
      if (!g_have_raw && !strcmp(p.name[i], "FGGameRender")) {
        memcpy(g_raw, &ctx, sizeof g_raw);
        g_have_raw = 1;
      }
      const int c = class_of(p.name[i]);
      proftab_sample(&g_win[g_cur], c, (uint32_t)ctx.pc, (uint32_t)ctx.r[14]);
      if (in_long)
        proftab_sample(&g_long, c, (uint32_t)ctx.pc, (uint32_t)ctx.r[14]);
      mutexUnlock(&g_lock);
    }
    mutexLock(&g_lock);
    g_win[g_cur].ticks++;
    if (in_long) {
      g_long.ticks++;
      if (frame != g_long_last_frame)
        g_long_last_frame = frame, g_long_frames++;
    }
    mutexUnlock(&g_lock);
  }
}

static int alloc_tab(ProfTab *t, uint32_t slots) {
  memset(t, 0, sizeof *t);
  t->slots = slots;
  t->s = calloc(slots, sizeof *t->s);
  return t->s != NULL;
}

void ssx_prof_start(void) {
  if (!dcr_config()->profile || g_run || dcr_is_emulator())
    return;
  /* the tables only when profiling: ~2.3 MB of the heap */
  g_scratch = calloc(CUM_SLOTS / 2, sizeof *g_scratch);
  if (!g_scratch || !alloc_tab(&g_win[0], WIN_SLOTS) || !alloc_tab(&g_win[1], WIN_SLOTS) ||
      !alloc_tab(&g_last, WIN_SLOTS) || !alloc_tab(&g_cum, CUM_SLOTS) || !alloc_tab(&g_race, CUM_SLOTS) ||
      !alloc_tab(&g_long, LONG_SLOTS)) {
    debugPrintf("[prof] no memory for the tables: not sampling\n");
    return;
  }
  g_run = 1;
  /* 64 KB of stack: the sampler also writes the files (stdio, qsort) */
  Result rc = threadCreate(&g_thr, sampler, NULL, NULL, 0x10000, PRIO_SAMPLE, -2);
  if (R_SUCCEEDED(rc))
    rc = threadStart(&g_thr);
  if (R_FAILED(rc)) {
    g_run = 0;
    debugPrintf("[prof] the sampler did not start (0x%x)\n", (unsigned)rc);
    return;
  }
  debugPrintf("[prof] sampling the game's busy threads every ~3 ms into prof.txt, the race windows also into "
              "prof-race.txt (frames past %d ms also into prof-long.txt)\n",
              LONG_MS);
}

/* Every 10 s (ssx_boot.c's report, just after the [fps] line): the window
 * ends; the sampler merges and writes it on its next tick. If it hasn't
 * finished the last one yet, this window runs on into the next. */
void ssx_prof_write(void) {
  if (!g_run)
    return;
  extern float g_ssx_window_render_fps; /* ssx_patch2.c */
  extern int g_ssx_window_race_full;
  mutexLock(&g_lock);
  if (!g_ended) {
    g_end_race = g_ssx_window_race_full;
    g_end_fps = g_ssx_window_render_fps;
    g_cur ^= 1;
    g_ended = 1;
  }
  mutexUnlock(&g_lock);
}

static void write_hist(const char *file, const ProfTab *t) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), file);
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  setvbuf(f, NULL, _IOFBF, 64 * 1024);
  fprintf(f, "# ssx_prof: ticks %u, lost %u, failed %u, pruned %u, uptime_ms %llu\n", (unsigned)t->ticks,
          (unsigned)t->lost, (unsigned)g_failed, (unsigned)t->pruned,
          (unsigned long long)(armTicksToNs(armGetSystemTick()) / 1000000ull));
  fprintf(f, "game 0x%08x 0x%08x\n", (unsigned)(uintptr_t)g_mod_game.load_virtbase, (unsigned)g_mod_game.load_size);
  fprintf(f, "stl 0x%08x 0x%08x\n", (unsigned)(uintptr_t)g_mod_stl.load_virtbase, (unsigned)g_mod_stl.load_size);
  fprintf(f, "port ssx_prof_write 0x%08x\n", (unsigned)(uintptr_t)ssx_prof_write);
  if (g_have_raw) { /* one FGGameRender context as the kernel wrote it (32-bit words) */
    fprintf(f, "# raw");
    for (unsigned i = 0; i < 0x120 / 4; i++)
      fprintf(f, " %08x", (unsigned)g_raw[i]);
    fprintf(f, "\n");
  }
  for (int c = 0; c < CLASSES; c++)
    if (g_cls[c][0])
      fprintf(f, "class %d %u %s\n", c, (unsigned)t->per[c], g_cls[c]);
  for (uint32_t i = 0; i < t->slots; i++)
    if (t->s[i].count)
      fprintf(f, "s %d 0x%08x 0x%08x %u\n", t->s[i].cls, (unsigned)t->s[i].pc, (unsigned)t->s[i].lr,
              (unsigned)t->s[i].count);
  fclose(f);
}
