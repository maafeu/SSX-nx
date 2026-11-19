/* ssx_shadercache.c -- [performance] shader_cache: Mesa's shader cache on the SD card.
 *
 * Mesa compiles each GLSL program when the game first asks for it: the GLSL
 * front end, the linker and the TGSI translation, on whatever thread holds
 * the GL context. SSX compiles about 500 at start-up and more as each course
 * streams in, mid-race (console run c3: 24 at a time on the asset streamer,
 * the render thread waiting for the one context). mesa32 now has a disk cache
 * for the Switch (src/util/disk_cache_switch.c): one pack file of compiled
 * programs, read back the next time the same program is asked for, so the
 * second and later starts of each course skip the compiler.
 *
 * Here: where the pack lives (<game folder>/shadercache/mesa_shader_cache.pack),
 * and which Mesa wrote it (SSX_MESA_ID, the Makefile's hash of the Mesa
 * libraries this program links: a new Mesa starts a new pack), both set before
 * the game first initializes EGL. Every 10 s the cache's counters go to the
 * log. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <switch.h>

#include "dcr_config.h"
#include "dcr_path.h"
#include "util.h"

#ifndef SSX_MESA_ID
#define SSX_MESA_ID ""
#endif

/* mesa32's src/util/disk_cache_switch.c (absent from an older Mesa: weak) */
extern int disk_cache_switch_global_stats(unsigned *hits, unsigned *misses, unsigned *puts, unsigned *objects,
                                          unsigned *keys, unsigned *bytes) __attribute__((weak));

static int g_on;
static char g_marker[512]; /* shadercache/starting while this start is young */

/* The safety net. On the console, 1 of the first 4 starts that read the cache
 * was followed by the whole console going down a few frames after the first
 * picture (2026-11-17; not explained, and a crash without the cache had been
 * seen too). So a start with the cache leaves shadercache/starting until it has
 * shown a minute of frames or left cleanly. A start that finds it there knows
 * the last one died young: the cache is set aside (the pack renamed .suspect),
 * shadercache/off is written, and the cache stays off until that file is
 * deleted. At most one such crash, whatever its cause. */
static int exists(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return 0;
  fclose(f);
  return 1;
}

static void disable(const char *why) {
  setenv("MESA_SHADER_CACHE_DISABLE", "true", 1);
  debugPrintf("[shadercache] off: %s\n", why);
}

void ssx_shadercache_setup(void) {
  if (!dcr_config()->shader_cache) {
    disable("[performance] shader_cache = false");
    return;
  }
  if (!disk_cache_switch_global_stats) {
    debugPrintf("[shadercache] this Mesa has no shader cache for the Switch\n");
    return;
  }
  char dir[512], off[560], pack[560];
  snprintf(dir, sizeof dir, "%s/shadercache", dcr_game_root());
  snprintf(off, sizeof off, "%s/off", dir);
  snprintf(pack, sizeof pack, "%s/mesa_shader_cache.pack", dir);
  snprintf(g_marker, sizeof g_marker, "%s/starting", dir);
  if (exists(off)) {
    disable("shadercache/off is there (a start with the cache ended in its first minute); delete that file to use "
            "the cache again");
    return;
  }
  if (exists(g_marker)) {
    char suspect[580];
    snprintf(suspect, sizeof suspect, "%s.suspect", pack);
    remove(suspect);
    rename(pack, suspect);
    remove(g_marker);
    FILE *f = fopen(off, "w");
    if (f) {
      fputs("The last start that used the shader cache ended within its first minute, without leaving cleanly\n"
            "(a crash, or the console going down). The cache is off until this file is deleted; the cache it used\n"
            "is kept as mesa_shader_cache.pack.suspect.\n",
            f);
      fclose(f);
    }
    disable("the last start with the cache ended in its first minute: the cache is set aside (.suspect) and off "
            "until shadercache/off is deleted");
    return;
  }
  mkdir(dir, 0777);
  FILE *m = fopen(g_marker, "w");
  if (m) {
    fputs("a start with the shader cache is in its first minute\n", m);
    fclose(m);
  }
  setenv("MESA_SHADER_CACHE_DIR", dir, 1);
  if (SSX_MESA_ID[0])
    setenv("MESA_SHADER_CACHE_BUILD_ID", SSX_MESA_ID, 1);
  g_on = 1;
  debugPrintf("[shadercache] compiled shaders kept in %s (Mesa %s)\n", dir, SSX_MESA_ID[0] ? SSX_MESA_ID : "?");
}

/* This start is past its first minute, or leaving cleanly: no longer suspect. */
static void settled(const char *why) {
  if (!g_marker[0])
    return;
  remove(g_marker);
  g_marker[0] = 0;
  debugPrintf("[shadercache] %s: the start marker is removed\n", why);
}

void ssx_shadercache_stop(void) { settled("leaving"); }

/* 1 when it wrote a line (the counters changed) */
static int report(unsigned frame) {
  unsigned hits, misses, puts, objects, keys, bytes;
  if (!disk_cache_switch_global_stats || !disk_cache_switch_global_stats(&hits, &misses, &puts, &objects, &keys, &bytes))
    return 0;
  static unsigned h0 = ~0u, m0, p0;
  if (hits == h0 && misses == m0 && puts == p0)
    return 0;
  char at[32] = "";
  if (frame)
    snprintf(at, sizeof at, " (frame %u)", frame);
  debugPrintf("[shadercache] %u programs found in the cache, %u compiled (%u stored this run); the pack holds %u "
              "programs and %u shader keys, %u KB%s\n",
              hits, misses, puts, objects, keys, bytes >> 10, at);
  h0 = hits, m0 = misses, p0 = puts;
  return 1;
}

void ssx_shadercache_report(void) { report(0); }

/* Before every swap: for the first minute, the counters every 10 frames and
 * the log written out when they change, so a log cut short (the console went
 * down at the second start with a full cache, 2026-11-17) shows how far the
 * cache had got. */
void ssx_shadercache_frame(void) {
  static unsigned n;
  if (!g_on || ++n > 3600 || n % 10)
    return;
  if (n == 3600)
    settled("a minute of frames");
  if (report(n))
    log_flush_ring();
}
