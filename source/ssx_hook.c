/* ssx_hook.c -- hooking libgame.so's functions by address, before its code is
 * sealed (ssx_loader.c calls ssx_patch_game between so_resolve and
 * so_finalize: the image is in its staging copy, load_base).
 *
 * Every hook is checked against the first word libgame.so 0.0.8430 has
 * there: another build is left alone, with a line in debug.log.
 *
 * A hook that still calls the function it replaces gets a trampoline: the
 * function's first two ARM instructions (the 8 bytes the jump overwrites)
 * are copied, then a jump back to the third. Instructions that read pc are
 * rewritten for their new place (a literal load becomes a load from the
 * trampoline's own pool, a branch jumps to its old target, `add rd, pc, rm`
 * adds the old pc); anything else that touches pc is refused (no hook).
 * Thumb functions are refused too: the hooked ones are all ARM. The
 * trampolines live in libgame.so's own code, in the unused tail of the page
 * its executable segment ends in (mapped executable with the rest, sealed
 * with it). The method and the instruction mover are the Sonic & SEGA
 * All-Stars Racing port's (aks796, ssr_patch.c; MIT). MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

static so_module *g_m;
static uint32_t g_pool, g_pool_end;

static uint32_t *stage_at(uint32_t vaddr) { return (uint32_t *)((uint8_t *)g_m->load_base + vaddr); }
static uint32_t live_at(uint32_t vaddr) { return (uint32_t)(uintptr_t)g_m->load_virtbase + vaddr; }

void ssx_hook_begin(so_module *m) {
  g_m = m;
  /* the end of the executable PT_LOAD, to its page's end */
  uint32_t end = 0;
  for (int i = 0; i < m->phnum; i++)
    if (m->phdr[i].p_type == PT_LOAD && (m->phdr[i].p_flags & PF_X))
      end = m->phdr[i].p_vaddr + m->phdr[i].p_memsz;
  g_pool = (end + 15) & ~7u;
  g_pool_end = (end + 0xfff) & ~0xfffu;
  debugPrintf("[hook] trampolines at 0x%06x..0x%06x\n", (unsigned)g_pool, (unsigned)g_pool_end);
}

int ssx_hook_word_is(uint32_t vaddr, uint32_t expect) { return g_m && *stage_at(vaddr) == expect; }

int ssx_patch_word(uint32_t vaddr, uint32_t expect, uint32_t value) {
  if (!g_m)
    return 0;
  uint32_t *p = stage_at(vaddr);
  if (*p != expect) {
    debugPrintf("[hook] 0x%06x is 0x%08x, not 0x%08x: left alone\n", (unsigned)vaddr, (unsigned)*p,
                (unsigned)expect);
    return 0;
  }
  *p = value;
  return 1;
}

/* One word, unchecked (after ssx_hook_word_is checks of the block). */
void ssx_poke_word(uint32_t vaddr, uint32_t value) {
  if (g_m)
    *stage_at(vaddr) = value;
}

/* The live address of a libgame.so vaddr (where it runs once sealed). */
uintptr_t ssx_hook_live(uint32_t vaddr) { return g_m ? live_at(vaddr) : 0; }

int ssx_hook_jump(uint32_t vaddr, uint32_t expect, void *dst) {
  if (!g_m || *stage_at(vaddr) != expect) {
    if (g_m)
      debugPrintf("[hook] jump at 0x%06x: 0x%08x, not 0x%08x: left alone\n", (unsigned)vaddr,
                  (unsigned)*stage_at(vaddr), (unsigned)expect);
    return 0;
  }
  const uint32_t stub[2] = {0xe51ff004u /* ldr pc, [pc, #-4] */, (uint32_t)(uintptr_t)dst};
  memcpy(stage_at(vaddr), stub, sizeof stub);
  return 1;
}

/* ------------------------------------------------------------ trampolines */
enum { LIT_MAX = 6 };
typedef struct {
  uint32_t word[12];
  int n;
  struct {
    int at;
    uint32_t value;
  } lit[LIT_MAX];
  int nlit;
} Tramp;

static int tr_lit(Tramp *t, uint32_t value) {
  if (t->nlit >= LIT_MAX)
    return -1;
  t->lit[t->nlit].at = t->n;
  t->lit[t->nlit].value = value;
  return t->nlit++;
}

static int tr_ldr_lit(Tramp *t, int rd, uint32_t value) {
  if (tr_lit(t, value) < 0)
    return 0;
  t->word[t->n++] = 0xe59f0000u | ((uint32_t)rd << 12);
  return 1;
}

static int tr_move(Tramp *t, uint32_t pc_vaddr) {
  const uint32_t ins = *stage_at(pc_vaddr);
  const uint32_t pc = live_at(pc_vaddr) + 8;
  if (ins >> 28 != 0xe)
    return 0;
  if ((ins & 0x0fff0000u) == 0x092d0000u) { /* push */
    t->word[t->n++] = ins;
    return 1;
  }
  if ((ins & 0x0e000000u) == 0x0a000000u) { /* b / bl */
    int32_t off = (int32_t)(ins << 8) >> 6;
    const uint32_t target = pc + (uint32_t)off;
    if (ins & 0x01000000u) {
      if (!tr_ldr_lit(t, 12, target))
        return 0;
      t->word[t->n++] = 0xe12fff3cu; /* blx ip */
    } else if (!tr_ldr_lit(t, 15, target)) {
      return 0;
    }
    return 1;
  }
  const int rn = (ins >> 16) & 15, rd = (ins >> 12) & 15, rm = ins & 15;
  if ((ins & 0x0e000000u) == 0x0c000000u && rn != 15) { /* VFP vpush / vstm / vldr..., not pc-relative */
    t->word[t->n++] = ins;
    return 1;
  }
  if ((ins & 0x0e000000u) == 0x04000000u) { /* ldr / str, immediate */
    if (rd == 15)
      return 0;
    if (rn != 15) {
      t->word[t->n++] = ins;
      return 1;
    }
    if ((ins & 0x01700000u) != 0x01100000u)
      return 0;
    const uint32_t off = ins & 0xfff;
    const uint32_t addr = (ins & 0x00800000u) ? pc + off : pc - off;
    return tr_ldr_lit(t, rd, *stage_at(addr - (uint32_t)(uintptr_t)g_m->load_virtbase));
  }
  if ((ins & 0x0c000000u) == 0) { /* data processing */
    const int imm = (ins >> 25) & 1;
    if (!imm && (ins & 0x90u) == 0x90u)
      return 0;
    const int op = (ins >> 21) & 15;
    const int uses_rn = op != 13 && op != 15;
    const int pc_in = (uses_rn && rn == 15) || (!imm && rm == 15);
    if (rd == 15 && op != 10 && op != 11 && op != 8 && op != 9)
      return 0;
    if (!pc_in) {
      t->word[t->n++] = ins;
      return 1;
    }
    if (op == 4 && !imm && rn == 15 && rm != 15 && rm != 12 && (ins & 0xff0u) == 0) {
      if (!tr_ldr_lit(t, 12, pc))
        return 0;
      t->word[t->n++] = (ins & ~0x000f0000u) | (12u << 16);
      return 1;
    }
    return 0;
  }
  return 0;
}

void *ssx_hook(uint32_t vaddr, uint32_t expect, void *dst, const char *what) {
  if (!g_m)
    return NULL;
  if (*stage_at(vaddr) != expect) {
    debugPrintf("[hook] %s at 0x%06x: 0x%08x, not 0x%08x: left alone\n", what, (unsigned)vaddr,
                (unsigned)*stage_at(vaddr), (unsigned)expect);
    return NULL;
  }
  Tramp t;
  memset(&t, 0, sizeof t);
  if (!tr_move(&t, vaddr) || !tr_move(&t, vaddr + 4)) {
    debugPrintf("[hook] %s at 0x%06x: its first instructions cannot be moved (0x%08x 0x%08x)\n", what,
                (unsigned)vaddr, (unsigned)stage_at(vaddr)[0], (unsigned)stage_at(vaddr)[1]);
    return NULL;
  }
  if (!tr_ldr_lit(&t, 15, live_at(vaddr + 8)))
    return NULL;
  const uint32_t size = (uint32_t)(t.n + t.nlit) * 4;
  if (g_pool + size > g_pool_end) {
    debugPrintf("[hook] %s at 0x%06x: no room for its trampoline\n", what, (unsigned)vaddr);
    return NULL;
  }
  for (uint32_t x = g_pool; x < g_pool + size; x += 4)
    if (*stage_at(x) != 0) {
      debugPrintf("[hook] %s: the trampolines' space is not empty (0x%06x)\n", what, (unsigned)x);
      return NULL;
    }
  const uint32_t base = g_pool;
  for (int i = 0; i < t.nlit; i++) {
    const uint32_t lit_at = base + (uint32_t)(t.n + i) * 4, ins_at = base + (uint32_t)t.lit[i].at * 4;
    const int32_t off = (int32_t)(lit_at - (ins_at + 8));
    if (off < 0)
      t.word[t.lit[i].at] = (t.word[t.lit[i].at] & ~0x00800000u) | (uint32_t)-off;
    else
      t.word[t.lit[i].at] |= (uint32_t)off;
    t.word[t.n + i] = t.lit[i].value;
  }
  memcpy(stage_at(base), t.word, size);
  g_pool = (base + size + 7) & ~7u;
  const uint32_t stub[2] = {0xe51ff004u, (uint32_t)(uintptr_t)dst};
  memcpy(stage_at(vaddr), stub, sizeof stub);
  return (void *)(uintptr_t)live_at(base);
}

/* Words of the trampolines' space for code of a caller's own (a veneer):
 * their vaddr, or 0. Fill them with ssx_poke_word. */
uint32_t ssx_hook_alloc(uint32_t words) {
  if (!g_m || g_pool + 4 * words > g_pool_end)
    return 0;
  for (uint32_t x = g_pool; x < g_pool + 4 * words; x += 4)
    if (*stage_at(x) != 0)
      return 0;
  const uint32_t at = g_pool;
  g_pool = (g_pool + 4 * words + 7) & ~7u;
  return at;
}

void ssx_hook_end(void) {
  if (g_m)
    debugPrintf("[hook] %u bytes of trampolines left\n", (unsigned)(g_pool_end - g_pool));
  g_m = NULL;
}
