/* ssx_boardpress.c -- nose and tail presses on the ground, as on the console
 * versions: hold Square (nose) or Circle (tail) while riding. With the
 * Switch's buttons by function those are Y and B (A and Y with swap_a_b).
 *
 * The presses are still in the Android build's code and data:
 * EA::Ant::Ssx::CarvingController::GatherGameStateParameters reads two
 * inputs (BoardPressNoseInput / BoardPressTailInput, or the Classic
 * variants) through evaluator objects of its asset, stores them in its
 * InputParameters (+217 nose, +218 tail), and right away decides from them,
 * the speed (BoardPressStartSpeed / StopSpeed) and the rider's state whether
 * the board presses (+216). On Android nothing drives those inputs.
 *
 * The two stores become jumps to stubs that OR the button in, store, redo the
 * instruction the jump took the place of, and come back. Only the player's
 * rider gets the buttons: the stub compares the game state table the
 * controller reads (r6) with the one the pad data was last written to
 * (Peripheral::ClassicGamepadAsset::UpdateGamepadData, which GameplayBase::
 * GetClassicGamepadData calls for controller 0 only). The whole press logic,
 * animation and scoring then run as the game has them. Found by the PS Vita
 * port (lukebrl's ssx-vita, source/ssx/boardpress.c); the offsets are
 * libgame.so 0.0.8430's. [controls] board_presses = false leaves it out. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_NOSE_STORE 0x7cbb28        /* strb r0, [r4, #217]; cmp r3, #0 */
#define OFF_TAIL_STORE 0x7cbb54        /* strb r0, [r4, #218]; ldr r3, [r5, #48] */
#define OFF_UpdateGamepadData 0x12ac208 /* ClassicGamepadAsset::UpdateGamepadData(Table&, float, float const*) const */

volatile uint32_t g_ssx_press_bits;      /* bit 0 nose (Square held), bit 1 tail (Circle held): ssx_input.c */
const void *volatile g_ssx_press_table;  /* the player's rider's game state table */
uint32_t g_ssx_press_nose_ret, g_ssx_press_tail_ret;

static void (*o_update_pad)(const void *asset, void *table, float dt, const float *data);
static void update_pad(const void *asset, void *table, float dt, const float *data) {
  g_ssx_press_table = table;
  o_update_pad(asset, table, dt, data);
}

/* r0: what the evaluator gave, r4: InputParameters, r6: the rider's table */
__attribute__((naked, target("arm"))) static void nose_stub(void) {
  __asm__ volatile("sub sp, sp, #4\n"
                   "push {r1, r2}\n"
                   "ldr r2, =g_ssx_press_table\n"
                   "ldr r2, [r2]\n"
                   "cmp r2, r6\n"
                   "bne 1f\n"
                   "ldr r1, =g_ssx_press_bits\n"
                   "ldr r1, [r1]\n"
                   "tst r1, #1\n"
                   "movne r0, #1\n"
                   "1: strb r0, [r4, #217]\n"
                   "ldr r2, =g_ssx_press_nose_ret\n"
                   "ldr r2, [r2]\n"
                   "str r2, [sp, #8]\n"
                   "pop {r1, r2}\n"
                   "cmp r3, #0\n" /* the instruction the jump took the place of */
                   "pop {pc}\n"
                   ".ltorg\n");
}

__attribute__((naked, target("arm"))) static void tail_stub(void) {
  __asm__ volatile("sub sp, sp, #4\n"
                   "push {r1, r2}\n"
                   "ldr r2, =g_ssx_press_table\n"
                   "ldr r2, [r2]\n"
                   "cmp r2, r6\n"
                   "bne 1f\n"
                   "ldr r1, =g_ssx_press_bits\n"
                   "ldr r1, [r1]\n"
                   "tst r1, #2\n"
                   "movne r0, #1\n"
                   "1: strb r0, [r4, #218]\n"
                   "ldr r2, =g_ssx_press_tail_ret\n"
                   "ldr r2, [r2]\n"
                   "str r2, [sp, #8]\n"
                   "pop {r1, r2}\n"
                   "ldr r3, [r5, #48]\n" /* the instruction the jump took the place of */
                   "pop {pc}\n"
                   ".ltorg\n");
}

void ssx_boardpress_patch(void) {
  if (!dcr_config()->presses)
    return;
  if (!ssx_hook_word_is(OFF_NOSE_STORE, 0xe5c400d9) || !ssx_hook_word_is(OFF_NOSE_STORE + 4, 0xe3530000) ||
      !ssx_hook_word_is(OFF_TAIL_STORE, 0xe5c400da) || !ssx_hook_word_is(OFF_TAIL_STORE + 4, 0xe5953030)) {
    debugPrintf("[press] CarvingController is not 0.0.8430's: no ground presses\n");
    return;
  }
  o_update_pad = ssx_hook(OFF_UpdateGamepadData, 0xe92d40f0, (void *)update_pad, "ClassicGamepadAsset::UpdateGamepadData");
  if (!o_update_pad)
    return;
  g_ssx_press_nose_ret = (uint32_t)ssx_hook_live(OFF_NOSE_STORE + 8);
  g_ssx_press_tail_ret = (uint32_t)ssx_hook_live(OFF_TAIL_STORE + 8);
  ssx_poke_word(OFF_NOSE_STORE, 0xe51ff004); /* ldr pc, [pc, #-4] */
  ssx_poke_word(OFF_NOSE_STORE + 4, (uint32_t)(uintptr_t)nose_stub);
  ssx_poke_word(OFF_TAIL_STORE, 0xe51ff004);
  ssx_poke_word(OFF_TAIL_STORE + 4, (uint32_t)(uintptr_t)tail_stub);
  debugPrintf("[press] ground presses: nose on Square, tail on Circle\n");
}
