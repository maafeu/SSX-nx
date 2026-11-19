/* ssx_overlay.c -- the frame-rate counter ([display] fps_counter), drawn over
 * the game's frame before each swap (port_gl_before_swap, ssx_movie.c).
 *
 * Top left: frames presented per second, and in races the speed of the race
 * simulation ("SIM 100%" = every step on time; below 100 the race plays in
 * slow motion even when the frame rate looks fine -- the PS Vita port's
 * v1.1.0 notes explain why). Green at 59 fps and over, yellow from 45, red
 * under. A 5x7 pixel font as solid quads: one program, no textures, the
 * game's GL state saved and restored as the movie overlay does. MIT.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_window.h"
#include "ssx.h"
#include "util.h"

int ssx_loop_stats(uint32_t *sim_iterations, float *sim_hz); /* ssx_patch2.c: -1 before the loops exist */

/* 5x7 glyphs, one row per byte (bit 4 = the left column) */
static const struct {
  char c;
  uint8_t row[7];
} k_font[] = {
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}}, {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}}, {'3', {0x1f, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}}, {'5', {0x1f, 0x10, 0x1e, 0x01, 0x01, 0x11, 0x0e}},
    {'6', {0x06, 0x08, 0x10, 0x1e, 0x11, 0x11, 0x0e}}, {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}}, {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x02, 0x0c}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}}, {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}}, {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}}, {'%', {0x19, 0x1a, 0x02, 0x04, 0x08, 0x0b, 0x13}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
};

#define MAX_CHARS 24
static GLfloat g_verts[MAX_CHARS * 35 * 12 + 12];
static GLuint g_prog, g_vao, g_vbo;
static GLint g_loc_pos, g_loc_color;
static void (*p_bind_vao)(GLuint), (*p_gen_vao)(GLsizei, GLuint *);
static int g_failed;

static GLuint shader(GLenum type, const char *src) {
  GLuint s = glCreateShader(type);
  glShaderSource(s, 1, &src, NULL);
  glCompileShader(s);
  return s;
}

static int setup(void) {
  if (g_prog)
    return 0;
  if (g_failed)
    return -1;
  static const char *vs = "attribute vec2 a_pos; void main() { gl_Position = vec4(a_pos, 0.0, 1.0); }\n";
  static const char *fs = "precision mediump float; uniform vec4 u_color; void main() { gl_FragColor = u_color; }\n";
  GLuint p = glCreateProgram();
  glAttachShader(p, shader(GL_VERTEX_SHADER, vs));
  glAttachShader(p, shader(GL_FRAGMENT_SHADER, fs));
  glLinkProgram(p);
  GLint ok = 0;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  if (!ok) {
    g_failed = 1;
    debugPrintf("[overlay] the counter's shader did not link: no counter\n");
    return -1;
  }
  g_loc_pos = glGetAttribLocation(p, "a_pos");
  g_loc_color = glGetUniformLocation(p, "u_color");
  p_bind_vao = (void (*)(GLuint))eglGetProcAddress("glBindVertexArrayOES");
  p_gen_vao = (void (*)(GLsizei, GLuint *))eglGetProcAddress("glGenVertexArraysOES");
  if (p_gen_vao && p_bind_vao)
    p_gen_vao(1, &g_vao);
  /* The vertices go through a buffer of ours: with a vertex array object of
   * ours bound, an ES 3 context refuses client-memory arrays
   * (GL_INVALID_OPERATION, "glVertexAttribPointer(non-VBO array)": console run
   * c3, 2026-11-16; the error then showed up as the next rider mesh's). */
  glGenBuffers(1, &g_vbo);
  g_prog = p;
  return 0;
}

/* The text's quads (pixels: x right, y down from the top left) into
 * g_verts as clip-space triangles; returns the vertex count. */
static int layout(const char *text, int x0, int y0, int scale, int ww, int wh) {
  int n = 0, x = x0;
  for (const char *s = text; *s && s - text < MAX_CHARS; s++, x += 6 * scale) {
    const uint8_t *rows = NULL;
    for (unsigned k = 0; k < sizeof k_font / sizeof k_font[0]; k++)
      if (k_font[k].c == *s)
        rows = k_font[k].row;
    if (!rows)
      continue; /* a space */
    for (int r = 0; r < 7; r++)
      for (int c = 0; c < 5; c++) {
        if (!(rows[r] & (0x10 >> c)))
          continue;
        const float l = (float)(x + c * scale) / ww * 2 - 1, rt = (float)(x + (c + 1) * scale) / ww * 2 - 1;
        const float t = 1 - (float)(y0 + r * scale) / wh * 2, b = 1 - (float)(y0 + (r + 1) * scale) / wh * 2;
        const GLfloat q[12] = {l, b, rt, b, l, t, l, t, rt, b, rt, t};
        memcpy(g_verts + n * 2, q, sizeof q);
        n += 6;
      }
  }
  return n;
}

static char g_text[MAX_CHARS + 1];
static float g_fps = 60;

/* Once a second: the text. */
static void measure(void) {
  static u64 t_prev, t_call;
  static uint32_t frames_prev, sim_prev;
  const u64 now = armGetSystemTick();
  /* no frame for 250 ms (HOME, sleep): the game was away; start a new second
   * instead of averaging the time away in (the console showed "3 FPS" after
   * HOME and back) */
  if (t_call && armTicksToNs(now - t_call) > 250000000ull)
    t_prev = 0;
  t_call = now;
  if (t_prev && armTicksToNs(now - t_prev) < 1000000000ull)
    return;
  const uint32_t frames = g_ssx_frames_presented;
  uint32_t sim = 0;
  float hz = 0;
  const int race = ssx_loop_stats(&sim, &hz);
  if (t_prev) {
    const double secs = (double)armTicksToNs(now - t_prev) / 1e9;
    g_fps = (float)((frames - frames_prev) / secs);
    int speed = hz > 0 ? (int)((sim - sim_prev) / secs / hz * 100 + 0.5) : 0;
    if (speed > 100)
      speed = 100;
    if (race == 1)
      snprintf(g_text, sizeof g_text, "%d FPS  SIM %d%%", (int)(g_fps + 0.5f), speed);
    else
      snprintf(g_text, sizeof g_text, "%d FPS", (int)(g_fps + 0.5f));
  }
  t_prev = now, frames_prev = frames, sim_prev = sim;
}

void ssx_overlay_draw(void) {
  if (!dcr_config()->fps_counter)
    return;
  measure();
  if (!g_text[0] || setup() != 0)
    return;
  int ww = 1280, wh = 720;
  dcr_window_size(&ww, &wh);
  const int scale = wh >= 1080 ? 4 : 3, pad = 2 * scale;
  const int tw = (int)strlen(g_text) * 6 * scale - scale, th = 7 * scale;
  const int x0 = 8 * scale / 3 + pad, y0 = 8 * scale / 3 + pad;

  /* the game's state */
  GLint prog, abuf, vao = 0, fb, vp[4], blend, depth, cull, scissor, stencil, bsrc, bdst, bsrc_a, bdst_a;
  GLboolean cmask[4];
  glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &abuf);
  if (g_vao)
    glGetIntegerv(0x85B5 /* GL_VERTEX_ARRAY_BINDING_OES */, &vao);
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
  glGetIntegerv(GL_VIEWPORT, vp);
  glGetIntegerv(GL_BLEND_SRC_RGB, &bsrc);
  glGetIntegerv(GL_BLEND_DST_RGB, &bdst);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &bsrc_a);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &bdst_a);
  glGetBooleanv(GL_COLOR_WRITEMASK, cmask);
  blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
  scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, ww, wh);
  glDisable(GL_DEPTH_TEST), glDisable(GL_CULL_FACE), glDisable(GL_SCISSOR_TEST), glDisable(GL_STENCIL_TEST);
  glColorMask(1, 1, 1, 1);
  glUseProgram(g_prog);
  if (g_vao)
    p_bind_vao(g_vao);
  glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  glEnableVertexAttribArray((GLuint)g_loc_pos);

  /* a dark box behind the text */
  {
    const float l = (float)(x0 - pad) / ww * 2 - 1, r = (float)(x0 + tw + pad) / ww * 2 - 1;
    const float t = 1 - (float)(y0 - pad) / wh * 2, b = 1 - (float)(y0 + th + pad) / wh * 2;
    const GLfloat box[12] = {l, b, r, b, l, t, l, t, r, b, r, t};
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUniform4f(g_loc_color, 0, 0, 0, 0.55f);
    glBufferData(GL_ARRAY_BUFFER, sizeof box, box, GL_STREAM_DRAW);
    glVertexAttribPointer((GLuint)g_loc_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);
    glDrawArrays(GL_TRIANGLES, 0, 6);
  }
  const int n = layout(g_text, x0, y0, scale, ww, wh);
  glDisable(GL_BLEND);
  if (g_fps >= 59)
    glUniform4f(g_loc_color, 0.35f, 1.0f, 0.35f, 1);
  else if (g_fps >= 45)
    glUniform4f(g_loc_color, 1.0f, 0.85f, 0.2f, 1);
  else
    glUniform4f(g_loc_color, 1.0f, 0.3f, 0.3f, 1);
  glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(n * 2 * sizeof(GLfloat)), g_verts, GL_STREAM_DRAW);
  glVertexAttribPointer((GLuint)g_loc_pos, 2, GL_FLOAT, GL_FALSE, 0, (const void *)0);
  glDrawArrays(GL_TRIANGLES, 0, n);
  if (!g_vao)
    glDisableVertexAttribArray((GLuint)g_loc_pos);

  /* the game's state back */
  if (g_vao)
    p_bind_vao((GLuint)vao);
  glBindBuffer(GL_ARRAY_BUFFER, (GLuint)abuf);
  glUseProgram((GLuint)prog);
  glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)fb);
  glViewport(vp[0], vp[1], vp[2], vp[3]);
  glBlendFuncSeparate((GLenum)bsrc, (GLenum)bdst, (GLenum)bsrc_a, (GLenum)bdst_a);
  glColorMask(cmask[0], cmask[1], cmask[2], cmask[3]);
  if (blend)
    glEnable(GL_BLEND);
  else
    glDisable(GL_BLEND);
  if (depth)
    glEnable(GL_DEPTH_TEST);
  if (cull)
    glEnable(GL_CULL_FACE);
  if (scissor)
    glEnable(GL_SCISSOR_TEST);
  if (stencil)
    glEnable(GL_STENCIL_TEST);
}

void ssx_movie_draw(void); /* ssx_movie.c */

/* gl_layer.h: before each eglSwapBuffers of the game */
void port_gl_before_swap(void) {
  ssx_movie_draw();
  ssx_overlay_draw();
  void ssx_saltynx_frame(void); /* ssx_saltynx.c */
  ssx_saltynx_frame();
  void ssx_shadercache_frame(void); /* ssx_shadercache.c */
  ssx_shadercache_frame();
}
