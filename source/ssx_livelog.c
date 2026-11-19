/* ssx_livelog.c -- [debug] live_log: the newest log lines in live.log, readable
 * while the game runs.
 *
 * debug.log is one file kept open for writing (runtime/source/util.c), and
 * Horizon's file system does not let another process open a file that is
 * open for writing: over sys-ftpd the console showed it as size 0 with no
 * permissions until the game closed (hardware run c2, 2026-11-16). So a
 * console test could only read the frame rate and the long frames after
 * quitting. Here a log tap keeps the newest LIVE_BYTES of lines in RAM, and
 * every 10 s (after the [fps] report, ssx_boot.c) they are written to
 * <game folder>/live.log by open, write, close: a few tens of KB every 10 s,
 * nothing like the continuous writes that froze the Drive Ahead port. Off
 * unless config.ini asks. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dcr_path.h"
#include "util.h"

#define LIVE_BYTES (24 * 1024)

static Mutex g_lock;
static char g_ring[LIVE_BYTES];
static size_t g_head; /* total bytes ever put */
static int g_on;

static void tap(const char *line) {
  size_t n = strlen(line);
  if (n > LIVE_BYTES)
    line += n - LIVE_BYTES, n = LIVE_BYTES;
  mutexLock(&g_lock);
  for (size_t i = 0; i < n; i++)
    g_ring[(g_head + i) % LIVE_BYTES] = line[i];
  g_head += n;
  mutexUnlock(&g_lock);
}

void ssx_livelog_init(void) {
  if (!dcr_config()->live_log || g_on)
    return;
  g_on = 1;
  dcr_log_tap = tap;
  debugPrintf("[live] the newest log lines go to live.log every 10 s\n");
}

void ssx_livelog_write(void) {
  static char copy[LIVE_BYTES];
  if (!g_on)
    return;
  mutexLock(&g_lock);
  size_t have = g_head < LIVE_BYTES ? g_head : LIVE_BYTES;
  size_t start = g_head - have;
  for (size_t i = 0; i < have; i++)
    copy[i] = g_ring[(start + i) % LIVE_BYTES];
  mutexUnlock(&g_lock);
  /* start at a line */
  size_t skip = 0;
  if (start) {
    while (skip < have && copy[skip] != '\n')
      skip++;
    if (skip < have)
      skip++;
  }
  char path[512];
  snprintf(path, sizeof path, "%s/live.log", dcr_game_root());
  FILE *f = fopen(path, "w");
  if (!f)
    return;
  fwrite(copy + skip, 1, have - skip, f);
  fclose(f);
}
