/* dcr_config.h -- SSX's settings (config.ini in the game folder), on the
 * runtime's INI engine (runtime/source/rt_cfg.c). MIT. */
#ifndef DCR_CONFIG_H
#define DCR_CONFIG_H

typedef struct DcrConfig {
  int race_fps;       /* 60 or 30: the render loop in races (ssx_patch.c) */
  int menu_fps;       /* 60 or 30: the render loop elsewhere */
  int race_sim_hz;    /* 30 or 60: the race simulation (ssx_patch2.c) */
  int race_sim_auto;  /* race_sim_hz auto: 60, and 30 for the rest of a race that cannot keep up */
  int swap_ab;        /* A and B swapped (the Xbox layout) */
  int presses;        /* nose / tail presses on Y / X while riding (ssx_patch.c) */
  int boost;          /* CPU boost while loading */
  int gl_selftest;
  int boot_log;
  int log_jni;
  int movies;         /* the game's movies played (ssx_video.c) */
  int skip_mercury;   /* the DLC screen answered "downloaded" */
  int trim_heaps;     /* EA's heaps sized from use (ssx_assetpatch.c) */
  int menu_full_gc;   /* the menus' full Lua collection kept (ssx_assetpatch.c) */
  int cpuinfo;        /* /proc/cpuinfo: 0 switch, 1 armv7, 2 no_neon, 3 empty (ssx_cpuinfo.c) */
  int fps_counter;    /* the frame-rate counter, top left (ssx_overlay.c) */
  int skip_ik;        /* 0 auto (all under an emulator), 1 none, 2 HumanIK's solvers, 3 all of it (ssx_ikdebug.c) */
  int live_log;       /* the newest log lines in live.log every 10 s (ssx_livelog.c) */
  int handheld_gpu;   /* handheld: the GPU at 460.8 MHz (ssx_clocks.c) */
  int profile;        /* the busy threads sampled into prof.txt (ssx_prof.c) */
  int threads_first;  /* 0 off, 1 FGGameRender, 2 and FGGameSim one priority step up (ssx_sched.c) */
  int glthread;       /* Mesa's glthread for the game's context (ssx_egl.c) */
  int native_atomics; /* EA::Thread's 64-bit atomics on LDREXD/STREXD (ssx_atomic64.c) */
  int detail_distance; /* percent: LOD metrics scaled (ssx_detail.c); 100 = the game's own */
  int render_height;  /* the 3D scene's height (16:9), 0 = the window's (ssx_render.c) */
  int shader_cache;   /* Mesa's compiled shaders kept on the SD card (ssx_shadercache.c) */
  int gl_finish;      /* the game's per-frame glFinish: 0 one frame in flight, 1 glFlush, 2 kept (ssx_gl.c) */
  int saltynx;        /* the frame rate in SaltyNX's shared memory for Status Monitor (ssx_saltynx.c) */
  int follow_docking; /* resolution auto: the window follows docking while the game runs (ssx_window.c; experimental) */
  int resize_test;    /* seconds: with follow_docking, the window switched 720 <-> 1080 that often (ssx_window.c) */
  int crop_top;       /* the window's crop at its buffers' top-left (a test; ssx_window.c) */
  int math_selftest;  /* the FPU sinf/cosf/acosf/asinf checked against newlib's at start-up (ssx_vfpmathf.c) */
} DcrConfig;

void dcr_config_load(void);
const DcrConfig *dcr_config(void);

#endif
