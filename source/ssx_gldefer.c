/* ssx_gldefer.c -- a glDelete* made on a thread with no GL context current is
 * done at the next eglMakeCurrent instead of being lost.
 *
 * The game has one GL context and hands it between its threads with
 * eglMakeCurrent (~5 times a frame). A GL call made on a thread that does not
 * hold the context at that moment goes to Mesa's no-op dispatch: nothing
 * happens. Console runs found one 1280x720 texture (3,840 KB of GPU memory)
 * left behind at every drop load and Restart: created through glTexImage2D
 * (a stack trace through _mesa_TexImage2D, st_TexImage, nvc0_miptree_create),
 * while the game's glGen/glDelete calls balanced. After ~21 loads the heap
 * ran out and the game closed (session bo1, 2026-11-17).
 *
 * Here each delete checks for a current context; without one the names are
 * queued, and the thread that next makes the context current deletes them
 * (ssx_egl.c calls ssx_gldefer_flush). The names stay taken until then, so
 * Mesa cannot hand them out again in between. Always on: one
 * eglGetCurrentContext per delete call. MIT.
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "bionic_pthread.h"
#include "util.h"

typedef void (*fn_del)(GLsizei, const GLuint *);
typedef void (*fn_del1)(GLuint);
EGLContext b_eglGetCurrentContext(void); /* gl_mesa.c / gl_null.c */

enum { D_BUF, D_TEX, D_FBO, D_RBO, D_VAO, D_PROG, D_SHADER, D_N };
static const char *const k_name[D_N] = {"buffers", "textures", "framebuffers", "renderbuffers",
                                        "vertex arrays", "programs", "shaders"};
static fn_del r_del[D_N];   /* the array forms */
static fn_del1 r_del1[D_N]; /* glDeleteProgram, glDeleteShader */

static Mutex g_mx;
static GLuint *g_q[D_N];
static int g_qn[D_N], g_qcap[D_N];
static volatile int g_pending;
/* for the report: names queued and deleted later, names dropped (no memory
 * for the queue), and the first thread seen deleting without a context */
static uint32_t g_deferred[D_N], g_dropped[D_N];
static char g_first_thread[D_N][16];
static GLuint g_first_name[D_N];

static void defer(int k, GLsizei n, const GLuint *ids) {
  mutexLock(&g_mx);
  for (GLsizei i = 0; i < n; i++) {
    if (!ids[i])
      continue;
    if (g_qn[k] == g_qcap[k]) {
      const int cap = g_qcap[k] ? g_qcap[k] * 2 : 64;
      GLuint *q = realloc(g_q[k], (size_t)cap * sizeof *q);
      if (!q) {
        g_dropped[k]++;
        continue;
      }
      g_q[k] = q, g_qcap[k] = cap;
    }
    g_q[k][g_qn[k]++] = ids[i];
    if (!g_deferred[k]++) {
      BThread *t = b_thread_self();
      snprintf(g_first_thread[k], sizeof g_first_thread[k], "%.15s", t && t->name[0] ? t->name : "?");
      g_first_name[k] = ids[i];
    }
  }
  __atomic_store_n(&g_pending, 1, __ATOMIC_RELEASE);
  mutexUnlock(&g_mx);
}

static inline int have_context(void) { return b_eglGetCurrentContext() != EGL_NO_CONTEXT; }

#define DEL_N(K, w)                                                                                 \
  static void w(GLsizei n, const GLuint *ids) {                                                     \
    if (n <= 0 || !ids || have_context())                                                           \
      r_del[K](n, ids);                                                                             \
    else                                                                                            \
      defer(K, n, ids);                                                                             \
  }
#define DEL_1(K, w)                                                                                 \
  static void w(GLuint id) {                                                                        \
    if (!id || have_context())                                                                      \
      r_del1[K](id);                                                                                \
    else                                                                                            \
      defer(K, 1, &id);                                                                             \
  }
DEL_N(D_BUF, w_del_buf)
DEL_N(D_TEX, w_del_tex)
DEL_N(D_FBO, w_del_fbo)
DEL_N(D_RBO, w_del_rbo)
DEL_N(D_VAO, w_del_vao)
DEL_1(D_PROG, w_del_prog)
DEL_1(D_SHADER, w_del_shader)

/* Every other GL call made with no context lands in Mesa's no-op dispatch
 * (src/mapi/table.c: noop_generic, which does not pass the function's name).
 * Counted, to show whether anything but deletes is lost that way. Mesa's own
 * handler (main/context.c) only acts when a context is current, which never
 * reaches a no-op entry here; ours replaces it after the first make-current. */
void _glapi_set_nop_handler(void (*func)(const char *name));
static volatile uint32_t g_nop_calls;
static char g_nop_thread[16];
static void nop_handler(const char *name) {
  (void)name;
  if (!__atomic_fetch_add(&g_nop_calls, 1, __ATOMIC_RELAXED)) {
    BThread *t = b_thread_self();
    snprintf(g_nop_thread, sizeof g_nop_thread, "%.15s", t && t->name[0] ? t->name : "?");
  }
}

/* After a successful eglMakeCurrent with a context, on that thread. */
void ssx_gldefer_flush(void) {
  static int nop_set;
  if (!nop_set) {
    nop_set = 1;
    _glapi_set_nop_handler(nop_handler);
  }
  if (!__atomic_load_n(&g_pending, __ATOMIC_ACQUIRE))
    return;
  mutexLock(&g_mx);
  for (int k = 0; k < D_N; k++) {
    if (!g_qn[k])
      continue;
    if (r_del[k])
      r_del[k](g_qn[k], g_q[k]);
    else if (r_del1[k])
      for (int i = 0; i < g_qn[k]; i++)
        r_del1[k](g_q[k][i]);
    g_qn[k] = 0;
  }
  __atomic_store_n(&g_pending, 0, __ATOMIC_RELEASE);
  mutexUnlock(&g_mx);
}

/* ssx_gl.c's port_gl_wrap asks here first (before the profiler's counters);
 * 0: not ours. */
uintptr_t ssx_gldefer_wrap(const char *name, uintptr_t real) {
  static const struct {
    const char *name;
    int kind, one;
    uintptr_t w;
  } k[] = {
      {"glDeleteBuffers", D_BUF, 0, (uintptr_t)w_del_buf},
      {"glDeleteTextures", D_TEX, 0, (uintptr_t)w_del_tex},
      {"glDeleteFramebuffers", D_FBO, 0, (uintptr_t)w_del_fbo},
      {"glDeleteRenderbuffers", D_RBO, 0, (uintptr_t)w_del_rbo},
      {"glDeleteVertexArrays", D_VAO, 0, (uintptr_t)w_del_vao},
      {"glDeleteVertexArraysOES", D_VAO, 0, (uintptr_t)w_del_vao},
      {"glDeleteProgram", D_PROG, 1, (uintptr_t)w_del_prog},
      {"glDeleteShader", D_SHADER, 1, (uintptr_t)w_del_shader},
  };
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strcmp(name, k[i].name)) {
      if (k[i].one)
        r_del1[k[i].kind] = (fn_del1)real;
      else
        r_del[k[i].kind] = (fn_del)real;
      return k[i].w;
    }
  return 0;
}

/* From the 10 s report: a line whenever the counts have changed. */
void ssx_gldefer_report(void) {
  static uint32_t last, last_nop;
  uint32_t sum = 0;
  for (int k = 0; k < D_N; k++)
    sum += g_deferred[k] + g_dropped[k];
  const uint32_t nop = g_nop_calls;
  if (nop != last_nop) {
    last_nop = nop;
    debugPrintf("[gl] %u other GL calls made with no context (lost; the first on thread %s)\n", (unsigned)nop,
                g_nop_thread);
  }
  if (sum == last)
    return;
  last = sum;
  char line[512];
  int n = snprintf(line, sizeof line, "[gl] deletes made with no context on the thread, done at the next "
                                      "eglMakeCurrent:");
  for (int k = 0; k < D_N && n < (int)sizeof line - 80; k++)
    if (g_deferred[k] || g_dropped[k])
      n += snprintf(line + n, sizeof line - n, " %s %u%s (first: %u, thread %s);", k_name[k],
                    (unsigned)g_deferred[k], g_dropped[k] ? " +dropped" : "", (unsigned)g_first_name[k],
                    g_first_thread[k]);
  debugPrintf("%s\n", line);
}
