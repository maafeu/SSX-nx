/* ssx_gl.c -- the game's GL calls, where the port needs to see or change them
 * (the runtime's port_gl_wrap: every gl* lookup goes through it).
 *
 * Diagnostics: the first viewports, scissors and framebuffer binds are
 * logged ([debug] gl_trace in config.ini makes it the first 400). MIT.
 */
#include <GLES3/gl3.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "gl_layer.h"
#include "ssx.h"
#include "util.h"

static int g_trace = 120;

typedef void (*fn_viewport)(GLint, GLint, GLsizei, GLsizei);
typedef void (*fn_bindfb)(GLenum, GLuint);
typedef void (*fn_fbtex)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void (*fn_teximage)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static fn_viewport r_viewport, r_scissor;
static fn_bindfb r_bindfb;
static fn_fbtex r_fbtex;

static void w_viewport(GLint x, GLint y, GLsizei w, GLsizei h) {
  if (g_trace > 0 && g_trace--)
    debugPrintf("[gl] glViewport(%d, %d, %d, %d)\n", x, y, w, h);
  r_viewport(x, y, w, h);
}
static void w_scissor(GLint x, GLint y, GLsizei w, GLsizei h) {
  if (g_trace > 0 && g_trace--)
    debugPrintf("[gl] glScissor(%d, %d, %d, %d)\n", x, y, w, h);
  r_scissor(x, y, w, h);
}
static void w_bindfb(GLenum t, GLuint fb) {
  if (g_trace > 0 && g_trace--)
    debugPrintf("[gl] glBindFramebuffer(0x%x, %u)\n", t, fb);
  r_bindfb(t, fb);
}
static void w_fbtex(GLenum t, GLenum att, GLenum tt, GLuint tex, GLint lvl) {
  if (g_trace > 0 && g_trace--)
    debugPrintf("[gl] glFramebufferTexture2D(0x%x, 0x%x, 0x%x, %u, %d)\n", t, att, tt, tex, lvl);
  r_fbtex(t, att, tt, tex, lvl);
}

/* Shader and program failures (the game does not report them): logged with
 * their info log, the first 24. */
typedef void (*fn_u)(GLuint);
static fn_u r_compile, r_link;
static int g_shader_fails;
static unsigned g_compiles, g_links;
static void w_compile(GLuint sh) {
  r_compile(sh);
  g_compiles++;
  GLint ok = 1;
  glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (!ok && g_shader_fails++ < 24) {
    char log[1024] = "";
    glGetShaderInfoLog(sh, sizeof log, NULL, log);
    GLint type = 0;
    glGetShaderiv(sh, GL_SHADER_TYPE, &type);
    debugPrintf("[gl] %s shader %u did not compile: %s\n", type == GL_VERTEX_SHADER ? "vertex" : "fragment", sh, log);
    GLint len = 0;
    glGetShaderiv(sh, GL_SHADER_SOURCE_LENGTH, &len);
    static char src[8192];
    glGetShaderSource(sh, sizeof src, NULL, src);
    debugPrintf("[gl] its source (%d bytes, first 2000): %.2000s\n", len, src);
  }
}
static void w_link(GLuint prog) {
  r_link(prog);
  g_links++;
  GLint ok = 1;
  glGetProgramiv(prog, GL_LINK_STATUS, &ok);
  if (!ok && g_shader_fails++ < 24) {
    char log[1024] = "";
    glGetProgramInfoLog(prog, sizeof log, NULL, log);
    debugPrintf("[gl] program %u did not link: %s\n", prog, log);
  }
}
void ssx_gl_report(void) {
  static unsigned c0, l0;
  if (g_compiles != c0 || g_links != l0)
    debugPrintf("[gl] %u shaders compiled, %u programs linked so far (%d failed)\n", g_compiles, g_links, g_shader_fails);
  c0 = g_compiles, l0 = g_links;
  extern int dcr_gl_error_count(void) __attribute__((weak)); /* runtime/source/gl_mesa.c */
  static int e0;
  int e = dcr_gl_error_count ? dcr_gl_error_count() : 0;
  if (e != e0)
    debugPrintf("[gl] %d GL errors so far (%d in the last 10 s)\n", e, e - e0);
  e0 = e;
  void ssx_shadercache_report(void); /* ssx_shadercache.c */
  ssx_shadercache_report();
  /* how often the game moves its context (GameRender::AcquireThread): what
   * Mesa's glthread would have to wait out each time */
  extern volatile unsigned g_ssx_make_current, g_ssx_frames_presented;
  static unsigned m0, f0;
  unsigned m = g_ssx_make_current, f = g_ssx_frames_presented;
  if (f != f0)
    debugPrintf("[egl] %u eglMakeCurrent calls in %u frames\n", m - m0, f - f0);
  m0 = m, f0 = f;
  void ssx_gl_finish_report(void);
  ssx_gl_finish_report();
  /* diagnostic (mesa32 nouveau_fence.c): the GPU waits that blocked, per caller */
  extern int nouveau_fence_wait_report(char *buf, int size) __attribute__((weak));
  if (nouveau_fence_wait_report) {
    char line[1024];
    if (nouveau_fence_wait_report(line, sizeof line) > 0)
      debugPrintf("[fence] blocking waits (offset from nouveau_fence_wait, outer: n, time):%s\n", line);
  }
}

uintptr_t ssx_gl_wrap2(const char *name, uintptr_t real);
uintptr_t ssx_globjs_wrap(const char *name, uintptr_t real); /* ssx_globjs.c: [debug] profile only */
uintptr_t ssx_gldefer_wrap(const char *name, uintptr_t real); /* ssx_gldefer.c: always */
uintptr_t port_gl_wrap(const char *name, uintptr_t real) {
  /* glDelete* made with no context current wait for the next eglMakeCurrent;
   * the profiler's counters (if on) wrap that wrapper */
  uintptr_t wd = ssx_gldefer_wrap(name, real);
  if (wd) {
    uintptr_t w0 = dcr_config()->profile ? ssx_globjs_wrap(name, wd) : 0;
    return w0 ? w0 : wd;
  }
  if (dcr_config()->profile) {
    uintptr_t w0 = ssx_globjs_wrap(name, real);
    if (w0)
      return w0;
  }
  uintptr_t w2 = ssx_gl_wrap2(name, real);
  if (w2)
    return w2;
  if (!strcmp(name, "glCompileShader"))
    return r_compile = (fn_u)real, (uintptr_t)w_compile;
  if (!strcmp(name, "glLinkProgram"))
    return r_link = (fn_u)real, (uintptr_t)w_link;
  if (!strcmp(name, "glViewport"))
    return r_viewport = (fn_viewport)real, (uintptr_t)w_viewport;
  if (!strcmp(name, "glScissor"))
    return r_scissor = (fn_viewport)real, (uintptr_t)w_scissor;
  if (!strcmp(name, "glBindFramebuffer"))
    return r_bindfb = (fn_bindfb)real, (uintptr_t)w_bindfb;
  if (!strcmp(name, "glFramebufferTexture2D"))
    return r_fbtex = (fn_fbtex)real, (uintptr_t)w_fbtex;
  return 0;
}

/* Vertex attribute formats the game uses (each distinct one logged once). */
typedef void (*fn_vap)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
static fn_vap r_vap;
static struct {
  GLint size;
  GLenum type;
  GLboolean norm;
} g_vap_seen[48];
static int g_nvap;
static void w_vap(GLuint idx, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void *ptr) {
  int i = 0;
  for (; i < g_nvap; i++)
    if (g_vap_seen[i].size == size && g_vap_seen[i].type == type && g_vap_seen[i].norm == norm)
      break;
  if (i == g_nvap && g_nvap < 48) {
    g_vap_seen[g_nvap].size = size, g_vap_seen[g_nvap].type = type, g_vap_seen[g_nvap].norm = norm;
    g_nvap++;
    debugPrintf("[gl] vertex attribute format: %d x 0x%x%s (stride %d)\n", size, type, norm ? " normalized" : "", stride);
  }
  r_vap(idx, size, type, norm, stride, ptr);
}
/* glFinish: the game calls it once a frame (through EA::Graphics::OpenGLES20's
 * wrapper), which makes the render thread wait until the GPU has drawn
 * everything so far (Mesa: st_finish, a spin on the last fence), so the CPU's
 * work on a frame and the GPU's never overlap: a frame costs the sum of the
 * two. At 720 that fit in 16.7 ms; at 1080 (the GPU's part 2.25 times the
 * pixels) it did not, with the GPU only ~45% busy (hardware, 2026-11-17: the
 * profile had the render thread 13% in that spin; Mesa's fence waits, counted
 * per caller, were all this). GL needs no glFinish for correct results (a read
 * back such as glReadPixels waits by itself). [performance] gl_finish:
 * frame (the default): the commands are sent on and the CPU waits only for
 * the GPU to have finished the frame before the last (a fence each call), so
 * the GPU draws one frame while the CPU prepares the next, and the CPU is never
 * more than a frame ahead (input to picture at most one frame later than the
 * game's own way); flush: no wait at all (the swap chain's three buffers are
 * the only limit); keep: the game's glFinish. */
typedef void (*fn_v)(void);
static fn_v r_finish;
static unsigned g_finishes, g_fence_waits;
static u64 g_fence_wait_ticks;
static const char *finish_mode(void) {
  static const char *const k[] = {"one frame in flight", "sent on as glFlush", "kept"};
  int m = dcr_config()->gl_finish;
  return k[m >= 0 && m <= 2 ? m : 0];
}
static void w_finish(void) {
  static uint32_t seen[4];
  static int nseen;
  uintptr_t lr = (uintptr_t)__builtin_return_address(0), base = (uintptr_t)g_mod_game.load_virtbase;
  uint32_t off = lr >= base && lr < base + g_mod_game.load_size ? (uint32_t)(lr - base) : 0;
  int i = 0;
  for (; i < nseen && seen[i] != off; i++)
    ;
  if (i == nseen && nseen < 4) {
    seen[nseen++] = off;
    debugPrintf("[gl] glFinish from libgame+0x%x (frame %u): %s\n", off, g_ssx_frames_presented, finish_mode());
  }
  g_finishes++;
  const int mode = dcr_config()->gl_finish;
  if (mode == 2) {
    r_finish();
    return;
  }
  if (mode == 1) {
    glFlush();
    return;
  }
  static GLsync prev;
  GLsync now = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
  glFlush();
  if (prev) {
    if (glClientWaitSync(prev, 0, 0) == GL_TIMEOUT_EXPIRED) {
      const u64 t0 = armGetSystemTick();
      glClientWaitSync(prev, 0, 100000000ull); /* 100 ms at most */
      g_fence_waits++, g_fence_wait_ticks += armGetSystemTick() - t0;
    }
    glDeleteSync(prev);
  }
  prev = now;
}
void ssx_gl_finish_report(void) {
  static unsigned f0, w0;
  static u64 t0;
  if (g_finishes != f0)
    debugPrintf("[gl] %u glFinish calls in the last 10 s (%s; %u waited for the frame before, %.1f ms in all)\n",
                g_finishes - f0, finish_mode(), g_fence_waits - w0, armTicksToNs(g_fence_wait_ticks - t0) / 1e6);
  f0 = g_finishes, w0 = g_fence_waits, t0 = g_fence_wait_ticks;
}

uintptr_t ssx_gl_wrap2(const char *name, uintptr_t real) {
  if (!strcmp(name, "glVertexAttribPointer"))
    return r_vap = (fn_vap)real, (uintptr_t)w_vap;
  if (!strcmp(name, "glFinish"))
    return r_finish = (fn_v)real, (uintptr_t)w_finish;
  return 0;
}
