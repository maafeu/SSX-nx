/* ssx.h -- what SSX's port files share.
 *
 * SSX for Android (com.ea.ssx 0.0.8430, armeabi) is one native library,
 * libgame.so (40 MB, EA's Blast / RenderWare / EAGL engines, softfp, not
 * stripped), plus GNU's libstdc++ (libgnustl_shared.so). The Java side is a
 * thin activity (com.ea.ssx.MainActivity, com.ea.blast.*): it is played by
 * the port's C (ssx_java.c, ssx_boot.c). Offsets into libgame.so are for that
 * one build (its md5 is checked at load: SSX_LIBGAME_MD5). MIT.
 */
#ifndef SSX_H
#define SSX_H

#include <stddef.h>
#include <stdint.h>

#include "jni.h"
#include "so_util.h"

#define SSX_LIB_GAME   "libgame.so"
#define SSX_LIB_STL    "libgnustl_shared.so"
#define SSX_VERSION    "0.0.8430"
#define SSX_VERSION_CODE 8430

/* The Android paths the game is given for its writable folders. The
 * runtime's dcr_path.c maps /data/data/<package>/... to <root>/data/... and
 * the shared storage (/sdcard...) to <root>/sdcard/... */
#define SSX_ANDROID_APP_DIR   "/data/data/com.ea.ssx"
#define SSX_ANDROID_FILES_DIR SSX_ANDROID_APP_DIR "/files"
#define SSX_ANDROID_SDCARD    "/sdcard"

/* ------------------------------------------------------------ ssx_loader.c */
extern so_module g_mod_game, g_mod_stl;
int ssx_load_modules(void);
void ssx_run_constructors(void);
void *ssx_native(const char *symbol);           /* an export of libgame.so, or NULL */
static inline uintptr_t ssx_text(uint32_t off) { /* libgame.so's vaddr -> its address now */
  extern uintptr_t g_ssx_base;
  return g_ssx_base + off;
}

/* ------------------------------------------------------------ ssx_assets.c */
/* The APK's assets/, read in place: stored entries straight from the APK,
 * deflated ones inflated. */
int ssx_assets_init(const char *apk);
typedef struct SsxAsset SsxAsset;
SsxAsset *ssx_asset_open(const char *name);    /* "common/config/memory.cfg"; NULL if absent */
int ssx_asset_read(SsxAsset *a, void *buf, size_t n);
long ssx_asset_seek(SsxAsset *a, long off, int whence);
long ssx_asset_size(const SsxAsset *a);
long ssx_asset_tell(const SsxAsset *a);
void ssx_asset_close(SsxAsset *a);
int ssx_asset_exists(const char *name);         /* 1 file, 2 folder, 0 none */
long ssx_asset_size_of(const char *name);       /* -1 if absent */
/* The names directly under folder `dir` ("" = the root): cb once each. */
int ssx_asset_list(const char *dir, void (*cb)(const char *name, void *ctx), void *ctx);
void ssx_assets_report(void);

/* ------------------------------------------------------------ ssx_java.c */
void ssx_java_init(void);
extern JObj *g_ssx_activity;      /* com.ea.ssx.Main, the activity */
extern JObj *g_ssx_asset_manager; /* android.content.res.AssetManager */
extern JObj *g_ssx_audio_track;   /* android.media.AudioTrack, given to AndroidEAAudioCore.Init */
extern JObj *g_ssx_main_thread;   /* com.ea.blast.MainThread */

/* ------------------------------------------------------------ ssx_egl.c */
extern volatile unsigned g_ssx_frames_presented; /* the game's eglSwapBuffers so far */
void ssx_egl_size(int *w, int *h);              /* the window the game renders to */

/* ------------------------------------------------------------ ssx_audio.c */
#define SSX_AUDIO_RATE  48000 /* AudioTrack.getNativeOutputSampleRate() */
#define SSX_AUDIO_FRAMES 1024 /* the game's AudioTrack buffer, in frames */
int ssx_audio_init(void);
jint ssx_audio_write(const int16_t *pcm, jint shorts); /* AudioTrack.write: blocks like it */
void ssx_audio_play(int on);
unsigned long ssx_audio_writes(void);
void ssx_audio_pause(int paused);

/* ------------------------------------------------------------ ssx_video.c */
JObj *ssx_video_new(void);
void ssx_video_init(JObj *p, const char *path, jint a, jint b, jint c, jint d, jint e);
void ssx_video_play(JObj *p, jint loop);
void ssx_video_stop(JObj *p);
jint ssx_video_pause(JObj *p);
int ssx_video_is_playing(JObj *p);
void ssx_video_destroy(JObj *p);
void ssx_video_poll(void);

/* ------------------------------------------------------------ ssx_input.c */
void ssx_input_init(void);
void ssx_input_frame(void);

/* ------------------------------------------------------------ ssx_boot.c */
void ssx_boot_run(void);
void ssx_request_exit(void);
int ssx_input_ready(void);

/* ------------------------------------------------------------ ssx_patch.c */
void ssx_patch_game(so_module *m);   /* before the code is sealed */
void ssx_patch_report(void);

#endif /* SSX_H */
