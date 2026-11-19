/* dcr_config.c -- SSX's settings: config.ini's options, on the runtime's INI
 * engine (runtime/source/rt_cfg.c). The file header is the runtime's
 * default, "# SSX for Switch -- settings." (PORT_TITLE). MIT.
 */
#include <stdio.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_cfg.h"
#include "util.h"

static DcrConfig g_cfg = {
    .race_fps = 60, .menu_fps = 60, .race_sim_hz = 60, .race_sim_auto = 1, .presses = 1, .boost = 1, .movies = 1, .skip_mercury = 1,
    .trim_heaps = 1, .native_atomics = 1, .saltynx = 1, .shader_cache = 1, .handheld_gpu = 1,
};

const DcrConfig *dcr_config(void) { return &g_cfg; }

/* CHOICE rows give the index of the choice */
static int g_race_idx, g_menu_idx, g_sim_idx, g_hgpu_idx, g_detail_idx, g_render_idx;
static const int k_render_h[] = {0, 720, 900, 1080}; /* render_resolution's choices, in order */
static const int k_detail_pct[] = {100, 50, 75, 125, 150, 200, 300}; /* detail_distance's choices, in order */

static const CfgOpt k_opts[] = {
    {"display", "race_fps", "60",
     "Frames per second in races: 60 or 30. With race_sim_hz 30 the frames between\n"
     "# two simulation steps are interpolated by the game's own renderer.",
     CFG_CHOICE, "60,30", &g_race_idx, 0, 0, 0, 0},
    {"display", "race_sim_hz", "auto",
     "How often the race itself (riders, physics, animation) is updated: auto (60,\n"
     "# every frame a real step, and back to 30 for the rest of a race if the\n"
     "# console cannot keep up, so a race never plays in slow motion), 30 (the\n"
     "# game's own rate, as on phones: the frames between two steps are\n"
     "# interpolated) or 60 (always, even when the race slows down).",
     CFG_CHOICE, "auto,30,60", &g_sim_idx, 0, 0, 0, 0},
    {"display", "menu_fps", "60",
     "Frames per second in the menus and on the globe: 60 or 30 (the Android\n"
     "# game's own rate was 30).",
     CFG_CHOICE, "60,30", &g_menu_idx, 0, 0, 0, 0},
    CFG_ROW_RESOLUTION("auto",
                       "The window: auto (1080 when docked at start-up, 720 in handheld), 720 or\n"
                       "# 1080: the size the menus and the HUD are drawn at, and the 3D scene is\n"
                       "# scaled to (docked at 1080: 60 fps, the GPU ~40% busy). To dock during\n"
                       "# play and keep the full 1080 picture, use 1080 (the handheld screen then\n"
                       "# shows it scaled down)."),
    {"display", "follow_docking", "false",
     "Experimental: with resolution auto, switch the window between 1080 and 720\n"
     "# when the console is docked or undocked during play (the game is told its\n"
     "# screen changed size, as a phone would tell it). In testing the game\n"
     "# crashed once in 12 switches (an abort in its own code), so it is off: the\n"
     "# window keeps its start-up size until the next start.",
     CFG_BOOL, NULL, &g_cfg.follow_docking, 0, 0, 0, 0},
    {"display", "render_resolution", "1080",
     "The 3D scene's resolution: 1080, 900, 720 or auto (the window's). Above the\n"
     "# window's (1080 on the handheld's 720 screen) it is scaled down: smoother\n"
     "# edges for more GPU work (handheld at 1080: 60 fps, the GPU ~40% busy\n"
     "# instead of ~24%; 720 or auto saves battery).",
     CFG_CHOICE, "auto,720,900,1080", &g_render_idx, 0, 0, 0, 0},
    {"display", "detail_distance", "100",
     "How far the scenery keeps its detail and is drawn, in percent of the game's\n"
     "# own distances: 50, 75, 100 (as shipped), 125, 150, 200 or 300. Above 100\n"
     "# trees, objects and terrain switch to their simpler models and disappear\n"
     "# farther away (more to draw each frame); below 100 nearer.",
     CFG_CHOICE, "100,50,75,125,150,200,300", &g_detail_idx, 0, 0, 0, 0},
    {"display", "fps_counter", "false",
     "Show the frame rate top left (and in races the race's speed: below 100% it\n"
     "# plays in slow motion even when the frame rate looks fine).",
     CFG_BOOL, NULL, &g_cfg.fps_counter, 0, 0, 0, 0},
    {"display", "saltynx", "true",
     "With SaltyNX installed, give Status Monitor Overlay the frame rate (FPS,\n"
     "# frame times and graph), as SaltyNX's NX-FPS does for retail games. Without\n"
     "# SaltyNX this does nothing.",
     CFG_BOOL, NULL, &g_cfg.saltynx, 0, 0, 0, 0},
    CFG_ROW_SWAP_AB("Swap A and B (the face buttons as an Xbox pad's: A below, B right).", &g_cfg.swap_ab),
    {"controls", "board_presses", "true",
     "Nose and tail presses while riding on the ground: hold Y (nose) or B (tail),\n"
     "# as on the console versions. The Android game kept the feature but had no\n"
     "# control for it.",
     CFG_BOOL, NULL, &g_cfg.presses, 0, 0, 0, 0},
    {"game", "movies", "true",
     "Play the game's movies (intro, mountain fly-ins). Off: they are skipped.",
     CFG_BOOL, NULL, &g_cfg.movies, 0, 0, 0, 0},
    {"game", "skip_dlc_screen", "true",
     "Answer the game's content download screen with \"already downloaded\": the\n"
     "# APK holds all of SSX's content, and EA's servers are gone.",
     CFG_BOOL, NULL, &g_cfg.skip_mercury, 0, 0, 0, 0},
    CFG_ROW_BOOST("CPU at its boost clock while the game starts and while it loads (frames\n"
                  "# over 50 ms), normal otherwise.",
                  &g_cfg.boost),
    {"performance", "trim_heaps", "true",
     "Size EA's memory heaps from what the game uses (as the PS Vita port measured,\n"
     "# with a margin): ~200 MB more free memory. Off: the Android file's sizes.",
     CFG_BOOL, NULL, &g_cfg.trim_heaps, 0, 0, 0, 0},
    {"performance", "handheld_gpu", "460",
     "Handheld mode's GPU clock: 460 (460.8 MHz, the performance setting Nintendo\n"
     "# added for handheld games; the CPU stays at 1020 MHz) or system (the system's\n"
     "# own, 307.2 MHz). Docked play always runs at the system's clocks.",
     CFG_CHOICE, "system,460", &g_hgpu_idx, 0, 0, 0, 0},
    {"performance", "threads_first", "off",
     "The game's frame threads one priority step above its other threads: off,\n"
     "# render (the render thread) or render_sim (and the race simulation's), so a\n"
     "# busy job thread never holds a frame up for a time slice.",
     CFG_CHOICE, "off,render,render_sim", &g_cfg.threads_first, 0, 0, 0, 0},
    {"performance", "glthread", "false",
     "Mesa's glthread: the game's GL calls are recorded on its threads and run by\n"
     "# a worker thread of the driver. Experimental.",
     CFG_BOOL, NULL, &g_cfg.glthread, 0, 0, 0, 0},
    {"performance", "shader_cache", "true",
     "Keep the compiled shaders on the SD card (shadercache/ in the game folder), so\n"
     "# later starts skip the shader compiler: a shorter start-up and fewer hitches\n"
     "# while a course loads. If a start with the cache ends in its first minute, the\n"
     "# next start turns it off by itself (shadercache/off; delete that file to try\n"
     "# again). The folder may be deleted at any time.",
     CFG_BOOL, NULL, &g_cfg.shader_cache, 0, 0, 0, 0},
    {"performance", "gl_finish", "frame",
     "The game's glFinish once a frame, which makes the CPU wait until the GPU has\n"
     "# drawn everything, so the two never work at the same time: frame (the CPU\n"
     "# waits only for the frame before the last: the GPU draws one frame while the\n"
     "# CPU prepares the next), flush (no wait at all) or keep (the game's own).",
     CFG_CHOICE, "frame,flush,keep", &g_cfg.gl_finish, 0, 0, 0, 0},
    {"performance", "native_atomics", "true",
     "EA's 64-bit atomic operations on the CPU's own instructions (LDREXD/STREXD)\n"
     "# instead of a mutex each (the Android build's ARMv5 fallback).",
     CFG_BOOL, NULL, &g_cfg.native_atomics, 0, 0, 0, 0},
    {"performance", "menu_full_gc", "false",
     "The menus' full Lua garbage collection after every move (the game's own\n"
     "# behaviour; a hitch each time). Off: Lua's incremental collector only.",
     CFG_BOOL, NULL, &g_cfg.menu_full_gc, 0, 0, 0, 0},
    CFG_ROW_GL_SELFTEST(&g_cfg.gl_selftest),
    CFG_ROW_BOOT_LOG("Show the start-up log on screen at every launch.", &g_cfg.boot_log),
    CFG_ROW_LOG_JNI("Write every call the game makes to Java into debug.log (slow).", &g_cfg.log_jni),
    {"debug", "fp_runfast", "false",
     "Experiment: every game thread with flush-to-zero and default-NaN floating point\n"
     "# (what the CPU's NEON unit always does). For emulator tests; leave off.",
     CFG_BOOL, NULL, NULL, 0, 0, 0, 0},
    {"debug", "cpuinfo", "switch",
     "The CPU the game reads in /proc/cpuinfo: switch (the console's), armv7 (a 2012\n"
     "# phone), no_neon (ARMv7 without NEON) or empty. For tests; leave switch.",
     CFG_CHOICE, "switch,armv7,no_neon,empty", &g_cfg.cpuinfo, 0, 0, 0, 0},
    {"debug", "skip_ik", "auto",
     "HumanIK, which fits the riders' limbs to the board: auto (skipped under an\n"
     "# emulator, where it makes the riders' bones NaN and the riders invisible;\n"
     "# kept on the console), none (always kept), solvers (its solvers skipped)\n"
     "# or all (always skipped: the riders as animated, limbs not fitted). If the\n"
     "# riders are invisible on the console, set all.",
     CFG_CHOICE, "auto,none,solvers,all", &g_cfg.skip_ik, 0, 0, 0, 0},
    {"debug", "live_log", "false",
     "Write the newest log lines to live.log every 10 s, a file that can be read\n"
     "# (FTP) while the game runs; debug.log can only be read after it closes.",
     CFG_BOOL, NULL, &g_cfg.live_log, 0, 0, 0, 0},
    {"debug", "profile", "false",
     "Sample where the game's busy threads spend their time (every ~3 ms) into\n"
     "# prof.txt, written every 10 s. For performance work; leave off.",
     CFG_BOOL, NULL, &g_cfg.profile, 0, 0, 0, 0},
    {"debug", "resize_test", "0",
     "With resolution auto and follow_docking: switch the window between 720 and\n"
     "# 1080 every this many seconds, as docking and undocking would (0: only when\n"
     "# the console really is docked or undocked). For tests; leave 0.",
     CFG_INT, NULL, &g_cfg.resize_test, 0, 600, 0, 0},
    {"debug", "window_crop_top", "false",
     "A test of the window's crop (the part of its buffers on screen); leave off.",
     CFG_BOOL, NULL, &g_cfg.crop_top, 0, 0, 0, 0},
    {"debug", "math_selftest", "false",
     "At start-up, check the FPU sinf, cosf, acosf and asinf the game gets against\n"
     "# the C library's soft-float ones (a million arguments each, ~10 s) and write\n"
     "# the result to debug.log. For tests; leave off.",
     CFG_BOOL, NULL, &g_cfg.math_selftest, 0, 0, 0, 0},
};

static void apply(void) {
  g_cfg.race_fps = g_race_idx == 1 ? 30 : 60;
  g_cfg.menu_fps = g_menu_idx == 1 ? 30 : 60;
  g_cfg.race_sim_hz = g_sim_idx == 1 ? 30 : 60;
  g_cfg.race_sim_auto = g_sim_idx == 0;
  g_cfg.handheld_gpu = g_hgpu_idx == 1;
  g_cfg.detail_distance =
      k_detail_pct[g_detail_idx >= 0 && g_detail_idx < (int)(sizeof k_detail_pct / sizeof k_detail_pct[0]) ? g_detail_idx : 0];
  g_cfg.render_height = k_render_h[g_render_idx >= 0 && g_render_idx < 4 ? g_render_idx : 0];
  debugPrintf("[config] race %d fps (simulation %s), menus %d fps, presses %s, swap_ab %s, movies %s\n",
              g_cfg.race_fps, g_cfg.race_sim_auto ? "auto: 60, 30 if behind" : g_cfg.race_sim_hz == 60 ? "60 Hz" : "30 Hz",
              g_cfg.menu_fps, g_cfg.presses ? "on" : "off", g_cfg.swap_ab ? "on" : "off", g_cfg.movies ? "on" : "off");
  static const char *const k_first[] = {"off", "render", "render_sim"};
  debugPrintf("[config] handheld GPU %s, threads_first %s, glthread %s, gl_finish %s, profile %s\n",
              g_cfg.handheld_gpu ? "460.8 MHz" : "system", k_first[g_cfg.threads_first > 2 ? 0 : g_cfg.threads_first],
              g_cfg.glthread ? "on" : "off", g_cfg.gl_finish == 2 ? "keep" : g_cfg.gl_finish == 1 ? "flush" : "frame", g_cfg.profile ? "on" : "off");
  void ssx_livelog_init(void); /* ssx_livelog.c */
  ssx_livelog_init();
  /* follow_docking: the window's buffers at 1080 in either mode, so its size can
   * change while the game runs (ssx_window.c, runtime/source/rt_window.c) */
  void dcr_window_crop_at_top(int on);           /* rt_window.c: a test */
  void dcr_window_set_buffer_size(int w, int h); /* rt_window.c */
  dcr_window_crop_at_top(g_cfg.crop_top);
  if (g_cfg.follow_docking && rt_config()->res_auto)
    dcr_window_set_buffer_size(1920, 1080);
}

/* Format 2 (1080 measured at 60 fps in handheld and docked): a format-1 file
 * still holding the old defaults gets the new ones. */
static const CfgMigrate k_migrate[] = {
    {"display", "resolution", "720", "auto", 0},
    {"display", "render_resolution", "auto", "1080", 0},
};

static const CfgTable k_table = {
    .opts = k_opts,
    .nopts = sizeof k_opts / sizeof k_opts[0],
    .migrate = k_migrate,
    .nmigrate = sizeof k_migrate / sizeof k_migrate[0],
    .version = 2,
    .apply = apply,
};

void dcr_config_load(void) { rt_config_load(&k_table); }
