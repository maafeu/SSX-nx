/* ssx_boot.c -- the app: what com.ea.ssx.Main (MainActivityGenerated,
 * com.ea.ssx.MainActivity, com.ea.blast.MainActivity) and com.ea.blast.
 * MainThread did, and the frame loop.
 *
 * Android's order, and what happens here:
 *   <clinit>        System.loadLibrary("game"): the constructors (ssx_main.c),
 *                   then JNI_OnLoad
 *   onCreate        rwfilesystem.StartupNativeImpl(assets, files, sdcard),
 *                   EAThread.Init, EAIO.StartupNativeImpl(assets, "/data",
 *                   files, sdcard), StorageDirectory.StartupNativeImpl,
 *                   MainActivity.NativeOnCreate, AndroidEAAudioCore.Init
 *                   (track, frames, 2 channels, 48000 Hz, -1),
 *                   PlayerAndroid.StartupNativeImpl, InitGameApplication
 *   onResume        NativeOnResume(no focus), NativeOnPauseEvent, the pad
 *                   connected ("PLAYSTATION")
 *   the GL thread   MainThread.NativeOnSurfaceCreated(0), the context reset
 *                   event, NativeOnSurfaceChanged(w, h, 0)
 *   focus           NativeOnResumeEvent, NativeOnWindowFocusChanged(true)
 *   each frame      MainThread.NativeOnDrawFrame (the game renders and swaps
 *                   through EGL10: ssx_egl.c)
 * (as the PS Vita port's lifecycle.c worked out). One thread plays both
 * the UI thread and MainThread here, as there: the game's own threads
 * (simulation, render jobs, audio, streaming) run beside it. MIT.
 */
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "bionic_io.h"
#include "dcr_apkcache.h"
#include "dcr_boost.h"
#include "dcr_config.h"
#include "error.h"
#include "gl_layer.h"
#include "rt_applet.h"
#include "ssx.h"
#include "util.h"
#include "watchdog.h"

void ssx_input_connect(void); /* ssx_input.c */

static volatile int g_exit;
void ssx_request_exit(void) { g_exit = 1; }
uint64_t dcr_boot_frames(void) { return g_ssx_frames_presented; }
int port_watchdog_hold(void) { return g_exit; }
static uint32_t watch_frames(void) { return g_ssx_frames_presented; }
static uint32_t watch_audio(void) { return (uint32_t)ssx_audio_writes(); }

static void *need(const char *sym) {
  void *p = ssx_native(sym);
  if (!p)
    fatal_error("libgame.so lacks %s:\nis the APK SSX 0.0.8430?", sym);
  return p;
}

#define ENV g_jni_env
#define CLS(n) (jni_class(n)->obj)
#define JS(s) jni_str(s)
#define NAT(ret, name, ...) ((ret(*)(void *, void *, ##__VA_ARGS__))need(name))

/* Input before the front end exists crashed the game on the Vita
 * (TouchController::OnNIControllerMotion on frame 1, then the classic
 * input reading FEDataStore at the boot logo): nothing reaches it until a
 * picture is up and FEDataStore's instance exists. */
#define OFF_FEDataStoreInstance 0x17cdb7c
int ssx_input_ready(void) {
  return g_ssx_frames_presented > 0 && *(void *volatile *)ssx_text(OFF_FEDataStoreInstance) != NULL;
}

static void on_create(void) {
  void *act = g_ssx_activity, *assets = g_ssx_asset_manager;
  debugPrintf("[boot] onCreate\n");
  NAT(void, "Java_com_ea_rwfilesystem_rwfilesystem_StartupNativeImpl", void *, void *, void *)
  (ENV, CLS("com/ea/rwfilesystem/rwfilesystem"), assets, JS(SSX_ANDROID_FILES_DIR), JS(SSX_ANDROID_SDCARD));
  NAT(void, "Java_com_ea_EAThread_EAThread_Init")(ENV, CLS("com/ea/EAThread/EAThread"));
  NAT(void, "Java_com_ea_EAIO_EAIO_StartupNativeImpl", void *, void *, void *, void *)
  (ENV, CLS("com/ea/EAIO/EAIO"), assets, JS("/data"), JS(SSX_ANDROID_FILES_DIR), JS(SSX_ANDROID_SDCARD));
  NAT(void, "Java_com_ea_EAMIO_StorageDirectory_StartupNativeImpl")(ENV, CLS("com/ea/EAMIO/StorageDirectory"));
  debugPrintf("[boot] MainActivity.NativeOnCreate\n");
  NAT(void, "Java_com_ea_blast_MainActivity_NativeOnCreate")(ENV, act);
  debugPrintf("[boot] AndroidEAAudioCore.Init\n");
  NAT(void, "Java_com_ea_EAAudioCore_AndroidEAAudioCore_Init", void *, jint, jint, jint, jint)
  (ENV, CLS("com/ea/EAAudioCore/AndroidEAAudioCore"), g_ssx_audio_track, SSX_AUDIO_FRAMES, 2, SSX_AUDIO_RATE, -1);
  NAT(void, "Java_com_ea_VideoPlayer_PlayerAndroid_StartupNativeImpl")(ENV, CLS("com/ea/VideoPlayer/PlayerAndroid"));
  debugPrintf("[boot] MainActivity.InitGameApplication\n");
  NAT(void, "Java_com_ea_ssx_MainActivity_InitGameApplication")(ENV, CLS("com/ea/ssx/MainActivity"));
  debugPrintf("[boot] onCreate done\n");
  log_flush_ring();
}

static void on_resume(void) {
  void *act = g_ssx_activity, *ssx_act = CLS("com/ea/ssx/MainActivity");
  debugPrintf("[boot] onResume\n");
  NAT(void, "Java_com_ea_blast_MainActivity_NativeOnResume", jboolean)(ENV, act, 0);
  NAT(void, "Java_com_ea_ssx_MainActivity_NativeOnPauseEvent")(ENV, ssx_act);
  ssx_input_connect();
  debugPrintf("[boot] MainThread.NativeOnSurfaceCreated\n");
  NAT(void, "Java_com_ea_blast_MainThread_NativeOnSurfaceCreated", jint)(ENV, g_ssx_main_thread, 0);
  NAT(void, "Java_com_ea_ssx_MainActivity_NativeOnContextResetEvent", jint)(ENV, ssx_act, 0);
  int w, h;
  ssx_egl_size(&w, &h);
  debugPrintf("[boot] MainThread.NativeOnSurfaceChanged(%d, %d)\n", w, h);
  NAT(void, "Java_com_ea_blast_MainThread_NativeOnSurfaceChanged", jint, jint, jint)(ENV, g_ssx_main_thread, w, h, 0);
  debugPrintf("[boot] window focus gained\n");
  NAT(void, "Java_com_ea_ssx_MainActivity_NativeOnResumeEvent")(ENV, ssx_act);
  NAT(void, "Java_com_ea_blast_MainActivity_NativeOnWindowFocusChanged", jboolean)(ENV, act, 1);
  debugPrintf("[boot] onResume done: the frames begin\n");
  log_flush_ring();
}

/* HOME / sleep (rt_applet.c): the game's own pause, as onPause and
 * onWindowFocusChanged(false) would; back: its resume. */
static int g_up;
void port_focus_lost(void) {
  if (!g_up)
    return;
  void (*f)(void *, void *) = (void (*)(void *, void *))ssx_native("Java_com_ea_ssx_MainActivity_PauseAudio");
  if (f)
    f(ENV, CLS("com/ea/ssx/MainActivity"));
  ssx_audio_pause(1);
}
void port_focus_gained(void) {
  if (!g_up)
    return;
  ssx_audio_pause(0);
  void (*f)(void *, void *) = (void (*)(void *, void *))ssx_native("Java_com_ea_ssx_MainActivity_ResumeAudio");
  if (f)
    f(ENV, CLS("com/ea/ssx/MainActivity"));
}

static void report(void) {
  static u64 last_tick;
  static unsigned last_frames;
  u64 tick = armGetSystemTick();
  unsigned frames = g_ssx_frames_presented;
  char fps[24] = "";
  if (last_tick)
    snprintf(fps, sizeof fps, " (%.1f fps)", (double)(frames - last_frames) * 1e9 / (double)armTicksToNs(tick - last_tick));
  last_tick = tick, last_frames = frames;
  /* malloc's arena (rt_heap_taken), not mallinfo(): mallinfo() walks the heap
   * with the malloc lock held, ~28 ms here, and the render and simulation
   * threads allocate every frame */
  debugPrintf("[boot] %u frames%s, %lu audio writes, %d Java objects; heap %u MB taken\n", frames, fps,
              ssx_audio_writes(), jni_live_objects(), (unsigned)(rt_heap_taken() >> 20));
  ssx_assets_report();
  ssx_patch_report();
  dcr_boost_report();
  dcr_apkcache_report();
  dcr_io_write_report();
  {
    void ssx_clocks_report(void);   /* ssx_clocks.c */
    void ssx_sched_report(void);    /* ssx_sched.c */
    void ssx_bakesort_report(void); /* ssx_bakesort.c */
    void ssx_gldefer_report(void);  /* ssx_gldefer.c */
    void ssx_bomem_report(void);    /* ssx_bomem.c */
    ssx_clocks_report();
    ssx_sched_report();
    ssx_bakesort_report();
    ssx_gldefer_report();
    ssx_bomem_report();
  }
  {
    void ssx_prof_write(void);    /* ssx_prof.c */
    void ssx_livelog_write(void); /* ssx_livelog.c: last, so this report is in it */
    ssx_prof_write();
    ssx_livelog_write();
  }
}

void ssx_boot_run(void) {
  {
    void ssx_clocks_start(void); /* ssx_clocks.c */
    ssx_clocks_start();
  }
  {
    void ssx_prof_start(void); /* ssx_prof.c */
    ssx_prof_start();
    void ssx_shadercache_setup(void); /* ssx_shadercache.c: before EGL */
    ssx_shadercache_setup();
    void ssx_saltynx_start(void); /* ssx_saltynx.c */
    ssx_saltynx_start();
  }
  ssx_java_init();
  {
    void ssx_fpmode_main(void); /* ssx_fpmode.c */
    ssx_fpmode_main();
  }
  {
    uint32_t ssx_fpscr(void); /* ssx_patch.c */
    debugPrintf("[boot] main thread FPSCR 0x%08x\n", (unsigned)ssx_fpscr());
  }
  dcr_watchdog_start();
  rt_watchdog_add_counter("frames", watch_frames);
  rt_watchdog_add_counter("audio writes", watch_audio);

  jint (*onload)(void *, void *) = (jint(*)(void *, void *))ssx_native("JNI_OnLoad");
  if (onload)
    debugPrintf("[boot] JNI_OnLoad -> 0x%x\n", (unsigned)onload(g_jni_vm, NULL));
  ssx_audio_init();
  ssx_input_init();
  if (dcr_config()->math_selftest) {
    void ssx_vfpmathf_selftest(void); /* ssx_vfpmathf.c */
    ssx_vfpmathf_selftest();
    log_flush_ring();
  }
  on_create();
  on_resume();
  g_up = 1;

  void (*draw)(void *, void *) = (void (*)(void *, void *))need("Java_com_ea_blast_MainThread_NativeOnDrawFrame");
  u64 last_report = armGetSystemTick();
  int first = 0;
  unsigned long loops = 0;
  while (!g_exit && !rt_exit_requested() && appletMainLoop()) {
    rt_applet_poll();
    if (!rt_focused()) {
      svcSleepThread(50000000ll);
      continue;
    }
    ssx_input_frame();
    void ssx_window_poll(void); /* ssx_window.c: auto follows docking */
    ssx_window_poll();
    unsigned before = g_ssx_frames_presented;
    draw(ENV, g_ssx_main_thread);
    ssx_video_poll();
    loops++;
    if (loops <= 3)
      debugPrintf("[boot] NativeOnDrawFrame #%lu done (%u presented)\n", loops, g_ssx_frames_presented);
    if (g_ssx_frames_presented == before)
      svcSleepThread(1000000ll); /* nothing drawn: the game's next tick is not due */
    dcr_boost_poll();
    if (!first && g_ssx_frames_presented > 0) {
      first = 1;
      dcr_boost_launch_end();
      debugPrintf("[boot] first frame presented\n");
    }
    u64 now = armGetSystemTick();
    /* under the emulator: a picture every 15 s from the first one (capture-NNN.bmp) */
    static u64 last_capture;
    if (first && dcr_is_emulator() && armTicksToNs(now - last_capture) >= 15000000000ull) {
      last_capture = now;
      dcr_gl_request_capture();
    }
    if (armTicksToNs(now - last_report) >= 10000000000ull) {
      last_report = now;
      report();
      log_flush_ring();
    }
  }
  {
    void ssx_saltynx_stop(void); /* ssx_saltynx.c */
    ssx_saltynx_stop();
    void ssx_shadercache_stop(void); /* ssx_shadercache.c */
    ssx_shadercache_stop();
  }
  debugPrintf("[boot] leaving\n");
  rt_applet_stop();
  log_flush_ring();
}
