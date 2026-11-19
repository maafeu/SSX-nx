/* ssx_window.c -- [display] follow_docking (experimental, off): resolution
 * auto follows the console while the game runs, 1080 docked, 720 in handheld.
 *
 * The game learns its screen size from MainThread.NativeOnSurfaceChanged(w,
 * h, format), which posts a Blast message (EA::Blast::MessageCoord2i, type
 * 5) to its own message queue; an Android phone sends it again when its
 * surface changes size. With follow_docking the window's buffers are made at
 * 1080 (runtime/source/rt_window.c), so a change of mode is: the window's crop and
 * the size the game is told (ssx_egl_size, which the game's eglQuerySurface
 * and its Display calls read) changed between two frames, and the message
 * sent again, on this thread, as the Android GL thread would.
 *
 * [debug] resize_test = N switches between the two sizes every N seconds
 * whatever the mode, to test the switch without docking: in handheld the vi
 * layer scales either crop to the screen, so the picture should look the
 * same, only sharper or softer.
 *
 * Console, 2026-11-18 (rz1, handheld, resize_test = 20): the game takes the
 * new size as it should, menus, HUD and 3D scene laid out again full-screen
 * at either size (also under Ryujinx, docked); but at the 12th switch,
 * mid-race, it aborted ("pure virtual method called", from its own code):
 * a phone hardly ever changes a landscape game's surface size while it
 * runs, and the game is not safe against it at any moment. So it is off by
 * default, and a window that must stay sharp when docked during play is
 * resolution = 1080. MIT.
 */
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "rt_window.h"
#include "ssx.h"
#include "util.h"

static int g_docked = -1;  /* the mode the window was last sized for */
static int g_test_big;     /* resize_test: 1080 now */
static u64 g_test_next;    /* resize_test: the tick of the next switch */
static unsigned g_changes;

/* NativeOnSurfaceChanged(env, thiz, w, h, format) */
typedef void (*SurfaceChangedFn)(void *, void *, jint, jint, jint);

/* Each loop, before NativeOnDrawFrame, on the game's main thread. */
void ssx_window_poll(void) {
  if (!dcr_config()->follow_docking || !rt_config()->res_auto)
    return;
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  const int test = dcr_config()->resize_test;
  if (test > 0 && g_ssx_frames_presented > 0) {
    u64 now = armGetSystemTick();
    if (!g_test_next)
      g_test_next = now + armNsToTicks((u64)test * 1000000000ull);
    else if (now >= g_test_next) {
      g_test_big = !g_test_big;
      g_test_next = now + armNsToTicks((u64)test * 1000000000ull);
    }
    docked = g_test_big ? !docked : docked;
  }
  if (docked == g_docked)
    return;
  g_docked = docked;
  const int h = docked ? 1080 : 720, w = h * 16 / 9;
  if (!dcr_window_resize(w, h))
    return; /* the size the game was told at start-up (or one too big for the buffers) */
  g_changes++;
  int bw = 0, bh = 0;
  dcr_window_buffer_size(&bw, &bh);
  debugPrintf("[window] %s: the game draws %dx%d now (the window's buffers %dx%d, the crop that part); "
              "NativeOnSurfaceChanged sent (change %u, frame %u)\n",
              test > 0 ? "resize_test" : docked ? "docked" : "handheld", w, h, bw, bh, g_changes,
              g_ssx_frames_presented);
  SurfaceChangedFn f = (SurfaceChangedFn)ssx_native("Java_com_ea_blast_MainThread_NativeOnSurfaceChanged");
  if (f)
    f(g_jni_env, g_ssx_main_thread, w, h, 0);
}
