/* test_proftab.c -- source/ssx_proftab.c on the PC.
 *
 * A made-up session shaped like console run c3's: minutes of menus (a long
 * tail of addresses hit a few times each), then a long race whose code is all
 * new, every sample going first into a 10 s window table and the window then
 * merged into the session table and (race windows) the race table, as
 * ssx_prof.c does. Checks:
 *   - every sample is accounted for: in a slot, pruned, or lost;
 *   - none is lost;
 *   - the race's hot addresses keep (nearly) all their samples in both
 *     tables, though they come after the menus;
 * and prints what the old single first-come table (8,192 slots) kept.
 *
 *   clang -O2 -Isource tools/test_proftab.c source/ssx_proftab.c -o test_proftab
 *
 * Exit 0 when every check holds. MIT.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssx_proftab.h"

#define WIN_SLOTS 16384
#define CUM_SLOTS 32768
#define OLD_SLOTS 8192
#define IDLE 8              /* the svc stubs threads wait in */
#define MENU_POOL 60000     /* menu and loading code: a long tail */
#define RACE_POOL 12000     /* the race's code, none of it run in the menus */
#define PER_WINDOW 30000    /* samples in 10 s (~3,300 ticks of ~9 threads) */
#define MENU_WINDOWS 30     /* 5 minutes */
#define RACE_WINDOWS 240    /* 40 minutes */

static uint64_t g_rng = 88172645463325252ull;
static uint32_t rnd(void) {
  g_rng ^= g_rng << 13, g_rng ^= g_rng >> 7, g_rng ^= g_rng << 17;
  return (uint32_t)(g_rng >> 16);
}
static double unif(void) { return (rnd() >> 8) / 16777216.0; }

/* a Zipf-like spread: address k hit in proportion to 1 / (k + 1)^s */
static double *zipf(int n, double s) {
  double *cdf = malloc(n * sizeof *cdf), sum = 0;
  for (int k = 0; k < n; k++) {
    sum += 1.0 / pow(k + 1.0, s);
    cdf[k] = sum;
  }
  for (int k = 0; k < n; k++)
    cdf[k] /= sum;
  return cdf;
}
static int draw(const double *cdf, int n) {
  const double u = unif();
  int lo = 0, hi = n - 1;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (cdf[mid] < u)
      lo = mid + 1;
    else
      hi = mid;
  }
  return lo;
}

enum { K_IDLE, K_MENU, K_RACE };
#define PC_IDLE(i) (0x00010000u + 4u * (uint32_t)(i))
#define PC_MENU(i) (0x01000000u + 4u * (uint32_t)(i))
#define PC_RACE(i) (0x08000000u + 4u * (uint32_t)(i))
#define CLS_IDLE 0 /* the idle stubs: classes 0-2 */
#define CLS_MENU 3
#define CLS_RACE 4

static uint32_t truth_idle[3][IDLE], truth_menu[MENU_POOL], truth_race[RACE_POOL];
static uint32_t race_phase_race[RACE_POOL]; /* the race code's samples (all in race windows) */

static ProfTab tab(uint32_t slots) {
  ProfTab t;
  memset(&t, 0, sizeof t);
  t.slots = slots;
  t.s = calloc(slots, sizeof *t.s);
  return t;
}

/* counts per address as a table holds them */
static void unpack(const ProfTab *t, uint32_t *race, uint64_t *sum) {
  memset(race, 0, RACE_POOL * sizeof *race);
  *sum = 0;
  for (uint32_t i = 0; i < t->slots; i++) {
    const ProfSlot *s = &t->s[i];
    if (!s->count)
      continue;
    *sum += s->count;
    if (s->pc >= PC_RACE(0) && s->pc < PC_RACE(RACE_POOL) && s->cls == CLS_RACE)
      race[(s->pc - PC_RACE(0)) / 4] += s->count;
  }
}

static int g_fail;
static void check(int ok, const char *what) {
  printf("  %s: %s\n", ok ? "ok  " : "FAIL", what);
  if (!ok)
    g_fail = 1;
}

/* the race's hot addresses (each >= 0.01% of the race's samples): the least
 * share of its true samples that one kept, and the share of all race-code
 * samples the table holds */
static void race_report(const char *name, const uint32_t *held, const uint32_t *truth, uint64_t race_total,
                        double *worst_hot, double *share) {
  double worst = 1.0;
  uint64_t kept = 0, all = 0;
  int hot = 0;
  for (int i = 0; i < RACE_POOL; i++) {
    all += truth[i], kept += held[i] < truth[i] ? held[i] : truth[i];
    if (truth[i] >= race_total / 10000) {
      hot++;
      const double r = (double)held[i] / truth[i];
      if (r < worst)
        worst = r;
    }
  }
  *worst_hot = worst, *share = all ? (double)kept / all : 0;
  printf("  %s: %d hot race addresses, the worst kept %.1f%% of its samples; %.1f%% of all race-code samples held\n",
         name, hot, 100 * worst, 100 * *share);
}

int main(void) {
  double *menu_cdf = zipf(MENU_POOL, 1.0), *race_cdf = zipf(RACE_POOL, 1.0);
  ProfTab win = tab(WIN_SLOTS), cum = tab(CUM_SLOTS), race = tab(CUM_SLOTS), old = tab(OLD_SLOTS);
  ProfSlot *scratch = calloc(CUM_SLOTS / 2, sizeof *scratch);
  uint64_t total = 0, race_total = 0, win_lost = 0;
  uint32_t race_tabs_pruned_at = 0;
  for (int w = 0; w < MENU_WINDOWS + RACE_WINDOWS; w++) {
    const int in_race = w >= MENU_WINDOWS;
    for (int n = 0; n < PER_WINDOW; n++) {
      const double u = unif();
      int cls;
      uint32_t pc;
      if (u < 0.82) { /* waiting */
        const int i = rnd() % IDLE;
        cls = CLS_IDLE + rnd() % 3, pc = PC_IDLE(i);
        truth_idle[cls - CLS_IDLE][i]++;
      } else if (!in_race || u < 0.84) { /* menu and loading code (a little goes on in a race) */
        const int i = draw(menu_cdf, MENU_POOL);
        cls = CLS_MENU, pc = PC_MENU(i);
        truth_menu[i]++;
      } else {
        const int i = draw(race_cdf, RACE_POOL);
        cls = CLS_RACE, pc = PC_RACE(i);
        truth_race[i]++, race_phase_race[i]++;
      }
      const uint32_t lr = pc + 0x40;
      proftab_sample(&win, cls, pc, lr);
      proftab_sample(&old, cls, pc, lr);
      total++;
      race_total += in_race;
    }
    win.ticks = PER_WINDOW / 9;
    win_lost += win.lost;
    proftab_merge(&cum, &win, scratch);
    if (in_race) {
      proftab_merge(&race, &win, scratch);
      if (race.pruned && !race_tabs_pruned_at)
        race_tabs_pruned_at = (uint32_t)w;
    }
    proftab_clear(&win);
  }

  uint32_t *held = malloc(RACE_POOL * sizeof *held);
  uint64_t sum;
  int menu_distinct = 0, race_distinct = 0;
  for (int i = 0; i < MENU_POOL; i++)
    menu_distinct += truth_menu[i] > 0;
  for (int i = 0; i < RACE_POOL; i++)
    race_distinct += truth_race[i] > 0;
  printf("%llu samples in %d windows: %d distinct menu addresses, %d distinct race ones\n",
         (unsigned long long)total, MENU_WINDOWS + RACE_WINDOWS, menu_distinct, race_distinct);

  printf("session table (%u slots):\n", CUM_SLOTS);
  unpack(&cum, held, &sum);
  printf("  %u used, %u samples pruned (%.2f%%), %u lost\n", cum.used, cum.pruned, 100.0 * cum.pruned / total,
         cum.lost);
  check(sum + cum.pruned + cum.lost == total, "every sample in a slot, pruned or lost");
  check(cum.lost == 0 && win_lost == 0, "none lost (window tables nor session table)");
  check(cum.used <= CUM_SLOTS / 2, "at most half full");
  double worst, share;
  race_report("session", held, truth_race, race_total, &worst, &share);
  check(worst >= 0.95, "every hot race address keeps >= 95% of its samples in the session table");

  printf("race table (race windows alone):\n");
  unpack(&race, held, &sum);
  printf("  %u used, %u samples pruned (%.2f%%), %u lost; first pruned in window %u\n", race.used, race.pruned,
         100.0 * race.pruned / race_total, race.lost, race_tabs_pruned_at);
  check(sum + race.pruned + race.lost == race_total, "every race-window sample in a slot, pruned or lost");
  race_report("race", held, race_phase_race, race_total, &worst, &share);
  check(worst >= 0.95, "every hot race address keeps >= 95% of its samples in the race table");

  printf("the old single table (%u slots, first come first kept), for comparison:\n", OLD_SLOTS);
  unpack(&old, held, &sum);
  printf("  %u used, %u samples lost (%.2f%%)\n", old.used, old.lost, 100.0 * old.lost / total);
  race_report("old", held, truth_race, race_total, &worst, &share);
  printf(g_fail ? "FAILED\n" : "all checks hold\n");
  return g_fail;
}
