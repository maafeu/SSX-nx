/* ssx_ikdebug.c -- [debug] skip_ik: HumanIK skipped, under the emulator.
 *
 * FOUND (2026-11-16): under Ryujinx the riders' bones come out NaN in
 * HumanIK's pose conversion (with "solvers" skipped they still did, emu32;
 * with "all" skipped not one palette in 6,855 had a NaN and the rider is on
 * the board, emu35). The PS Vita runs the same code on ARM hardware with
 * riders shown, so it is the emulator's arithmetic: the default, auto, skips
 * all of HumanIK under an emulator only (the riders as animated, limbs not
 * fitted to the board) and keeps it on the console.
 *
 * Each rider's pose goes through EA::Ant's HumanIK every frame, as commands
 * of EA::Ant's command queue: PoseToHikPlugin (the animated pose into
 * HumanIK's state), the solver plugins (EffectorSetBegin*, Solve*: feet on
 * the board, hands, hips, head, roll bones), then HikToPosePlugin (the state
 * back into the pose). All of them are ARM code taking one
 * Command::Arguments and returning nothing; "skipped" makes them return at
 * once (bx lr):
 *   none     as the game is (default)
 *   solvers  the 13 solver plugins skipped: the pose goes into HumanIK and
 *            back unsolved
 *   all      PoseToHik and HikToPose skipped too: the animated pose as it is
 * If the riders' NaN bones go away with "solvers", a solver makes them; with
 * "all" only, the conversion; with neither, the animation upstream. MIT.
 */
#include <stdint.h>

#include "dcr_config.h"
#include "ssx_hook.h"
#include "util.h"

#define BX_LR 0xe12fff1eu
#define PUSH_R4_LR 0xe92d4010u        /* push {r4, lr} */
#define PUSH_R4_R11_LR 0xe92d4ff0u    /* push {r4-r11, lr} */

static const uint32_t k_solvers[] = {
    0x124c2d0, /* EffectorSetBeginLowLODPlugin::Evaluate */
    0x124c344, /* EffectorSetBeginPlugin::Evaluate */
    0x124c538, /* SolveRollExtractionPlugin::Evaluate */
    0x124c5b0, /* SolveHipsTranslationPlugin::Evaluate */
    0x124c628, /* SolveHeadAndNeckPlugin::Evaluate */
    0x124c6a0, /* SolveFloorContactApproxPlugin::Evaluate */
    0x124c718, /* SolveFloorContactPlugin::Evaluate */
    0x124c790, /* SolveFingersAndToesPlugin::Evaluate */
    0x124c808, /* SolveEffectorSetLowLODPlugin::Evaluate */
    0x124c880, /* SolveBodyPullLowLODPlugin::Evaluate */
    0x124c8f8, /* SolveBodyPullPlugin::Evaluate */
    0x124c970, /* SolveArmsAndLegsLowLODPlugin::Evaluate */
    0x124c9e8, /* SolveArmsAndLegsPlugin::Evaluate */
};
static const uint32_t k_convert[] = {
    0x125eca8, /* HikToPosePlugin::Evaluate */
    0x125f724, /* PoseToHikPlugin::Evaluate */
};

/* ssx_patch2.c's ssx_patch_more, inside the hook window */
void ssx_ikdebug_patch(void) {
  /* the setting: 0 auto, 1 none, 2 solvers, 3 all -> here 0 none, 1 solvers, 2 all */
  const int cfg = dcr_config()->skip_ik;
  const int mode = cfg == 0 ? (dcr_is_emulator() ? 2 : 0) : cfg - 1;
  if (!mode)
    return;
  int n = 0;
  for (unsigned i = 0; i < sizeof k_solvers / sizeof k_solvers[0]; i++)
    n += ssx_patch_word(k_solvers[i], PUSH_R4_LR, BX_LR);
  if (mode == 2)
    for (unsigned i = 0; i < sizeof k_convert / sizeof k_convert[0]; i++)
      n += ssx_patch_word(k_convert[i], PUSH_R4_R11_LR, BX_LR);
  debugPrintf("[ik] HumanIK %s skipped (%d functions)%s\n", mode == 2 ? "entirely" : "solvers", n,
              cfg == 0 ? ": an emulator (skip_ik auto)" : "");
}
