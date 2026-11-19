/* ssx_spinwait.c -- the game's busy-wait loops sleep between checks.
 *
 * A few EA loops wait for another thread by re-reading a value until it
 * changes, never yielding; EA::Ant::SceneOpNode::RunFunc's, for one:
 *
 *     loop: ldr r2, [r1, #68]; ldr r3, [r4, #68]; cmp r2, r3; bne loop
 *
 * Android time-slices threads, so the thread they wait for runs anyway. The
 * PS Vita does not time-slice threads of equal priority, and with spinners on
 * every core the thread they waited for never ran again: the game froze a few
 * minutes into a race, at a different time each run. Horizon schedules the
 * same way (round-robin only at its preemption priorities), so the same
 * freeze is waiting for the Switch.
 *
 * The backward branch of each loop goes, with its own condition, to a veneer
 * that sleeps 100 us and jumps back to the loop's start (which re-reads the
 * value and sets the flags again); the exit path is untouched. The sites and
 * the method are the PS Vita port's (source/ssx/spinwait.c, lukebrl, MIT);
 * the veneers live in the hooks' trampoline space (ssx_hook.c). MIT.
 */
#include <stdint.h>
#include <switch.h>

#include "ssx_hook.h"
#include "util.h"

static const struct {
  uint32_t branch, expect;
  const char *where;
} k_loops[] = {
    {0x562774, 0xbafffffc, "EA::Audio::Core::BufferReference::ClearPending"},
    {0x67a734, 0x1afffffc, "EA::Ant::Anim::CacheLock::CacheLock (constprop)"},
    {0x681f54, 0x1afffffc, "EA::Ant::Anim::CacheLock::CacheLock"},
    {0x6a140c, 0x1afffffb, "EA::Ant::SceneOpNode::AddJobEvent"},
    {0x6a1658, 0x1afffffb, "EA::Ant::SceneOpNode::ScheduleTaskList"},
    {0x6a17d0, 0x1afffffb, "EA::Ant::SceneOpNode::RunFunc"},
    {0xf37600, 0x0afffffc, "EA::Ant::Interaction::InteractionManager::GetInteractionScene"},
};

static volatile uint32_t g_sleeps;

static void spin_sleep(void) {
  __atomic_add_fetch(&g_sleeps, 1, __ATOMIC_RELAXED);
  svcSleepThread(100000);
}

/* Saves the caller-saved registers, calls spin_sleep on an 8-byte aligned
 * stack, restores them, and jumps to the loop's start. */
static const uint32_t k_veneer[] = {
    0xe92d500f, /* push {r0-r3, r12, lr} */
    0xe1a0100d, /* mov  r1, sp */
    0xe3c10007, /* bic  r0, r1, #7 */
    0xe1a0d000, /* mov  sp, r0 */
    0xe92d0006, /* push {r1, r2} */
    0xe59fc010, /* ldr  r12, [pc, #16]  (spin_sleep) */
    0xe12fff3c, /* blx  r12 */
    0xe8bd0006, /* pop  {r1, r2} */
    0xe1a0d001, /* mov  sp, r1 */
    0xe8bd500f, /* pop  {r0-r3, r12, lr} */
    0xe59ff000, /* ldr  pc, [pc]        (the loop's start) */
    0,          /* spin_sleep */
    0,          /* the loop's start (live address) */
};
#define VENEER_WORDS (sizeof k_veneer / sizeof k_veneer[0])

/* ssx_patch2.c's ssx_patch_more, inside the hook window */
void ssx_spinwait_patch(void) {
  int patched = 0;
  for (unsigned i = 0; i < sizeof k_loops / sizeof k_loops[0]; i++) {
    const uint32_t b = k_loops[i].branch, ins = k_loops[i].expect;
    if (!ssx_hook_word_is(b, ins)) {
      debugPrintf("[spin] %s: not the expected branch, left alone\n", k_loops[i].where);
      continue;
    }
    const int32_t imm = (int32_t)(ins << 8) >> 8;
    const uint32_t loop = b + 8 + (uint32_t)(imm * 4);
    const uint32_t v = ssx_hook_alloc(VENEER_WORDS);
    if (!v) {
      debugPrintf("[spin] %s: no room for its veneer\n", k_loops[i].where);
      continue;
    }
    for (unsigned w = 0; w < VENEER_WORDS - 2; w++)
      ssx_poke_word(v + 4 * w, k_veneer[w]);
    ssx_poke_word(v + 4 * (VENEER_WORDS - 2), (uint32_t)(uintptr_t)spin_sleep);
    ssx_poke_word(v + 4 * (VENEER_WORDS - 1), (uint32_t)ssx_hook_live(loop));
    /* b<cond> veneer, the branch's own condition */
    const int32_t off = (int32_t)(v - (b + 8)) >> 2;
    ssx_poke_word(b, (ins & 0xf0000000u) | 0x0a000000u | ((uint32_t)off & 0x00ffffffu));
    patched++;
  }
  debugPrintf("[spin] %d busy-wait loops now sleep between checks\n", patched);
}

void ssx_spinwait_report(void) {
  static uint32_t last;
  if (g_sleeps != last) {
    last = g_sleeps;
    debugPrintf("[spin] %u sleeps in busy-wait loops so far\n", (unsigned)g_sleeps);
  }
}
