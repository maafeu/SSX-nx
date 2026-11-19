/* ssx_scopecache.c -- RNA::ScopeC::FindParm and ::FindBlockByName cached.
 *
 * Shaders look their parameters up by name every time they are bound:
 * FindParm searches the scope's blocks by name (strcmp down a tree), then the
 * block's parameters (strcmp down a list), a few thousand times a race frame
 * on the render thread; FindBlockByName (ResolveParm, AddParm: every material
 * bind) interns the name and walks the block tree. Results are cached:
 *   - by the name pointers themselves when the names are string literals in
 *     libgame.so (almost all: the shaders' parameter tables), which cannot
 *     change;
 *   - otherwise by scope, type, index and a hash of the two names; a hit is
 *     checked against the parameter it returns (type, index, name).
 * Any change to any scope (blocks or parameters added or deleted, scopes made
 * or destroyed) bumps a generation that empties the cache; those nine
 * functions are hooked for it, and if one of them cannot be, the cache stays
 * off. Only the thread that holds the GL context uses it (the cache itself is
 * not locked). Lookups that find nothing are not cached. The PS Vita port's
 * design and offsets (source/ssx/scopecache.c, lukebrl, MIT): ~2-3 ms a race
 * frame there. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_FindParm 0xdb60cc        /* RNA::ScopeC::FindParm(char const*, TypeID, char const*, unsigned) const */
#define OFF_FindBlockByName 0xdb5ff8 /* RNA::ScopeC::FindBlockByName(char const*) const */
#define PARM_TYPE(p) (*(uint32_t *)((uint8_t *)(p) + 28))
#define PARM_NAME(p) (*(const char **)((uint8_t *)(p) + 40))
#define PARM_INDEX(p) (*(uint32_t *)((uint8_t *)(p) + 52))

volatile uintptr_t g_ssx_gl_thread; /* ssx_egl.c: the thread the context was last made current on */
static volatile uint32_t g_gen = 1;

static inline uintptr_t thread_key(void) {
  uintptr_t v;
  __asm__ volatile("mrc p15, 0, %0, c13, c0, 3" : "=r"(v));
  return v;
}
uintptr_t ssx_thread_key(void) { return thread_key(); }

/* ---------------------------------------------------------------- what changes scopes */
typedef uint64_t (*Fn8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
#define MUTATOR(n)                                                                                    \
  static Fn8 mut_orig_##n;                                                                            \
  static uint64_t mut_hook_##n(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,      \
                               uint32_t a5, uint32_t a6, uint32_t a7) {                               \
    __atomic_add_fetch(&g_gen, 1, __ATOMIC_SEQ_CST);                                                  \
    uint64_t r = mut_orig_##n(a0, a1, a2, a3, a4, a5, a6, a7);                                        \
    __atomic_add_fetch(&g_gen, 1, __ATOMIC_SEQ_CST);                                                  \
    return r;                                                                                         \
  }
MUTATOR(0) MUTATOR(1) MUTATOR(2) MUTATOR(3) MUTATOR(4) MUTATOR(5) MUTATOR(6) MUTATOR(7) MUTATOR(8)

static const struct {
  uint32_t addr, first;
  const char *name;
} k_mut[] = {
    {0xdbdba8, 0xe92d4038, "ScopeC::ScopeC"},
    {0xdc0638, 0xe92d45f8, "ScopeC::~ScopeC"},
    {0xdb5d64, 0xe92d4ff0, "ScopeC::AddBlock"},
    {0xdb6b38, 0xe92d41f0, "ScopeC::AddParm"},
    {0xdb7764, 0xe92d4038, "ScopeC::DeleteUnusedParms"},
    {0xdb56fc, 0xe92d47f0, "ScopeBlockC::ScopeBlockC"},
    {0xdb5c4c, 0xe92d41f0, "ScopeBlockC::AddParm"},
    {0xdb57fc, 0xe92d4ff0, "ScopeBlockC::DeleteAllParms"},
    {0xdb5a78, 0xe92d40f8, "ScopeBlockC::DeleteUnusedParms"},
};
#define NMUT (sizeof k_mut / sizeof k_mut[0])
static Fn8 *const k_orig[] = {&mut_orig_0, &mut_orig_1, &mut_orig_2, &mut_orig_3, &mut_orig_4,
                              &mut_orig_5, &mut_orig_6, &mut_orig_7, &mut_orig_8};
static void *const k_hook[] = {mut_hook_0, mut_hook_1, mut_hook_2, mut_hook_3, mut_hook_4,
                               mut_hook_5, mut_hook_6, mut_hook_7, mut_hook_8};
_Static_assert(sizeof k_hook / sizeof k_hook[0] == NMUT, "one hook per mutator");

/* ---------------------------------------------------------------- the caches */
#define CACHE 2048
static struct {
  uint32_t gen;
  const void *scope;
  uint32_t hblock, hparm, type, index;
  void *result;
} c2[CACHE];
#define L1 1024
static struct {
  uint32_t gen;
  const void *scope;
  const char *block, *parm;
  uint32_t type, index;
  void *result;
} c1[L1];
#define BLOCKS 512
static struct {
  uint32_t gen;
  const void *scope;
  const char *name;
  void *result;
} cb[BLOCKS];

static uintptr_t g_img_lo, g_img_hi;
static inline int literal(const char *s) { return (uintptr_t)s >= g_img_lo && (uintptr_t)s < g_img_hi; }

static void *(*o_find_parm)(const void *, const char *, uint32_t, const char *, uint32_t);
static void *(*o_find_block)(const void *, const char *);
static volatile uint32_t g_hits, g_misses, g_bhits, g_bmisses;

static inline uint32_t fnv(const char *s, uint32_t h) {
  if (!s)
    return h ^ 0x5bd1e995u;
  for (; *s; s++)
    h = (h ^ (uint8_t)*s) * 0x01000193u;
  return h;
}

static void *find_parm(const void *scope, const char *block, uint32_t type, const char *parm, uint32_t index) {
  if (thread_key() != g_ssx_gl_thread)
    return o_find_parm(scope, block, type, parm, index);
  const uint32_t gen = __atomic_load_n(&g_gen, __ATOMIC_SEQ_CST);
  const int lit = parm && literal(parm) && (!block || literal(block));
  uint32_t s1 = 0;
  if (lit) {
    s1 = (((uintptr_t)scope >> 4) ^ ((uintptr_t)block >> 2) ^ ((uintptr_t)parm * 7) ^ type * 131 ^ index * 7919) &
         (L1 - 1);
    if (c1[s1].gen == gen && c1[s1].parm == parm && c1[s1].block == block && c1[s1].scope == scope &&
        c1[s1].type == type && c1[s1].index == index) {
      void *r = c1[s1].result;
      if (PARM_TYPE(r) == type && PARM_INDEX(r) == index) {
        g_hits++;
        return r;
      }
    }
  }
  const uint32_t hb = fnv(block, 0x811C9DC5u), hp = fnv(parm, 0x811C9DC5u);
  const uint32_t slot = (((uintptr_t)scope >> 4) ^ hb * 31 ^ hp ^ type * 131 ^ index * 7919) & (CACHE - 1);
  if (c2[slot].gen == gen && c2[slot].scope == scope && c2[slot].hblock == hb && c2[slot].hparm == hp &&
      c2[slot].type == type && c2[slot].index == index) {
    void *r = c2[slot].result;
    if (r && PARM_TYPE(r) == type && PARM_INDEX(r) == index && parm && PARM_NAME(r) && !strcmp(PARM_NAME(r), parm)) {
      g_hits++;
      if (lit) {
        c1[s1].gen = gen, c1[s1].scope = scope, c1[s1].block = block, c1[s1].parm = parm;
        c1[s1].type = type, c1[s1].index = index, c1[s1].result = r;
      }
      return r;
    }
  }
  g_misses++;
  void *r = o_find_parm(scope, block, type, parm, index);
  if (r && __atomic_load_n(&g_gen, __ATOMIC_SEQ_CST) == gen) {
    c2[slot].gen = gen, c2[slot].scope = scope, c2[slot].hblock = hb, c2[slot].hparm = hp;
    c2[slot].type = type, c2[slot].index = index, c2[slot].result = r;
  }
  return r;
}

static void *find_block(const void *scope, const char *name) {
  if (!name || thread_key() != g_ssx_gl_thread || !literal(name))
    return o_find_block(scope, name);
  const uint32_t gen = __atomic_load_n(&g_gen, __ATOMIC_SEQ_CST);
  const uint32_t h = (uint32_t)(uintptr_t)name * 2654435761u;
  const uint32_t slot = (((uintptr_t)scope >> 4) ^ h ^ (h >> 15)) & (BLOCKS - 1);
  if (cb[slot].gen == gen && cb[slot].scope == scope && cb[slot].name == name) {
    g_bhits++;
    return cb[slot].result;
  }
  g_bmisses++;
  void *r = o_find_block(scope, name);
  if (r && __atomic_load_n(&g_gen, __ATOMIC_SEQ_CST) == gen)
    cb[slot].gen = gen, cb[slot].scope = scope, cb[slot].name = name, cb[slot].result = r;
  return r;
}

void ssx_scopecache_report(void) {
  if (g_hits || g_misses)
    debugPrintf("[scope] FindParm %u cached / %u searched; FindBlockByName %u cached / %u searched (10 s)\n", g_hits,
                g_misses, g_bhits, g_bmisses);
  g_hits = g_misses = g_bhits = g_bmisses = 0;
}

void ssx_scopecache_patch(so_module *m) {
  g_img_lo = (uintptr_t)m->load_virtbase;
  g_img_hi = g_img_lo + m->load_size;
  if (!ssx_hook_word_is(OFF_FindParm, 0xe92d41f0) || !ssx_hook_word_is(OFF_FindBlockByName, 0xe92d45f8)) {
    debugPrintf("[scope] FindParm is not 0.0.8430's: no cache\n");
    return;
  }
  for (unsigned i = 0; i < NMUT; i++)
    if (!ssx_hook_word_is(k_mut[i].addr, k_mut[i].first)) {
      debugPrintf("[scope] %s is not 0.0.8430's: no cache\n", k_mut[i].name);
      return;
    }
  for (unsigned i = 0; i < NMUT; i++) {
    *k_orig[i] = ssx_hook(k_mut[i].addr, k_mut[i].first, k_hook[i], k_mut[i].name);
    if (!*k_orig[i]) {
      /* a mutator hooked already bumps the generation harmlessly: the lookups stay uncached */
      debugPrintf("[scope] %s could not be hooked: no cache\n", k_mut[i].name);
      return;
    }
  }
  o_find_block = ssx_hook(OFF_FindBlockByName, 0xe92d45f8, (void *)find_block, "ScopeC::FindBlockByName");
  o_find_parm = ssx_hook(OFF_FindParm, 0xe92d41f0, (void *)find_parm, "ScopeC::FindParm");
  debugPrintf("[scope] FindParm %s, FindBlockByName %s\n", o_find_parm ? "cached" : "NOT cached",
              o_find_block ? "cached" : "NOT cached");
}
