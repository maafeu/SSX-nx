/* ssx_movie.c -- playing the game's movies (FFmpeg: ffmpeg32's MP4 demuxer,
 * MPEG-4 Part 2 video and AAC audio; built with int-sized enums, like this
 * file: see the Makefile).
 *
 * The APK's movies (the .mp4 files in common/mp4: the EA logo, the intro, the World
 * Tour comics and the mountains' fly-ins) are MPEG-4 Part 2 + AAC, stored in
 * the APK: they are demuxed in place through ssx_assets.c. A decoder thread
 * keeps a few frames ahead (YUV 4:2:0 copies); the render thread, just before
 * the game's eglSwapBuffers (port_gl_before_swap), uploads the frame due and
 * draws it over the game's picture with a YUV->RGB shader -- as Android drew
 * its MediaPlayer's SurfaceView over the game's GL view. Every GL state it
 * touches is put back (the game caches its own), and its vertex setup lives
 * in a vertex array object of its own. The sound is converted to 48 kHz
 * stereo and mixed into the game's own AudioTrack blocks (ssx_audio.c).
 * The clock is the console's: frames are shown by their time stamps. MIT.
 */
#ifndef SSX_VIDEO
#define SSX_VIDEO 0
#endif

#if !SSX_VIDEO
/* No FFmpeg in this build: no movie starts (ssx_video.c then skips it). */
#include <stdint.h>
int ssx_movie_start(const char *path) { return -1; }
void ssx_movie_stop(void) {}
int ssx_movie_finished(void) { return 1; }
int ssx_movie_active(void) { return 0; }
void ssx_movie_mix(int16_t *block, unsigned frames) {}
void ssx_movie_draw(void) {}
#else

#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/cpu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "rt_window.h"
#include "ssx.h"
#include "util.h"

static unsigned g_movie_gen;      /* counts movie starts (ssx_movie_draw's uploads) */
#define QUEUE 4                 /* decoded frames kept ahead */
#define PCM_RING (48000 * 2)    /* 1 s of 48 kHz stereo s16 (in samples) */

typedef struct {
  uint8_t *y, *u, *v;
  int64_t pts_us;
} Frame;

static struct {
  Mutex lock;
  Thread thread;
  volatile int running;  /* the decoder thread lives */
  volatile int stop;     /* asked to stop */
  volatile int done;     /* the file is over (all frames decoded) */
  int w, h;
  Frame q[QUEUE];
  int head, count;       /* ring of decoded frames */
  u64 t0;                /* console tick at the first frame shown */
  int shown_any;
  int64_t last_pts;
  /* sound */
  int16_t pcm[PCM_RING];
  unsigned pcm_r, pcm_w; /* samples, mod PCM_RING */
  char path[256];
} M;

/* ---------------------------------------------------------------- the sound ring */
static unsigned pcm_count(void) { return (M.pcm_w - M.pcm_r + PCM_RING) % PCM_RING; }

static void pcm_push(const int16_t *s, unsigned n) {
  for (;;) {
    mutexLock(&M.lock);
    unsigned room = PCM_RING - 1 - pcm_count();
    if (room >= n || M.stop) {
      for (unsigned i = 0; i < n && i < room; i++) {
        M.pcm[M.pcm_w] = s[i];
        M.pcm_w = (M.pcm_w + 1) % PCM_RING;
      }
      mutexUnlock(&M.lock);
      return;
    }
    mutexUnlock(&M.lock);
    svcSleepThread(5000000ll); /* the game's mixer takes 1024 frames each ~21 ms */
  }
}

/* ssx_audio.c: add the movie's sound to a block about to go out (stereo s16). */
void ssx_movie_mix(int16_t *block, unsigned frames) {
  if (!M.running && !pcm_count())
    return;
  mutexLock(&M.lock);
  unsigned n = frames * 2, have = pcm_count();
  if (n > have)
    n = have;
  for (unsigned i = 0; i < n; i++) {
    int v = block[i] + M.pcm[M.pcm_r];
    block[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    M.pcm_r = (M.pcm_r + 1) % PCM_RING;
  }
  mutexUnlock(&M.lock);
}

/* ---------------------------------------------------------------- reading from the APK */
static int io_read(void *opaque, uint8_t *buf, int n) {
  int r = ssx_asset_read(opaque, buf, (size_t)n);
  return r > 0 ? r : AVERROR_EOF;
}
static int64_t io_seek(void *opaque, int64_t off, int whence) {
  if (whence == AVSEEK_SIZE)
    return ssx_asset_size(opaque);
  return ssx_asset_seek(opaque, (long)off, whence & 3);
}

/* ---------------------------------------------------------------- the decoder thread */
static void queue_frame(const AVFrame *f, int64_t pts_us) {
  for (;;) {
    mutexLock(&M.lock);
    if (M.count < QUEUE || M.stop)
      break;
    mutexUnlock(&M.lock);
    svcSleepThread(4000000ll);
  }
  if (M.stop) {
    mutexUnlock(&M.lock);
    return;
  }
  Frame *q = &M.q[(M.head + M.count) % QUEUE];
  mutexUnlock(&M.lock);
  const int w = M.w, h = M.h;
  for (int y = 0; y < h; y++)
    memcpy(q->y + y * w, f->data[0] + y * f->linesize[0], (size_t)w);
  for (int y = 0; y < h / 2; y++) {
    memcpy(q->u + y * (w / 2), f->data[1] + y * f->linesize[1], (size_t)(w / 2));
    memcpy(q->v + y * (w / 2), f->data[2] + y * f->linesize[2], (size_t)(w / 2));
  }
  q->pts_us = pts_us;
  mutexLock(&M.lock);
  M.count++;
  mutexUnlock(&M.lock);
}

/* AAC (planar float, any rate) -> 48 kHz stereo s16, linear resampling */
static double g_rs_pos;
static void audio_frame(const AVFrame *f) {
  const int ch = f->ch_layout.nb_channels;
  if (f->format != AV_SAMPLE_FMT_FLTP || ch < 1 || f->sample_rate <= 0)
    return;
  const float *l = (const float *)f->data[0], *r = (const float *)f->data[ch > 1 ? 1 : 0];
  const double step = (double)f->sample_rate / 48000.0;
  static int16_t out[4096];
  unsigned n = 0;
  for (; g_rs_pos < f->nb_samples - 1; g_rs_pos += step) {
    int i = (int)g_rs_pos;
    float t = (float)(g_rs_pos - i);
    float a = l[i] + (l[i + 1] - l[i]) * t, b = r[i] + (r[i + 1] - r[i]) * t;
    int va = (int)(a * 32767.0f), vb = (int)(b * 32767.0f);
    out[n++] = (int16_t)(va > 32767 ? 32767 : va < -32768 ? -32768 : va);
    out[n++] = (int16_t)(vb > 32767 ? 32767 : vb < -32768 ? -32768 : vb);
    if (n >= sizeof out / sizeof out[0]) {
      pcm_push(out, n);
      n = 0;
    }
  }
  g_rs_pos -= f->nb_samples - 1;
  if (n)
    pcm_push(out, n);
}

static void decoder_thread(void *arg) {
  SsxAsset *as = arg;
  AVFormatContext *fmt = NULL;
  AVCodecContext *vdec = NULL, *adec = NULL;
  AVPacket *pkt = av_packet_alloc();
  AVFrame *fr = av_frame_alloc();
  uint8_t *iobuf = av_malloc(65536);
  AVIOContext *io = avio_alloc_context(iobuf, 65536, 0, as, io_read, NULL, io_seek);
  int vs = -1, ast = -1;
  fmt = avformat_alloc_context();
  fmt->pb = io;
  if (avformat_open_input(&fmt, NULL, NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) {
    debugPrintf("[movie] %s: cannot be read\n", M.path);
    goto out;
  }
  for (unsigned i = 0; i < fmt->nb_streams; i++) {
    const AVCodecParameters *p = fmt->streams[i]->codecpar;
    const AVCodec *c = avcodec_find_decoder(p->codec_id);
    if (!c)
      continue;
    if (p->codec_type == AVMEDIA_TYPE_VIDEO && vs < 0) {
      vdec = avcodec_alloc_context3(c);
      avcodec_parameters_to_context(vdec, p);
      if (avcodec_open2(vdec, c, NULL) == 0)
        vs = (int)i;
    } else if (p->codec_type == AVMEDIA_TYPE_AUDIO && ast < 0) {
      adec = avcodec_alloc_context3(c);
      avcodec_parameters_to_context(adec, p);
      if (avcodec_open2(adec, c, NULL) == 0)
        ast = (int)i;
    }
  }
  if (vs < 0) {
    debugPrintf("[movie] %s: no video stream it can decode\n", M.path);
    goto out;
  }
  M.w = vdec->width & ~1;
  M.h = vdec->height & ~1;
  for (int i = 0; i < QUEUE; i++) {
    M.q[i].y = malloc((size_t)M.w * M.h);
    M.q[i].u = malloc((size_t)M.w * M.h / 4);
    M.q[i].v = malloc((size_t)M.w * M.h / 4);
  }
  debugPrintf("[movie] %s: %dx%d %s, sound %s\n", M.path, M.w, M.h, avcodec_get_name(vdec->codec_id),
              ast >= 0 ? avcodec_get_name(adec->codec_id) : "none");
  const AVRational vtb = fmt->streams[vs]->time_base;
  g_rs_pos = 0;
  while (!M.stop && av_read_frame(fmt, pkt) >= 0) {
    AVCodecContext *d = pkt->stream_index == vs ? vdec : pkt->stream_index == ast ? adec : NULL;
    if (d && avcodec_send_packet(d, pkt) == 0) {
      while (!M.stop && avcodec_receive_frame(d, fr) == 0) {
        if (d == vdec) {
          int64_t pts = fr->best_effort_timestamp != AV_NOPTS_VALUE ? fr->best_effort_timestamp : 0;
          queue_frame(fr, av_rescale_q(pts, vtb, (AVRational){1, 1000000}));
        } else {
          audio_frame(fr);
        }
      }
    }
    av_packet_unref(pkt);
  }
  /* the decoder's last frames */
  if (!M.stop && avcodec_send_packet(vdec, NULL) == 0)
    while (!M.stop && avcodec_receive_frame(vdec, fr) == 0) {
      int64_t pts = fr->best_effort_timestamp != AV_NOPTS_VALUE ? fr->best_effort_timestamp : 0;
      queue_frame(fr, av_rescale_q(pts, vtb, (AVRational){1, 1000000}));
    }
out:
  avcodec_free_context(&vdec);
  avcodec_free_context(&adec);
  if (fmt)
    avformat_close_input(&fmt);
  if (io) {
    av_freep(&io->buffer);
    avio_context_free(&io);
  }
  av_frame_free(&fr);
  av_packet_free(&pkt);
  ssx_asset_close(as);
  M.done = 1;
}

/* ---------------------------------------------------------------- start / stop */
static void free_frames(void) {
  for (int i = 0; i < QUEUE; i++) {
    free(M.q[i].y), free(M.q[i].u), free(M.q[i].v);
    M.q[i].y = M.q[i].u = M.q[i].v = NULL;
  }
}

void ssx_movie_stop(void) {
  if (!M.running)
    return;
  M.stop = 1;
  threadWaitForExit(&M.thread);
  threadClose(&M.thread);
  mutexLock(&M.lock);
  M.running = 0;
  M.count = 0;
  M.pcm_r = M.pcm_w = 0;
  mutexUnlock(&M.lock);
  free_frames();
}

/* The asset of a movie path the game gives: "appbundle:/common/mp4/x.mp4",
 * or its copy in the files folder ("/data/.../files/x.mp4": the same file). */
static SsxAsset *open_movie(const char *path, char *name, size_t cap) {
  const char *p = path;
  if (!strncmp(p, "appbundle:", 10))
    p += 10;
  while (*p == '/')
    p++;
  SsxAsset *a = ssx_asset_open(p);
  if (a) {
    snprintf(name, cap, "%s", p);
    return a;
  }
  const char *base = strrchr(path, '/');
  snprintf(name, cap, "common/mp4/%s", base ? base + 1 : path);
  return ssx_asset_open(name);
}

int ssx_movie_start(const char *path) {
  ssx_movie_stop();
  /* Under an emulator FFmpeg takes its C paths: Ryujinx's A32 decoder lacks
   * VMLAL by scalar (simple_idct_neon's idct_row4_neon: UndefinedInstruction,
   * emu22). The console runs the NEON code. */
  static int cpu_set;
  if (!cpu_set) {
    cpu_set = 1;
    if (dcr_is_emulator()) {
      av_force_cpu_flags(0);
      debugPrintf("[movie] emulator: FFmpeg without NEON\n");
    }
  }
  char name[256];
  SsxAsset *a = open_movie(path, name, sizeof name);
  if (!a) {
    debugPrintf("[movie] %s: not in the APK\n", path);
    return -1;
  }
  memset(M.q, 0, sizeof M.q);
  M.head = M.count = 0;
  M.stop = M.done = 0;
  M.shown_any = 0;
  M.last_pts = -1;
  g_movie_gen++;
  M.w = M.h = 0;
  M.pcm_r = M.pcm_w = 0;
  snprintf(M.path, sizeof M.path, "%s", name);
  if (R_FAILED(threadCreate(&M.thread, decoder_thread, a, NULL, 0x20000, 0x2C, -2))) {
    ssx_asset_close(a);
    return -1;
  }
  M.running = 1;
  threadStart(&M.thread);
  return 0;
}

/* Over: decoded to the end and every frame shown. */
int ssx_movie_finished(void) {
  if (!M.running)
    return 1;
  mutexLock(&M.lock);
  int over = M.done && M.count == 0;
  mutexUnlock(&M.lock);
  return over;
}

int ssx_movie_active(void) { return M.running; }

/* ---------------------------------------------------------------- drawing */
/* Two sets of plane textures, used in turn: a frame is uploaded only when a
 * new one is due (the movies are 30 fps or less, the swaps 60), and into the
 * set the GPU is not drawing from. Uploading the whole 1920x1080 frame at
 * every swap into the texture the last draw used held the render thread up
 * (17-40 ms render iterations and 47-52 fps during the intro movies on the
 * console, hardware run c2). */
#define TEX_SETS 2
static GLuint g_prog, g_tex[TEX_SETS][3];
static GLint g_loc_pos, g_loc_uv;
static GLuint g_vao, g_vbo;
static void (*p_bind_vao)(GLuint), (*p_gen_vao)(GLsizei, GLuint *);
static int g_tex_w[TEX_SETS], g_tex_h[TEX_SETS];
static int g_set = -1;            /* the set holding the frame on screen */
static const Frame *g_up_frame;   /* the frame in it (slot and time stamp) */
static int64_t g_up_pts;
static unsigned g_up_movie;

static GLuint shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  GLint ok = 0;
  glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[512] = "";
    glGetShaderInfoLog(s, sizeof log, NULL, log);
    debugPrintf("[movie] shader: %s\n", log);
  }
  return s;
}

static int gl_setup(void) {
  if (g_prog)
    return 0;
  static const char *vs = "attribute vec2 a_pos; attribute vec2 a_uv; varying vec2 v_uv;\n"
                          "void main() { v_uv = a_uv; gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  static const char *fs = "precision mediump float; varying vec2 v_uv;\n"
                          "uniform sampler2D t_y, t_u, t_v;\n"
                          "void main() {\n"
                          "  float y = 1.16438 * (texture2D(t_y, v_uv).r - 0.0627);\n"
                          "  float u = texture2D(t_u, v_uv).r - 0.5, v = texture2D(t_v, v_uv).r - 0.5;\n"
                          "  gl_FragColor = vec4(y + 1.59603 * v, y - 0.39176 * u - 0.81297 * v, y + 2.01723 * u, 1.0);\n"
                          "}\n";
  GLuint p = glCreateProgram();
  glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok)
    return -1;
  g_loc_pos = glGetAttribLocation(p, "a_pos");
  g_loc_uv = glGetAttribLocation(p, "a_uv");
  GLint prev;
  glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
  glUseProgram(p);
  glUniform1i(glGetUniformLocation(p, "t_y"), 0);
  glUniform1i(glGetUniformLocation(p, "t_u"), 1);
  glUniform1i(glGetUniformLocation(p, "t_v"), 2);
  glUseProgram((GLuint)prev);
  glGenTextures(TEX_SETS * 3, &g_tex[0][0]);
  p_bind_vao = (void (*)(GLuint))eglGetProcAddress("glBindVertexArrayOES");
  p_gen_vao = (void (*)(GLsizei, GLuint *))eglGetProcAddress("glGenVertexArraysOES");
  if (p_gen_vao)
    p_gen_vao(1, &g_vao);
  glGenBuffers(1, &g_vbo); /* the quad: client-memory arrays are refused with our VAO bound (ES 3) */
  g_prog = p;
  return 0;
}

/* The frame due now (or the last one shown); 1 when there is one to draw. */
static Frame *frame_due(void) {
  mutexLock(&M.lock);
  if (!M.count) {
    mutexUnlock(&M.lock);
    return NULL;
  }
  if (!M.shown_any) {
    /* the clock starts at the first frame's time stamp */
    M.t0 = armGetSystemTick();
    M.last_pts = M.q[M.head].pts_us;
    M.shown_any = 1;
    mutexUnlock(&M.lock);
    return &M.q[M.head];
  }
  /* the movie's time now; a frame gives way once its successor is due */
  const int64_t now = M.last_pts + (int64_t)(armTicksToNs(armGetSystemTick() - M.t0) / 1000);
  while (M.count > 1 && M.q[(M.head + 1) % QUEUE].pts_us <= now) {
    M.head = (M.head + 1) % QUEUE;
    M.count--;
  }
  Frame *f = &M.q[M.head];
  mutexUnlock(&M.lock);
  return f;
}

static void frame_consumed_if_last(void) {
  mutexLock(&M.lock);
  if (M.done && M.count == 1) {
    const int64_t now = M.last_pts + (int64_t)(armTicksToNs(armGetSystemTick() - M.t0) / 1000);
    if (now >= M.q[M.head].pts_us + 40000) {
      M.head = (M.head + 1) % QUEUE;
      M.count--;
    }
  }
  mutexUnlock(&M.lock);
}

/* Before each swap (port_gl_before_swap, ssx_overlay.c): the movie over
 * the game's frame. */
void ssx_movie_draw(void) {
  if (!M.running || !M.w)
    return;
  Frame *f = frame_due();
  if (!f)
    return;
  if (gl_setup() != 0)
    return;
  /* the game's state */
  GLint prog, active, tex[3], abuf, vao = 0, fb, vp[4], align, blend, depth, cull, scissor, stencil;
  GLboolean cmask[4];
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
  for (int i = 0; i < 3; i++) {
    glActiveTexture(GL_TEXTURE0 + i);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex[i]);
  }
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
  if (g_vao)
    glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING_OES */, &vao);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
  glGetIntegerv(GL_VIEWPORT, vp);
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &align);
  glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
  blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
  scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);

  /* the frame's planes: uploaded when a new frame is due, into the other set */
  const int fresh = g_set < 0 || f != g_up_frame || f->pts_us != g_up_pts || g_up_movie != g_movie_gen;
  if (fresh) {
    const int set = (g_set + 1) % TEX_SETS;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    const int w[3] = {M.w, M.w / 2, M.w / 2}, h[3] = {M.h, M.h / 2, M.h / 2};
    const uint8_t *src[3] = {f->y, f->u, f->v};
    for (int i = 0; i < 3; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, g_tex[set][i]);
      if (g_tex_w[set] != M.w || g_tex_h[set] != M.h) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, w[i], h[i], 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, src[i]);
      } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w[i], h[i], GL_LUMINANCE, GL_UNSIGNED_BYTE, src[i]);
      }
    }
    g_tex_w[set] = M.w, g_tex_h[set] = M.h;
    g_set = set, g_up_frame = f, g_up_pts = f->pts_us, g_up_movie = g_movie_gen;
  } else {
    for (int i = 0; i < 3; i++) {
      glActiveTexture(GL_TEXTURE0 + i);
      glBindTexture(GL_TEXTURE_2D, g_tex[g_set][i]);
    }
  }

  /* a full-screen quad, letterboxed to the movie's shape */
  int ww = 1280, wh = 720;
  dcr_window_size(&ww, &wh);
  float sx = 1.0f, sy = 1.0f;
  const float ma = (float)M.w / (float)M.h, wa = (float)ww / (float)wh;
  if (ma > wa)
    sy = wa / ma;
  else
    sx = ma / wa;
  const GLfloat quad[] = {-sx, -sy, 0, 1, sx, -sy, 1, 1, -sx, sy, 0, 0, sx, sy, 1, 0};
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, ww, wh);
  glDisable(GL_BLEND), glDisable(GL_DEPTH_TEST), glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST), glDisable(GL_STENCIL_TEST);
  glColorMask(1, 1, 1, 1);
  glClearColor(0, 0, 0, 1);
  glClear(GL_COLOR_BUFFER_BIT);
  glUseProgram(g_prog);
  if (g_vao && p_bind_vao)
    p_bind_vao(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STREAM_DRAW);
  glEnableVertexAttribArray((GLuint)g_loc_pos);
  glEnableVertexAttribArray((GLuint)g_loc_uv);
  glVertexAttribPointer((GLuint)g_loc_pos, 2, GL_FLOAT, GL_FALSE, 16, (const void *)0);
  glVertexAttribPointer((GLuint)g_loc_uv, 2, GL_FLOAT, GL_FALSE, 16, (const void *)8);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  if (!g_vao) {
    glDisableVertexAttribArray((GLuint)g_loc_pos);
    glDisableVertexAttribArray((GLuint)g_loc_uv);
  }

  /* the game's state back */
  if (g_vao && p_bind_vao)
    p_bind_vao((GLuint)vao);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
  glUseProgram((GLuint)prog);
  for (int i = 2; i >= 0; i--) {
    glActiveTexture(GL_TEXTURE0 + i);
    glBindTexture(GL_TEXTURE_2D, (GLuint)tex[i]);
  }
  glActiveTexture((GLenum)active);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glPixelStorei(GL_UNPACK_ALIGNMENT, align);
  glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
  if (blend) glEnable(GL_BLEND);
  if (depth) glEnable(GL_DEPTH_TEST);
  if (cull) glEnable(GL_CULL_FACE);
  if (scissor) glEnable(GL_SCISSOR_TEST);
  if (stencil) glEnable(GL_STENCIL_TEST);
  frame_consumed_if_last();
}

#endif /* SSX_VIDEO */
