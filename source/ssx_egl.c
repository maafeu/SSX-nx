/* ssx_egl.c -- javax.microedition.khronos.egl.EGL10 and
 * com.ea.blast.EglAndroidDelegate, on Mesa's EGL.
 *
 * SSX does not call libEGL to set up its context (its only libEGL import is
 * eglGetProcAddress): its native code drives EGL through Java's EGL10, the
 * object EglAndroidDelegate.GetEgl() hands it. Each EGL10 call lands here and
 * goes to the runtime's EGL layer (gl_mesa.c's b_egl*: Mesa's Switch
 * platform, with the Android-only attributes dropped and the window being
 * libnx's default NWindow). The Java EGLDisplay / EGLConfig / EGLContext /
 * EGLSurface objects hold the real handles (JObj.p).
 *
 * The game makes its context current with every GPU lock
 * (GameRender::AcquireThread), from its render thread and from its loading
 * threads in turn; Mesa lets a context move between threads that release it
 * in between, as Android's EGL does. GL calls from a thread with no current
 * context do nothing in Mesa (its no-op dispatch), as on Android. MIT.
 */
#include <EGL/egl.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "dcr_sched.h"
#include "gl_layer.h"
#include "rt_window.h"
#include "ssx.h"
#include "util.h"

volatile unsigned g_ssx_frames_presented;
volatile int g_ssx_quitting, g_ssx_swap_parked;

/* gl_mesa.c and android_ndk.c */
EGLDisplay b_eglGetDisplay(EGLNativeDisplayType d);
EGLBoolean b_eglInitialize(EGLDisplay d, EGLint *maj, EGLint *min);
EGLint b_eglGetError(void);
const char *b_eglQueryString(EGLDisplay d, EGLint name);
EGLBoolean b_eglChooseConfig(EGLDisplay d, const EGLint *attrs, EGLConfig *cfgs, EGLint cap, EGLint *num);
EGLBoolean b_eglGetConfigAttrib(EGLDisplay d, EGLConfig c, EGLint a, EGLint *v);
EGLContext b_eglCreateContext(EGLDisplay d, EGLConfig c, EGLContext share, const EGLint *attrs);
EGLBoolean b_eglDestroyContext(EGLDisplay d, EGLContext c);
EGLSurface b_eglCreateWindowSurface(EGLDisplay d, EGLConfig c, EGLNativeWindowType w, const EGLint *attrs);
EGLSurface b_eglCreatePbufferSurface(EGLDisplay d, EGLConfig c, const EGLint *attrs);
EGLBoolean b_eglDestroySurface(EGLDisplay d, EGLSurface s);
EGLBoolean b_eglMakeCurrent(EGLDisplay d, EGLSurface dr, EGLSurface rd, EGLContext c);
EGLContext b_eglGetCurrentContext(void);
EGLSurface b_eglGetCurrentSurface(EGLint w);
EGLDisplay b_eglGetCurrentDisplay(void);
EGLBoolean b_eglQuerySurface(EGLDisplay d, EGLSurface s, EGLint a, EGLint *v);
EGLBoolean b_eglQueryContext(EGLDisplay d, EGLContext c, EGLint a, EGLint *v);
EGLBoolean b_eglSwapBuffers(EGLDisplay d, EGLSurface s);
EGLBoolean b_eglSwapInterval(EGLDisplay d, EGLint i);
void *b_ANativeWindow_fromSurface(void *env, void *surface);

#define C_DISPLAY "javax/microedition/khronos/egl/EGLDisplay"
#define C_CONFIG  "javax/microedition/khronos/egl/EGLConfig"
#define C_CONTEXT "javax/microedition/khronos/egl/EGLContext"
#define C_SURFACE "javax/microedition/khronos/egl/EGLSurface"

static JObj *g_no_display, *g_no_context, *g_no_surface, *g_egl10;
static EGLSurface g_window_surface; /* the window's (the game makes one) */

void ssx_egl_size(int *w, int *h) {
  int ww = 1280, wh = 720;
  dcr_window_size(&ww, &wh);
  if (w)
    *w = ww;
  if (h)
    *h = wh;
}

static void init_objects(void) {
  if (g_egl10)
    return;
  g_egl10 = jni_singleton("javax/microedition/khronos/egl/EGL10");
  g_no_display = jni_new(C_DISPLAY);
  g_no_display->immortal = 1;
  g_no_context = jni_new(C_CONTEXT);
  g_no_context->immortal = 1;
  g_no_surface = jni_new(C_SURFACE);
  g_no_surface->immortal = 1;
}

/* One Java object per real handle (the game compares them, and keeps them). */
#define MAX_WRAP 64
static struct {
  void *real;
  JObj *obj;
} g_wrap[MAX_WRAP];
static int g_nwrap;
static Mutex g_wrap_lock;

static JObj *wrap(const char *cls, void *real) {
  if (!real) {
    init_objects();
    return !strcmp(cls, C_DISPLAY) ? g_no_display : !strcmp(cls, C_CONTEXT) ? g_no_context
           : !strcmp(cls, C_SURFACE) ? g_no_surface : NULL;
  }
  mutexLock(&g_wrap_lock);
  for (int i = 0; i < g_nwrap; i++)
    if (g_wrap[i].real == real && !strcmp(g_wrap[i].obj->cls->name, cls)) {
      JObj *o = g_wrap[i].obj;
      mutexUnlock(&g_wrap_lock);
      return o;
    }
  JObj *o = jni_new(cls);
  o->immortal = 1;
  o->p = real;
  if (g_nwrap < MAX_WRAP)
    g_wrap[g_nwrap].real = real, g_wrap[g_nwrap++].obj = o;
  mutexUnlock(&g_wrap_lock);
  return o;
}

static void *real_of(const void *o) { return o ? ((const JObj *)o)->p : NULL; }

static jint *int_array(const void *o, jsize *len) {
  const JObj *a = o;
  if (!a || a->kind != JK_ARRAY) {
    if (len)
      *len = 0;
    return NULL;
  }
  if (len)
    *len = a->a.len;
  return (jint *)a->a.data;
}

/* ------------------------------------------------------------ EglAndroidDelegate */
JNI_H_DECL(ssx_egl_get_egl) {
  init_objects();
  return jv_l(jni_retain(g_egl10));
}
JNI_H_DECL(ssx_egl_get_surface) { return jv_l(jni_retain(jni_singleton("android/view/SurfaceHolder"))); }
JNI_H_DECL(ssx_egl_no_context) {
  init_objects();
  return jv_l(g_no_context);
}
JNI_H_DECL(ssx_egl_no_display) {
  init_objects();
  return jv_l(g_no_display);
}
JNI_H_DECL(ssx_egl_no_surface) {
  init_objects();
  return jv_l(g_no_surface);
}

/* ------------------------------------------------------------ EGL10 */
static void put_configs(JObj *arr, const EGLConfig *cfgs, EGLint n) {
  if (!arr || arr->kind != JK_ARRAY)
    return;
  JObj **slot = (JObj **)arr->a.data;
  for (EGLint i = 0; i < n && i < arr->a.len; i++) {
    JObj *o = wrap(C_CONFIG, cfgs[i]);
    if (slot[i] != o) {
      if (slot[i])
        jni_release(slot[i]);
      slot[i] = jni_retain(o);
    }
  }
}

static unsigned g_make_current_fails;
volatile unsigned g_ssx_make_current; /* eglMakeCurrent calls (ssx_gl.c reports the rate) */

JNI_H_DECL(ssx_egl10_call) {
  const char *n = m->name;
  init_objects();
  if (!strcmp(n, "eglGetDisplay"))
    return jv_l(wrap(C_DISPLAY, b_eglGetDisplay(EGL_DEFAULT_DISPLAY)));
  if (!strcmp(n, "eglInitialize")) {
    EGLint maj = 0, min = 0;
    EGLBoolean ok = b_eglInitialize(real_of(a[0].l), &maj, &min);
    jsize len;
    jint *v = int_array(a[1].l, &len);
    if (v && len >= 2)
      v[0] = maj, v[1] = min;
    debugPrintf("[egl] eglInitialize -> %d (EGL %d.%d)\n", ok, maj, min);
    return jv_z(ok);
  }
  if (!strcmp(n, "eglTerminate"))
    return jv_z(1); /* the display stays: the game's next start reuses it */
  if (!strcmp(n, "eglQueryString")) {
    const char *s = b_eglQueryString(real_of(a[0].l), a[1].i);
    return jv_l(jni_str(s ? s : ""));
  }
  if (!strcmp(n, "eglGetConfigs")) {
    JObj *arr = a[1].l;
    EGLint cap = a[2].i, num = 0;
    EGLConfig cfgs[64];
    if (cap > 64)
      cap = 64;
    EGLBoolean ok = eglGetConfigs(real_of(a[0].l), arr ? cfgs : NULL, arr ? cap : 0, &num);
    if (ok && arr)
      put_configs(arr, cfgs, num);
    jsize len;
    jint *v = int_array(a[3].l, &len);
    if (v && len >= 1)
      v[0] = num;
    return jv_z(ok);
  }
  if (!strcmp(n, "eglChooseConfig")) {
    jsize alen;
    const jint *attrs = int_array(a[1].l, &alen);
    JObj *arr = a[2].l;
    EGLint cap = a[3].i, num = 0;
    EGLConfig cfgs[64];
    if (cap > 64)
      cap = 64;
    EGLBoolean ok = b_eglChooseConfig(real_of(a[0].l), (const EGLint *)attrs, arr ? cfgs : NULL, arr ? cap : 0, &num);
    if (ok && arr)
      put_configs(arr, cfgs, num);
    jsize len;
    jint *v = int_array(a[4].l, &len);
    if (v && len >= 1)
      v[0] = num;
    {
      char buf[256] = "";
      for (jsize i = 0; attrs && i + 1 < alen && attrs[i] != EGL_NONE && strlen(buf) < 220; i += 2)
        snprintf(buf + strlen(buf), sizeof buf - strlen(buf), " %x=%d", attrs[i], attrs[i + 1]);
      debugPrintf("[egl] eglChooseConfig(%s) -> %d, %d configs\n", buf, ok, num);
    }
    return jv_z(ok);
  }
  if (!strcmp(n, "eglGetConfigAttrib")) {
    EGLint val = 0;
    EGLBoolean ok = b_eglGetConfigAttrib(real_of(a[0].l), real_of(a[1].l), a[2].i, &val);
    jsize len;
    jint *v = int_array(a[3].l, &len);
    if (v && len >= 1)
      v[0] = val;
    return jv_z(ok);
  }
  if (!strcmp(n, "eglCreateWindowSurface")) {
    void *win = b_ANativeWindow_fromSurface(g_jni_env, a[2].l);
    jsize alen;
    const jint *attrs = int_array(a[3].l, &alen);
    EGLSurface s = b_eglCreateWindowSurface(real_of(a[0].l), real_of(a[1].l), (EGLNativeWindowType)win,
                                            alen ? (const EGLint *)attrs : NULL);
    int w = 0, h = 0, bw = 0, bh = 0;
    ssx_egl_size(&w, &h);
    dcr_window_buffer_size(&bw, &bh);
    if (s)
      g_window_surface = s;
    debugPrintf("[egl] eglCreateWindowSurface -> %p (%dx%d drawn, buffers %dx%d), error 0x%x\n", s, w, h, bw, bh,
                s ? 0x3000 : b_eglGetError());
    return jv_l(wrap(C_SURFACE, s));
  }
  if (!strcmp(n, "eglCreatePbufferSurface")) {
    jsize alen;
    const jint *attrs = int_array(a[2].l, &alen);
    EGLSurface s = b_eglCreatePbufferSurface(real_of(a[0].l), real_of(a[1].l), alen ? (const EGLint *)attrs : NULL);
    debugPrintf("[egl] eglCreatePbufferSurface -> %p\n", s);
    return jv_l(wrap(C_SURFACE, s));
  }
  if (!strcmp(n, "eglCreatePixmapSurface")) {
    debugPrintf("[egl] eglCreatePixmapSurface: none here\n");
    return jv_l(g_no_surface);
  }
  if (!strcmp(n, "eglCreateContext")) {
    jsize alen;
    const jint *attrs = int_array(a[3].l, &alen);
    EGLContext c = b_eglCreateContext(real_of(a[0].l), real_of(a[1].l), real_of(a[2].l),
                                      alen ? (const EGLint *)attrs : NULL);
    debugPrintf("[egl] eglCreateContext -> %p\n", c);
    if (c && dcr_config()->glthread) {
      /* before the context is first made current (egl_switch.c) */
      int ok = rt_egl_start_glthread(real_of(a[0].l), c);
      debugPrintf("[gl] glthread %s: the game's GL calls are recorded on its threads and run by Mesa's worker\n",
                  ok ? "on" : "NOT available");
    }
    return jv_l(wrap(C_CONTEXT, c));
  }
  if (!strcmp(n, "eglMakeCurrent")) {
    EGLBoolean ok = b_eglMakeCurrent(real_of(a[0].l), real_of(a[1].l), real_of(a[2].l), real_of(a[3].l));
    g_ssx_make_current++;
    if (ok && real_of(a[3].l)) {
      /* the thread that draws now (ssx_scopecache.c caches for it alone) */
      extern volatile uintptr_t g_ssx_gl_thread;
      uintptr_t ssx_thread_key(void);
      g_ssx_gl_thread = ssx_thread_key();
      /* the deletes other threads made without the context (ssx_gldefer.c) */
      void ssx_gldefer_flush(void);
      ssx_gldefer_flush();
    }
    if (!ok && g_make_current_fails++ < 16)
      debugPrintf("[egl] eglMakeCurrent(%p, %p) failed: 0x%x\n", real_of(a[1].l), real_of(a[3].l), b_eglGetError());
    return jv_z(ok);
  }
  if (!strcmp(n, "eglSwapBuffers")) {
    if (g_ssx_quitting) {
      /* the game asked to quit and the window is about to close (ssx_boot.c):
       * no frame may reach it, so its render thread stays here */
      g_ssx_swap_parked = 1;
      for (;;)
        svcSleepThread(1000000000ll);
    }
    EGLBoolean ok = b_eglSwapBuffers(real_of(a[0].l), real_of(a[1].l));
    g_ssx_frames_presented++;
    return jv_z(ok);
  }
  if (!strcmp(n, "eglQuerySurface")) {
    EGLint val = 0;
    EGLBoolean ok = b_eglQuerySurface(real_of(a[0].l), real_of(a[1].l), a[2].i, &val);
    if (ok && real_of(a[1].l) == g_window_surface && (a[2].i == EGL_WIDTH || a[2].i == EGL_HEIGHT)) {
      /* the window's buffers may be bigger than what is drawn (resolution auto: rt_window.c) */
      int w = 0, h = 0;
      ssx_egl_size(&w, &h);
      val = a[2].i == EGL_WIDTH ? w : h;
    }
    jsize len;
    jint *v = int_array(a[3].l, &len);
    if (v && len >= 1)
      v[0] = val;
    return jv_z(ok);
  }
  if (!strcmp(n, "eglQueryContext")) {
    EGLint val = 0;
    EGLBoolean ok = b_eglQueryContext(real_of(a[0].l), real_of(a[1].l), a[2].i, &val);
    jsize len;
    jint *v = int_array(a[3].l, &len);
    if (v && len >= 1)
      v[0] = val;
    return jv_z(ok);
  }
  if (!strcmp(n, "eglGetError"))
    return jv_i(b_eglGetError());
  if (!strcmp(n, "eglDestroyContext") || !strcmp(n, "eglDestroySurface")) {
    /* kept: Android lost them at a pause; here the game keeps drawing to them */
    debugPrintf("[egl] %s(%p): kept\n", n, real_of(a[1].l));
    return jv_z(1);
  }
  if (!strcmp(n, "eglGetCurrentContext"))
    return jv_l(wrap(C_CONTEXT, b_eglGetCurrentContext()));
  if (!strcmp(n, "eglGetCurrentDisplay"))
    return jv_l(wrap(C_DISPLAY, b_eglGetCurrentDisplay()));
  if (!strcmp(n, "eglGetCurrentSurface"))
    return jv_l(wrap(C_SURFACE, b_eglGetCurrentSurface(a[0].i)));
  if (!strcmp(n, "eglWaitGL") || !strcmp(n, "eglWaitNative") || !strcmp(n, "eglCopyBuffers"))
    return jv_z(1);
  debugPrintf("[egl] unhandled EGL10.%s%s\n", n, m->sig);
  return jv_none();
}

/* Mesa's glthread worker ([performance] glthread), once when it starts: a
 * guest thread on cores 0-2 at the frame threads' priority (ssx_sched.c), so
 * the render thread's GL work is not queued behind a job thread. */
void switch_egl_glthread_hook(void) {
  dcr_sched_guest(CUR_THREAD_HANDLE);
  Result rc = svcSetThreadPriority(CUR_THREAD_HANDLE, 58);
  debugPrintf("[gl] glthread worker started (cores 0-2, priority 58%s)\n", R_SUCCEEDED(rc) ? "" : ": refused");
}
