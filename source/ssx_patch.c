/* ssx_patch.c -- libgame.so 0.0.8430 where the Switch needs it to behave
 * differently from a phone. Offsets are that build's (its symbol table has
 * every function's name; most are local, so they are hooked by address).
 * Which functions matter, and why, was found by the PS Vita port (lukebrl's
 * ssx-vita, source/patch.c and source/ssx/); each hook says what it found.
 * MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dcr_path.h"
#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

/* ---------------------------------------------------------------- Mercury
 * MERCURY::MercuryManager::Init(EA::Blast::ISystem*, _jobject*). The front
 * end's content download screen (FE::MercuryDownloadProvider::OnEnter) waits
 * for data store items 456 or 457 ("package info retrieved") and starts a
 * download unless 453 ("package downloaded") is set too.
 * MercuryManager::OnPackagesRetrieved would queue them through this+4 (456),
 * this+5 (457) and this+6 (453), which MercuryManager::UpdateDatastore()
 * writes to the data store every frame. The APK has all the content: report
 * the SSX package as downloaded. */
#define OFF_MercuryManager_Init 0x16c5b0
static int (*o_mercury_init)(void *self, void *system, void *activity);
static int mercury_init(void *self, void *system, void *activity) {
  uint8_t *mm = self;
  if (!dcr_config()->skip_mercury)
    return o_mercury_init(self, system, activity);
  mm[4] = 1; /* 456: package info retrieved */
  mm[6] = 1; /* 453: package already downloaded */
  debugPrintf("[ssx] Mercury: the SSX package reported as downloaded\n");
  return 0;
}

/* ---------------------------------------------------------------- File::Copy
 * EA::IO::File::Copy(char const*, char const*, bool): the game's first start
 * copies its streamed files (music, movies: ~750 MB) out of the APK into its
 * files folder (GameCore::CopyBigFilesOutsideAppBundle). EA's copy moves
 * 2 KB at a time: AAsset_read(2048), then write(fd, 2048) -- each one an
 * append to the SD card. A PC's file system absorbs that (1 s for the 82 MB
 * stream_2.mus under Ryujinx); the console took minutes per file, ~0.5 MB/s,
 * so a first start sat on a black screen for ~25 minutes (hardware run c2).
 * The save does the same at every start: the 25 MB ghosts file is copied to
 * a new name (the "Saving" after the title), 75-78 s each time.
 * Here the source -- an appbundle: entry read straight from the APK, or a
 * file -- is read and written in COPY_CHUNK pieces; EA's copy is left only
 * what this cannot open. Its rule kept: with overwrite false an existing
 * destination is left alone (0). A file copied onto itself is refused (0):
 * EA's copy opened the destination for writing, truncating its own source. */
#define OFF_File_Copy 0x11dd24
#define COPY_CHUNK (4u << 20)
static int (*o_file_copy)(const char *src, const char *dst, int overwrite);

static int fast_copy(const char *src, const char *dst, int overwrite) {
  char from_real[DCR_PATH_MAX], to_real[DCR_PATH_MAX];
  const char *to = dcr_translate_path(dst, to_real, sizeof to_real);
  SsxAsset *a = NULL;
  FILE *in = NULL;
  long size;
  if (!strncmp(src, "appbundle:", 10)) {
    const char *name = src + 10;
    while (*name == '/')
      name++;
    if (!(a = ssx_asset_open(name)))
      return -1;
    size = ssx_asset_size(a);
  } else {
    const char *from = dcr_translate_path(src, from_real, sizeof from_real);
    if (!strcmp(from, to))
      return 0;
    if (!(in = fopen(from, "rb")))
      return -1;
    setvbuf(in, NULL, _IONBF, 0);
    if (fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) < 0 || fseek(in, 0, SEEK_SET) != 0) {
      fclose(in);
      return -1;
    }
  }
  if (!overwrite) {
    FILE *e = fopen(to, "rb");
    if (e) {
      fclose(e);
      if (a)
        ssx_asset_close(a);
      if (in)
        fclose(in);
      return 0;
    }
  }
  uint8_t *buf = malloc(COPY_CHUNK);
  FILE *f = buf ? fopen(to, "wb") : NULL;
  if (!f) {
    free(buf);
    if (a)
      ssx_asset_close(a);
    if (in)
      fclose(in);
    return -1;
  }
  setvbuf(f, NULL, _IONBF, 0);
  long done = 0;
  int ok = 1;
  while (done < size) {
    const long want = size - done < (long)COPY_CHUNK ? size - done : (long)COPY_CHUNK;
    const long n = a ? ssx_asset_read(a, buf, (size_t)want) : (long)fread(buf, 1, (size_t)want, in);
    if (n <= 0 || fwrite(buf, 1, (size_t)n, f) != (size_t)n) {
      ok = 0;
      break;
    }
    done += n;
  }
  ok = (fclose(f) == 0) && ok;
  free(buf);
  if (a)
    ssx_asset_close(a);
  if (in)
    fclose(in);
  if (!ok) {
    debugPrintf("[ssx] File::Copy: writing %s failed at %ld of %ld bytes\n", to, done, size);
    remove(to);
  }
  return ok;
}

static int file_copy(const char *src, const char *dst, int overwrite) {
  u64 t0 = armGetSystemTick();
  int r = -1;
  if (src && dst)
    r = fast_copy(src, dst, overwrite);
  const int fast = r >= 0;
  if (r < 0)
    r = o_file_copy(src, dst, overwrite);
  const u64 ms = armTicksToNs(armGetSystemTick() - t0) / 1000000ull;
  debugPrintf("[ssx] File::Copy(%s -> %s) = %d in %llu ms%s\n", src ? src : "?", dst ? dst : "?", r,
              (unsigned long long)ms, fast ? " (4 MB pieces)" : "");
  return r;
}

/* ---------------------------------------------------------------- GameRender
 * GameRender::PlatInit(): starts from a hard-coded 1280x720 render
 * resolution (this+28: render w, h, current w, h). Logged: 1280x720 is the
 * handheld screen, kept. */
#define OFF_GameRender_PlatInit 0xdd7504
static int (*o_platinit)(void *self);
static int gamerender_platinit(void *self) {
  int r = o_platinit(self);
  const int32_t *res = (const int32_t *)((const char *)self + 28);
  debugPrintf("[ssx] GameRender::PlatInit = %d: render %dx%d, current %dx%d\n", r, res[0], res[1], res[2], res[3]);
  return r;
}

/* ---------------------------------------------------------------- out of memory
 * MemoryFramework::ErrorHandlers::OutOfMemory(MemoryFailure const&) (and its
 * .constprop clone): the game prints the details through a printf of its own
 * that goes nowhere. Logged first, to know which memory.cfg heap failed. */
typedef struct {
  const char *category;
  const char *name;
  uint32_t size;
} MemoryFailure;
#define OFF_OutOfMemory 0x15a978
#define OFF_OutOfMemory2 0x156ba8
static void (*o_oom)(const MemoryFailure *f), (*o_oom2)(const MemoryFailure *f);
static void log_oom(const MemoryFailure *f) {
  debugPrintf("[ssx] OUT OF MEMORY: %u bytes for '%s' in '%s'\n", f ? (unsigned)f->size : 0,
              f && f->name ? f->name : "?", f && f->category ? f->category : "?");
  log_flush_ring();
}
static void oom(const MemoryFailure *f) {
  log_oom(f);
  o_oom(f);
}
static void oom2(const MemoryFailure *f) {
  log_oom(f);
  o_oom2(f);
}

/* ---------------------------------------------------------------- the audio thread
 * EA::Audio::Core::Dac::SubmitSamplesThreadFunc(void*), the thread that feeds
 * AudioTrack, runs while Dac::sDacActive and Dac::sSubmitThreadRunning are
 * set -- but it is started before Dac::Start() sets sDacActive. On Android
 * the JNI attach and AudioTrack.play() it does first take long enough to win
 * that race; here they are instant, the thread exits at once and Dac::Start()
 * waits for it forever (a black screen at boot). It waits for the flag
 * first (up to 5 s). */
#define OFF_Dac_SubmitSamplesThreadFunc 0x518dbc
#define OFF_Dac_sDacActive 0x184c650
static int (*o_dac_thread)(void *dac);
static inline uint32_t fpscr_now(void) {
  uint32_t v;
  __asm__ volatile("vmrs %0, fpscr" : "=r"(v));
  return v;
}
uint32_t ssx_fpscr(void) { return fpscr_now(); }

static int dac_thread(void *dac) {
  debugPrintf("[audio] mixer thread FPSCR 0x%08x\n", (unsigned)fpscr_now());
  volatile uint8_t *active = (volatile uint8_t *)ssx_text(OFF_Dac_sDacActive);
  int i = 0;
  for (; i < 500 && !*active; i++)
    svcSleepThread(10000000ll);
  debugPrintf("[audio] the game's mixer thread starts (sDacActive=%d after %d ms)\n", *active, i * 10);
  int r = o_dac_thread(dac);
  debugPrintf("[audio] the game's mixer thread ended\n");
  return r;
}

/* ---------------------------------------------------------------- all */
static unsigned g_applied, g_tried;

#define HOOK(off, expect, fn, orig)                                              \
  do {                                                                           \
    g_tried++;                                                                   \
    void *o_ = ssx_hook(off, expect, (void *)fn, #fn);                           \
    if (o_) {                                                                    \
      *(void **)&(orig) = o_;                                                    \
      g_applied++;                                                               \
    }                                                                            \
  } while (0)

void ssx_patch_game(so_module *m) {
  ssx_hook_begin(m);
  HOOK(OFF_MercuryManager_Init, 0xe92d41f0, mercury_init, o_mercury_init);
  HOOK(OFF_File_Copy, 0xe92d4070, file_copy, o_file_copy);
  HOOK(OFF_GameRender_PlatInit, 0xe92d4070, gamerender_platinit, o_platinit);
  HOOK(OFF_OutOfMemory, 0xe92d4070, oom, o_oom);
  HOOK(OFF_OutOfMemory2, 0xe92d4070, oom2, o_oom2);
  HOOK(OFF_Dac_SubmitSamplesThreadFunc, 0xe92d4ff0, dac_thread, o_dac_thread);
  void ssx_patch_more(void); /* ssx_patch2.c: frame rate, presses, performance */
  ssx_patch_more();
  void ssx_render_patch(void); /* ssx_render.c: the render targets */
  ssx_render_patch();
  void ssx_scopecache_patch(so_module * m); /* ssx_scopecache.c */
  ssx_scopecache_patch(m);
  void ssx_emufp_patch(so_module * m); /* ssx_emufp.c: emulator only */
  ssx_emufp_patch(m);
  ssx_hook_end();
  debugPrintf("[patch] %u of %u hooks applied\n", g_applied, g_tried);
}

void ssx_fps_report(void); /* ssx_patch2.c */
void ssx_hwskin_report(void); /* ssx_hwskin.c */
void ssx_gl_report(void);     /* ssx_gl.c */
void ssx_terraincull_report(void); /* ssx_terraincull.c */
void ssx_scopecache_report(void);  /* ssx_scopecache.c */
void ssx_mathguard_report(void);   /* ssx_mathguard.c */
void ssx_spinwait_report(void);    /* ssx_spinwait.c */
void ssx_patch_report(void) {
  ssx_fps_report();
  ssx_mathguard_report();
  ssx_spinwait_report();
  ssx_hwskin_report();
  ssx_gl_report();
  ssx_terraincull_report();
  ssx_scopecache_report();
  void ssx_atomic64_report(void); /* ssx_atomic64.c */
  ssx_atomic64_report();
  void ssx_detail_report(void); /* ssx_detail.c */
  ssx_detail_report();
}
