/* ssx_audio.c -- android.media.AudioTrack, on the console's audio output.
 *
 * EA's audio core (EA::Audio::Core) mixes on its own thread
 * (Dac::SubmitSamplesThreadFunc) and hands each mix to the AudioTrack that
 * AndroidEAAudioCore.Init was given, with write(short[], off, n): 16-bit
 * stereo at 48 kHz (what ssx_boot.c tells Init). On a phone write() blocks
 * while the track's buffer is full, which paces the mixer; here the samples
 * are cut into RT_AUDOUT_FRAMES-frame blocks for the runtime's audout
 * (rt_audout.c: three queued), whose submit blocks the same way. Nothing is
 * resampled: audout runs at 48 kHz. MIT.
 */
#include <string.h>
#include <switch.h>

#include "rt_audout.h"
#include "ssx.h"
#include "util.h"

static int16_t g_block[RT_AUDOUT_FRAMES * 2];
static unsigned g_fill;          /* shorts in g_block */
static volatile int g_playing, g_paused, g_open;
static volatile unsigned long g_writes, g_dropped;
static Mutex g_lock;

void ssx_movie_mix(int16_t *block, unsigned frames); /* ssx_movie.c */

int ssx_audio_init(void) {
  if (g_open)
    return 0;
  if (rt_audout_open() != 0) {
    debugPrintf("[audio] audout could not be opened: no sound\n");
    return -1;
  }
  g_open = 1;
  debugPrintf("[audio] audout at %u Hz, %d frames a block\n", rt_audout_rate(), RT_AUDOUT_FRAMES);
  return 0;
}

void ssx_audio_play(int on) {
  g_playing = on;
  debugPrintf("[audio] AudioTrack.%s\n", on ? "play" : "stop");
}

void ssx_audio_pause(int paused) { g_paused = paused; }

unsigned long ssx_audio_writes(void) { return g_writes; }

jint ssx_audio_write(const int16_t *pcm, jint shorts) {
  if (!g_open || shorts <= 0)
    return shorts;
  g_writes++;
  mutexLock(&g_lock);
  jint left = shorts;
  while (left > 0) {
    unsigned room = RT_AUDOUT_FRAMES * 2 - g_fill;
    unsigned take = (unsigned)left < room ? (unsigned)left : room;
    memcpy(g_block + g_fill, pcm, take * sizeof *pcm);
    g_fill += take, pcm += take, left -= (jint)take;
    if (g_fill == RT_AUDOUT_FRAMES * 2) {
      if (g_paused)
        memset(g_block, 0, sizeof g_block); /* HOME: silence, still paced */
      else
        ssx_movie_mix(g_block, RT_AUDOUT_FRAMES); /* a playing movie's sound (ssx_movie.c) */
      if (rt_audout_submit(g_block) != 0)
        g_dropped++;
      g_fill = 0;
    }
  }
  mutexUnlock(&g_lock);
  return shorts;
}
