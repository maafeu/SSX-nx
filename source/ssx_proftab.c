/* ssx_proftab.c -- the profiler's histograms: see ssx_proftab.h. MIT. */
#include "ssx_proftab.h"

#include <stdlib.h>
#include <string.h>

/* linear probing: at half full (the most a session table gets) a few runs of
 * taken slots pass 32, and an address meeting one would be lost */
#define PROBES 128

/* the slot to start at: all 32 bits mixed (MurmurHash3's finalizer), for the
 * table index is the low bits, and ARM pcs are multiples of 4 (2 in Thumb):
 * pc * odd, as the first profiler had, never starts at 3 slots in 4 */
static inline uint32_t mix(uint32_t pc, int cls) {
  uint32_t h = pc ^ (uint32_t)cls * 0x9e3779b9u;
  h ^= h >> 16, h *= 0x85ebca6bu;
  h ^= h >> 13, h *= 0xc2b2ae35u;
  return h ^ (h >> 16);
}

int proftab_add(ProfTab *t, int cls, uint32_t pc, uint32_t lr, uint32_t n) {
  const uint32_t h = mix(pc, cls);
  for (int probe = 0; probe < PROBES; probe++) {
    ProfSlot *s = &t->s[(h + (uint32_t)probe) & (t->slots - 1)];
    if (s->count && s->pc == pc && s->cls == cls) {
      s->count += n;
      s->lr = lr;
      return 1;
    }
    if (!s->count) {
      s->pc = pc, s->lr = lr, s->cls = (uint8_t)cls, s->count = n;
      t->used++;
      return 1;
    }
  }
  return 0;
}

void proftab_sample(ProfTab *t, int cls, uint32_t pc, uint32_t lr) {
  t->per[cls]++;
  if (!proftab_add(t, cls, pc, lr, 1))
    t->lost++;
}

void proftab_clear(ProfTab *t) {
  memset(t->s, 0, t->slots * sizeof *t->s);
  t->used = t->lost = t->pruned = t->ticks = 0;
  memset(t->per, 0, sizeof t->per);
}

static int by_count_down(const void *a, const void *b) {
  const uint32_t x = ((const ProfSlot *)a)->count, y = ((const ProfSlot *)b)->count;
  return x < y ? 1 : x > y ? -1 : 0;
}

/* keep the slots/4 most-hit addresses; called with used <= slots/2 */
static void prune(ProfTab *t, ProfSlot *scratch) {
  uint32_t n = 0;
  for (uint32_t i = 0; i < t->slots; i++)
    if (t->s[i].count)
      scratch[n++] = t->s[i];
  qsort(scratch, n, sizeof *scratch, by_count_down);
  uint32_t keep = t->slots / 4;
  if (keep > n)
    keep = n;
  for (uint32_t i = keep; i < n; i++)
    t->pruned += scratch[i].count;
  memset(t->s, 0, t->slots * sizeof *t->s);
  t->used = 0;
  for (uint32_t i = 0; i < keep; i++)
    if (!proftab_add(t, scratch[i].cls, scratch[i].pc, scratch[i].lr, scratch[i].count))
      t->lost += scratch[i].count;
}

void proftab_merge(ProfTab *t, const ProfTab *w, ProfSlot *scratch) {
  for (uint32_t i = 0; i < w->slots; i++) {
    const ProfSlot *e = &w->s[i];
    if (!e->count)
      continue;
    if (t->used >= t->slots / 2)
      prune(t, scratch);
    if (!proftab_add(t, e->cls, e->pc, e->lr, e->count))
      t->lost += e->count;
  }
  t->lost += w->lost;
  t->pruned += w->pruned;
  t->ticks += w->ticks;
  for (int c = 0; c < PROF_CLASSES; c++)
    t->per[c] += w->per[c];
}
