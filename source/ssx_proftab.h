/* ssx_proftab.h -- the sampling profiler's histograms (ssx_prof.c): open-
 * addressed tables of (thread class, pc) -> samples.
 *
 * The first profiler kept one table for the whole session, first come first
 * kept: once its 8,192 slots were taken every new address was dropped, so code
 * that only began late (a race, after minutes of menus) never showed (console
 * run c3, session mt2: 668,161 samples lost, the race's own code missing). Now
 * the sampler fills a fresh table per 10 s window, and each window is merged
 * into the session's tables, which stay at most half full: when one gets
 * there it keeps its slots/4 most-hit addresses, and the samples of the rest
 * are counted as pruned. A hot address that comes late gets a slot at once
 * and its count from then on; what is dropped is the long tail of rarely hit
 * ones. No libnx here: tools/test_proftab.c runs it on the PC. MIT.
 */
#ifndef SSX_PROFTAB_H
#define SSX_PROFTAB_H

#include <stdint.h>

#define PROF_CLASSES 12 /* thread names */

typedef struct {
  uint32_t pc, lr; /* lr: the return address seen last at pc */
  uint32_t count;
  uint8_t cls;
} ProfSlot;

typedef struct {
  ProfSlot *s;
  uint32_t slots;              /* a power of two */
  uint32_t used;               /* slots taken */
  uint32_t lost;               /* samples that found no slot */
  uint32_t pruned;             /* samples of addresses dropped to make room */
  uint32_t ticks;              /* sampling ticks */
  uint32_t per[PROF_CLASSES];  /* samples per thread class, every one */
} ProfTab;

/* n samples of (cls, pc); 0 when it found no slot (the table is unchanged) */
int proftab_add(ProfTab *t, int cls, uint32_t pc, uint32_t lr, uint32_t n);

/* one sample, counted in per[] whether or not it finds a slot */
void proftab_sample(ProfTab *t, int cls, uint32_t pc, uint32_t lr);

/* empty again (the slots, used and every count) */
void proftab_clear(ProfTab *t);

/* every entry and count of w into t, keeping t at most half full (pruning
 * when it gets there); scratch has room for t->slots / 2 entries */
void proftab_merge(ProfTab *t, const ProfTab *w, ProfSlot *scratch);

#endif
