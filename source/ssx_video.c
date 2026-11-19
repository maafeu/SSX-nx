/* ssx_video.c -- com.ea.VideoPlayer.PlayerAndroid: the game's movies.
 *
 * The Java player (MediaPlayer on a SurfaceView over the game) plays an MP4
 * the game names (init(path, x, y, w, h, uiMode)), and tells the game when it
 * ends through OnCompletionNativeImpl(mInstanceID). Here ssx_movie.c plays it
 * (FFmpeg, drawn over the game's frame); one movie plays at a time, as in the
 * game. Without FFmpeg in the build, with [game] movies = false, or for a
 * movie that cannot be opened, it ends after a second and 30 frames (an end
 * 30 ms after play() crashed the game's render thread, emu8/emu12: on a
 * phone it comes seconds later). MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx.h"
#include "util.h"

typedef void (*fn_completion)(void *env, void *self, jint instance);

/* ssx_movie.c (FFmpeg); stubs when the build has no FFmpeg */
int ssx_movie_start(const char *path);
void ssx_movie_stop(void);
int ssx_movie_finished(void);

#define MAX_PLAYERS 8
static struct {
  JObj *obj;
  jint instance;
  int playing;
  int complete_pending;
  int real;        /* ssx_movie.c plays it */
  u64 complete_at; /* stub: not before */
  unsigned frames; /* polls since play */
  char path[256];
} g_pl[MAX_PLAYERS];
static jint g_next_instance = 1;
static Mutex g_lock;

static int slot_of(JObj *p, int make) {
  for (int i = 0; i < MAX_PLAYERS; i++)
    if (g_pl[i].obj == p)
      return i;
  if (!make)
    return -1;
  for (int i = 0; i < MAX_PLAYERS; i++)
    if (!g_pl[i].obj) {
      g_pl[i].obj = jni_retain(p);
      return i;
    }
  return -1;
}

void ssx_video_init(JObj *p, const char *path, jint x, jint y, jint w, jint h, jint ui) {
  mutexLock(&g_lock);
  int i = slot_of(p, 1);
  if (i >= 0) {
    g_pl[i].instance = g_next_instance++;
    g_pl[i].playing = 0;
    g_pl[i].complete_pending = 0;
    g_pl[i].real = 0;
    snprintf(g_pl[i].path, sizeof g_pl[i].path, "%s", path ? path : "");
    jvalue v;
    v.j = 0;
    v.i = g_pl[i].instance;
    jni_set_field(p, "mInstanceID", v, 0);
  }
  mutexUnlock(&g_lock);
  debugPrintf("[movie] init(%s, %d,%d %dx%d, ui %d)\n", path, x, y, w, h, ui);
}

void ssx_video_play(JObj *p, jint pos) {
  mutexLock(&g_lock);
  int i = slot_of(p, 1);
  if (i >= 0) {
    for (int k = 0; k < MAX_PLAYERS; k++)
      if (k != i && g_pl[k].real)
        g_pl[k].real = 0, g_pl[k].playing = 0, g_pl[k].complete_pending = 0;
    g_pl[i].playing = 1;
    g_pl[i].complete_pending = 1;
    g_pl[i].frames = 0;
    g_pl[i].complete_at = armGetSystemTick() + armNsToTicks(1000000000ull);
    g_pl[i].real = dcr_config()->movies && ssx_movie_start(g_pl[i].path) == 0;
  }
  mutexUnlock(&g_lock);
}

void ssx_video_stop(JObj *p) {
  mutexLock(&g_lock);
  int i = slot_of(p, 0);
  if (i >= 0) {
    if (g_pl[i].real)
      ssx_movie_stop();
    g_pl[i].real = 0;
    g_pl[i].playing = 0;
    g_pl[i].complete_pending = 0;
  }
  mutexUnlock(&g_lock);
}

jint ssx_video_pause(JObj *p) { return 0; }

int ssx_video_is_playing(JObj *p) {
  mutexLock(&g_lock);
  int i = slot_of(p, 0);
  int r = i >= 0 && g_pl[i].playing;
  mutexUnlock(&g_lock);
  return r;
}

void ssx_video_destroy(JObj *p) {
  mutexLock(&g_lock);
  int i = slot_of(p, 0);
  if (i >= 0) {
    if (g_pl[i].real)
      ssx_movie_stop();
    jni_release(g_pl[i].obj);
    memset(&g_pl[i], 0, sizeof g_pl[i]);
  }
  mutexUnlock(&g_lock);
}

JObj *ssx_video_new(void) { return jni_new("com/ea/VideoPlayer/PlayerAndroid"); }

/* The frame loop: movies that ended tell the game (on its own thread, as the
 * Java's completion listener ran on the UI thread). */
void ssx_video_poll(void) {
  static fn_completion done;
  if (!done)
    done = (fn_completion)ssx_native("Java_com_ea_VideoPlayer_PlayerAndroid_OnCompletionNativeImpl");
  for (int i = 0; i < MAX_PLAYERS; i++) {
    mutexLock(&g_lock);
    JObj *o = NULL;
    if (g_pl[i].complete_pending) {
      ++g_pl[i].frames;
      const int over = g_pl[i].real ? ssx_movie_finished()
                                    : g_pl[i].frames >= 30 && armGetSystemTick() >= g_pl[i].complete_at;
      if (over)
        o = g_pl[i].obj;
    }
    jint inst = g_pl[i].instance;
    if (o) {
      if (g_pl[i].real)
        ssx_movie_stop();
      g_pl[i].real = 0;
      g_pl[i].complete_pending = 0;
      g_pl[i].playing = 0;
    }
    mutexUnlock(&g_lock);
    if (o && done) {
      debugPrintf("[movie] instance %d: completed\n", (int)inst);
      done(g_jni_env, o, inst);
    }
  }
}
