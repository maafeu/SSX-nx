/* ssx_input.c -- the Switch's controllers, as the gamepad SSX was made for.
 *
 * The Android build plays with one controller: MainActivityGenerated looks
 * for the input device named "PLAYSTATION" (mTargetControllerName: the
 * Xperia Play and PlayStation-certified pads), and hands its keys and sticks
 * to the natives NativeOnControllerKeyDown/Up(keycode) and
 * NativeOnControllerMotion(lx, ly, rx, ry). The keycodes are Android's
 * KEYCODE_BUTTON_* as that pad reports them: Cross arrives as BUTTON_X,
 * Circle as BUTTON_Y, Square as BUTTON_A, Triangle as BUTTON_B.
 *
 * Switch to PlayStation, by function: A (confirm) = Cross, B (back) =
 * Circle, Y = Square, X = Triangle; [controls] swap_a_b = by position (B =
 * Cross). L/R = L1/R1, ZL/ZR = L2/R2, the sticks' clicks = L3/R3, + = Start,
 * - = Select. Any controller (or the handheld) is the player's. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <switch.h>

#include "dcr_config.h"
#include "dcr_path.h"
#include "rt_pad.h"
#include "ssx.h"
#include "util.h"

enum {
  AK_DPAD_UP = 19, AK_DPAD_DOWN = 20, AK_DPAD_LEFT = 21, AK_DPAD_RIGHT = 22,
  AK_BUTTON_A = 96, AK_BUTTON_B = 97, AK_BUTTON_X = 99, AK_BUTTON_Y = 100,
  AK_BUTTON_L1 = 102, AK_BUTTON_R1 = 103, AK_BUTTON_L2 = 104, AK_BUTTON_R2 = 105,
  AK_BUTTON_THUMBL = 106, AK_BUTTON_THUMBR = 107, AK_BUTTON_START = 108, AK_BUTTON_SELECT = 109,
};

typedef void (*fn_key)(void *env, void *cls, jint code);
typedef void (*fn_motion)(void *env, void *cls, jfloat lx, jfloat ly, jfloat rx, jfloat ry);
typedef void (*fn_connect)(void *env, void *cls, jboolean on, void *name);

static fn_key g_down, g_up;
static fn_motion g_motion;
static PadState g_pad;
static u64 g_held;
static float g_last[4];
static int g_connected;
static u64 script_held(float st[4]);

void ssx_input_init(void) {
  rt_pad_setup(RT_PAD_MAX_PLAYERS, 1);
  padInitializeAny(&g_pad);
  g_down = (fn_key)ssx_native("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerKeyDown");
  g_up = (fn_key)ssx_native("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerKeyUp");
  g_motion = (fn_motion)ssx_native("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerMotion");
  if (!g_down || !g_up || !g_motion)
    debugPrintf("[input] MISSING the controller natives\n");
}

/* MainActivityGenerated.onResume -> ParseInputDevices: the pad is there. */
void ssx_input_connect(void) {
  fn_connect c = (fn_connect)ssx_native("Java_com_ea_ssx_MainActivityGenerated_NativeOnControllerConnectState");
  if (c && !g_connected) {
    c(g_jni_env, jni_class("com/ea/ssx/MainActivityGenerated")->obj, 1, jni_str("PLAYSTATION"));
    g_connected = 1;
    debugPrintf("[input] controller \"PLAYSTATION\" connected\n");
  }
}

static int key_of(u64 bit, int swap) {
  switch (bit) {
  case HidNpadButton_A: return swap ? AK_BUTTON_Y : AK_BUTTON_X;  /* Cross / Circle */
  case HidNpadButton_B: return swap ? AK_BUTTON_X : AK_BUTTON_Y;  /* Circle / Cross */
  case HidNpadButton_Y: return AK_BUTTON_A;                       /* Square */
  case HidNpadButton_X: return AK_BUTTON_B;                       /* Triangle */
  case HidNpadButton_L: return AK_BUTTON_L1;
  case HidNpadButton_R: return AK_BUTTON_R1;
  case HidNpadButton_ZL: return AK_BUTTON_L2;
  case HidNpadButton_ZR: return AK_BUTTON_R2;
  case HidNpadButton_StickL: return AK_BUTTON_THUMBL;
  case HidNpadButton_StickR: return AK_BUTTON_THUMBR;
  case HidNpadButton_Plus: return AK_BUTTON_START;
  case HidNpadButton_Minus: return AK_BUTTON_SELECT;
  case HidNpadButton_Up: return AK_DPAD_UP;
  case HidNpadButton_Down: return AK_DPAD_DOWN;
  case HidNpadButton_Left: return AK_DPAD_LEFT;
  case HidNpadButton_Right: return AK_DPAD_RIGHT;
  }
  return 0;
}

static const u64 k_buttons[] = {
    HidNpadButton_A, HidNpadButton_B, HidNpadButton_X, HidNpadButton_Y, HidNpadButton_L, HidNpadButton_R,
    HidNpadButton_ZL, HidNpadButton_ZR, HidNpadButton_StickL, HidNpadButton_StickR, HidNpadButton_Plus,
    HidNpadButton_Minus, HidNpadButton_Up, HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Right,
};

/* The buttons held now (ssx_patch2.c's board presses read them). */
u64 ssx_input_held(void) { return g_held; }

/* ---------------------------------------------------------------- input script
 * For unattended test runs (an emulator window gets no controller): the game
 * folder's input_script.txt, one press per line, "<seconds> <button> <ms>",
 * seconds counted from the first picture; buttons A B X Y L R ZL ZR PLUS
 * MINUS UP DOWN LEFT RIGHT LSTICK RSTICK, or LX+ LX- LY+ LY- (the left stick
 * pushed fully). '#' starts a comment. The presses add to the controller's. */
#define SCRIPT_MAX 256
static struct {
  u32 at_ms, len_ms;
  u64 button;
  int axis; /* 1..4: LX+ LX- LY+ LY- */
} g_script[SCRIPT_MAX];
static int g_nscript = -1;
static u64 g_t0;

static void script_load(void) {
  g_nscript = 0;
  char path[512];
  snprintf(path, sizeof path, "%s/input_script.txt", dcr_game_root());
  FILE *f = fopen(path, "r");
  if (!f)
    return;
  static const struct {
    const char *name;
    u64 bit;
    int axis;
  } names[] = {
      {"A", HidNpadButton_A, 0},       {"B", HidNpadButton_B, 0},         {"X", HidNpadButton_X, 0},
      {"Y", HidNpadButton_Y, 0},       {"L", HidNpadButton_L, 0},         {"R", HidNpadButton_R, 0},
      {"ZL", HidNpadButton_ZL, 0},     {"ZR", HidNpadButton_ZR, 0},       {"PLUS", HidNpadButton_Plus, 0},
      {"MINUS", HidNpadButton_Minus, 0}, {"UP", HidNpadButton_Up, 0},     {"DOWN", HidNpadButton_Down, 0},
      {"LEFT", HidNpadButton_Left, 0}, {"RIGHT", HidNpadButton_Right, 0}, {"LSTICK", HidNpadButton_StickL, 0},
      {"RSTICK", HidNpadButton_StickR, 0}, {"LX+", 0, 1},                 {"LX-", 0, 2},
      {"LY+", 0, 3},                   {"LY-", 0, 4},
  };
  char line[128];
  while (fgets(line, sizeof line, f) && g_nscript < SCRIPT_MAX) {
    char *hash = strchr(line, '#');
    if (hash)
      *hash = 0;
    float sec;
    char name[16];
    unsigned ms = 150;
    if (sscanf(line, "%f %15s %u", &sec, name, &ms) < 2)
      continue;
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
      if (!strcasecmp(name, names[i].name)) {
        g_script[g_nscript].at_ms = (u32)(sec * 1000.0f);
        g_script[g_nscript].len_ms = ms;
        g_script[g_nscript].button = names[i].bit;
        g_script[g_nscript].axis = names[i].axis;
        g_nscript++;
        break;
      }
  }
  fclose(f);
  debugPrintf("[input] input_script.txt: %d presses\n", g_nscript);
}

static u64 script_held(float st[4]) {
  if (g_nscript < 0)
    script_load();
  if (!g_nscript || !g_ssx_frames_presented)
    return 0;
  if (!g_t0)
    g_t0 = armGetSystemTick();
  u32 now = (u32)(armTicksToNs(armGetSystemTick() - g_t0) / 1000000ull);
  u64 held = 0;
  for (int i = 0; i < g_nscript; i++) {
    if (now < g_script[i].at_ms || now >= g_script[i].at_ms + g_script[i].len_ms)
      continue;
    held |= g_script[i].button;
    switch (g_script[i].axis) {
    case 1: st[0] = 1.0f; break;
    case 2: st[0] = -1.0f; break;
    case 3: st[1] = 1.0f; break;
    case 4: st[1] = -1.0f; break;
    }
  }
  return held;
}

void ssx_input_frame(void) {
  padUpdate(&g_pad);
  float st[4] = {0};
  u64 held = rt_pad_read(&g_pad, st);
  held |= script_held(st);
  static int was_ready = -1;
  const int ready = ssx_input_ready();
  if (ready != was_ready) {
    debugPrintf("[input] the game %s input (frame %u)\n", ready ? "takes" : "does not take", g_ssx_frames_presented);
    was_ready = ready;
  }
  if (!ready) {
    g_held = 0;
    return;
  }
  const int swap = dcr_config()->swap_ab;
  void *cls = jni_class("com/ea/ssx/MainActivityGenerated")->obj;
  u64 changed = held ^ g_held;
  for (unsigned i = 0; i < sizeof k_buttons / sizeof k_buttons[0]; i++) {
    const u64 b = k_buttons[i];
    if (!(changed & b))
      continue;
    int code = key_of(b, swap);
    if (!code)
      continue;
    static int logged;
    if (logged++ < 24)
      debugPrintf("[input] key %d %s\n", code, held & b ? "down" : "up");
    if (held & b)
      g_down(g_jni_env, cls, code);
    else
      g_up(g_jni_env, cls, code);
  }
  g_held = held;
  /* the ground presses (ssx_boardpress.c): Square = Y, Circle = B (A with swap_a_b) */
  {
    extern volatile uint32_t g_ssx_press_bits;
    const u64 circle = swap ? HidNpadButton_A : HidNpadButton_B;
    g_ssx_press_bits = ((held & HidNpadButton_Y) ? 1u : 0u) | ((held & circle) ? 2u : 0u);
  }
  /* sticks: -1..1, up positive (as Android's AXIS_Y negated by the Java) */
  if (memcmp(st, g_last, sizeof st)) {
    memcpy(g_last, st, sizeof st);
    g_motion(g_jni_env, NULL, st[0], st[1], st[2], st[3]);
  }
}
