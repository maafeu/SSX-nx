/* ssx_globjs.c -- [debug] profile: the game's live GL objects.
 *
 * A Restart on Extinguisher Tower left ~5 MB more of the heap in use each time
 * (mallinfo at each bake: 860, 867, 871, 877, 881 MB, the free space inside the
 * arena flat at 10 MB: memory held, not fragmentation). Each Restart rebuilds
 * the course's static meshes (GL buffers) and the snow's deformation render
 * targets, so the first question is whether GL objects outlive their use:
 * glGen* minus glDelete* per kind, and the bytes given to glBufferData and
 * glTexImage2D, reported with each bake's heap line (ssx_bakesort.c). Counting
 * only; the calls go on unchanged. MIT.
 */
#include <GLES3/gl3.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx.h"
#include "util.h"

typedef void (*fn_gen)(GLsizei, GLuint *);
typedef void (*fn_del)(GLsizei, const GLuint *);
typedef void (*fn_bufdata)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (*fn_teximage)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);

enum { K_BUF, K_TEX, K_FBO, K_RBO, K_N };
static const char *const k_name[K_N] = {"buffers", "textures", "framebuffers", "renderbuffers"};
static fn_gen r_gen[K_N];
static fn_del r_del[K_N];
static volatile int32_t g_live[K_N];
static volatile uint64_t g_buf_bytes, g_tex_bytes;
static fn_bufdata r_bufdata;
static fn_teximage r_teximage;

static int count_ids(GLsizei n, const GLuint *ids) {
  int k = 0;
  for (GLsizei i = 0; ids && i < n; i++)
    k += ids[i] != 0;
  return k;
}

#define GEN_DEL(K, gen, del)                                                                    \
  static void gen(GLsizei n, GLuint *ids) {                                                     \
    r_gen[K](n, ids);                                                                           \
    __atomic_fetch_add(&g_live[K], count_ids(n, ids), __ATOMIC_RELAXED);                        \
  }                                                                                             \
  static void del(GLsizei n, const GLuint *ids) {                                               \
    __atomic_fetch_sub(&g_live[K], count_ids(n, ids), __ATOMIC_RELAXED);                        \
    r_del[K](n, ids);                                                                           \
  }
GEN_DEL(K_BUF, w_gen_buf, w_del_buf)
GEN_DEL(K_TEX, w_gen_tex, w_del_tex)
GEN_DEL(K_FBO, w_gen_fbo, w_del_fbo)
GEN_DEL(K_RBO, w_gen_rbo, w_del_rbo)

static void w_bufdata(GLenum t, GLsizeiptr size, const void *data, GLenum usage) {
  __atomic_fetch_add(&g_buf_bytes, (uint64_t)(size > 0 ? size : 0), __ATOMIC_RELAXED);
  r_bufdata(t, size, data, usage);
}

static void w_teximage(GLenum t, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt,
                       GLenum type, const void *data) {
  const uint64_t bpp = type == GL_UNSIGNED_BYTE ? (fmt == GL_RGBA ? 4 : fmt == GL_RGB ? 3 : 1) : 4;
  __atomic_fetch_add(&g_tex_bytes, (uint64_t)(w > 0 ? w : 0) * (uint64_t)(h > 0 ? h : 0) * bpp, __ATOMIC_RELAXED);
  r_teximage(t, level, ifmt, w, h, border, fmt, type, data);
}

/* ssx_gl.c's port_gl_wrap asks here first; 0: not ours. */
uintptr_t ssx_globjs_wrap(const char *name, uintptr_t real) {
  static const struct {
    const char *name;
    int kind, del;
    uintptr_t w;
  } k[] = {
      {"glGenBuffers", K_BUF, 0, (uintptr_t)w_gen_buf},        {"glDeleteBuffers", K_BUF, 1, (uintptr_t)w_del_buf},
      {"glGenTextures", K_TEX, 0, (uintptr_t)w_gen_tex},       {"glDeleteTextures", K_TEX, 1, (uintptr_t)w_del_tex},
      {"glGenFramebuffers", K_FBO, 0, (uintptr_t)w_gen_fbo},   {"glDeleteFramebuffers", K_FBO, 1, (uintptr_t)w_del_fbo},
      {"glGenRenderbuffers", K_RBO, 0, (uintptr_t)w_gen_rbo},  {"glDeleteRenderbuffers", K_RBO, 1, (uintptr_t)w_del_rbo},
  };
  for (unsigned i = 0; i < sizeof k / sizeof k[0]; i++)
    if (!strcmp(name, k[i].name)) {
      if (k[i].del)
        r_del[k[i].kind] = (fn_del)real;
      else
        r_gen[k[i].kind] = (fn_gen)real;
      return k[i].w;
    }
  if (!strcmp(name, "glBufferData"))
    return r_bufdata = (fn_bufdata)real, (uintptr_t)w_bufdata;
  if (!strcmp(name, "glTexImage2D"))
    return r_teximage = (fn_teximage)real, (uintptr_t)w_teximage;
  return 0;
}

/* The driver's GPU memory: every nouveau buffer object is heap memory
 * (memalign) made an nvmap (libdrm_nouveau's nouveau_bo_new), and its
 * nouveau_bo_del closes the nvmap before freeing the memory. Linked with
 * -Wl,--wrap=nvMapCreate -Wl,--wrap=nvMapClose (the Makefile). Always
 * counted: two atomics per buffer. */
Result __real_nvMapCreate(NvMap *m, void *cpu_addr, u32 size, u32 align, NvKind kind, bool is_cpu_cacheable);
void __real_nvMapClose(NvMap *m);
static volatile int32_t g_maps;
static volatile uint64_t g_map_bytes;
/* live nvmaps by log2 of their size, linear (pitch: buffers, Mesa's
 * sub-allocator slabs) in [0..31], tiled (textures, render targets) in
 * [32..63] */
static volatile int32_t g_map_hist[64];

static int size_class(u32 size, NvKind kind) {
  return (31 - __builtin_clz(size ? size : 1)) + (kind != NvKind_Pitch ? 32 : 0);
}

/* The live 2 MB-and-larger ones in a table, each with the bake it was made
 * after (ssx_globjs_report counts the bakes), printed at each bake: the ones
 * a Restart leaves behind are the ones still there from an older bake. No
 * logging in here: a debugPrintf from inside nvMapCreate hung the game where
 * the boot log hands the screen over (its framebuffer is closed and the
 * window's buffers made on that path; console run, build 202611180327). */
#define BIG_MIN (2u << 20)
#define BIG_N 256
static struct {
  const void *addr;
  u32 size;
  uint16_t gen;
  uint8_t tiled;
} g_big[BIG_N];
static int g_gen, g_big_lost;
static Mutex g_big_mx;

/* Who makes them: for each tiled 2 MB+ one made after the first bake, the
 * code addresses found on the stack (this program's and libgame's; a word
 * scan, since neither keeps frame pointers: a rough backtrace, the true
 * callers among some stale words). Each stack page is checked to be mapped
 * before it is read. Printed with the table, as P+offset (this program, its
 * link address: addr2line on ssx_nx.elf) and G+offset (libgame.so's vaddr). */
#define TRACE_N 12
#define TRACE_DEPTH 48
extern char __code_start[], __rodata_start[];
static struct {
  u32 size;
  uint16_t gen;
  uint8_t n;
  uint32_t pc[TRACE_DEPTH];
} g_trace[TRACE_N];
static int g_ntrace;

static int stack_word_readable(uintptr_t a) {
  static uintptr_t ok_page;
  const uintptr_t page = a & ~(uintptr_t)0xfff;
  if (page == ok_page)
    return 1;
  MemoryInfo mi;
  u32 pi;
  if (R_FAILED(svcQueryMemory(&mi, &pi, page)) || mi.type == MemType_Unmapped || !(mi.perm & Perm_R))
    return 0;
  ok_page = page;
  return 1;
}

static void trace_here(u32 size) {
  if (g_ntrace >= TRACE_N)
    return;
  const uintptr_t p0 = (uintptr_t)__code_start, p1 = (uintptr_t)__rodata_start;
  const uintptr_t g0 = (uintptr_t)g_mod_game.load_virtbase, g1 = g0 + g_mod_game.load_size;
  uintptr_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  int n = 0;
  for (uintptr_t a = sp; a < sp + 16384 && n < TRACE_DEPTH; a += 4) {
    if (!stack_word_readable(a))
      break;
    const uintptr_t w = *(const volatile uint32_t *)a & ~(uintptr_t)1;
    if ((w >= p0 && w < p1) || (g0 && w >= g0 && w < g1))
      g_trace[g_ntrace].pc[n++] = (uint32_t)w;
  }
  g_trace[g_ntrace].size = size, g_trace[g_ntrace].gen = (uint16_t)g_gen, g_trace[g_ntrace].n = (uint8_t)n;
  g_ntrace++;
}

static void big_add(const void *addr, u32 size, NvKind kind) {
  if (size < BIG_MIN || !dcr_config()->profile)
    return;
  mutexLock(&g_big_mx);
  int i = 0;
  while (i < BIG_N && g_big[i].addr)
    i++;
  if (i < BIG_N) {
    g_big[i].addr = addr, g_big[i].size = size, g_big[i].gen = (uint16_t)g_gen;
    g_big[i].tiled = kind != NvKind_Pitch;
  } else {
    g_big_lost++;
  }
  if (kind != NvKind_Pitch && g_gen >= 1)
    trace_here(size);
  mutexUnlock(&g_big_mx);
}

static void big_remove(const void *addr, u32 size) {
  if (size < BIG_MIN || !dcr_config()->profile)
    return;
  mutexLock(&g_big_mx);
  for (int i = 0; i < BIG_N; i++)
    if (g_big[i].addr == addr) {
      g_big[i].addr = NULL;
      break;
    }
  mutexUnlock(&g_big_mx);
}

Result __wrap_nvMapCreate(NvMap *m, void *cpu_addr, u32 size, u32 align, NvKind kind, bool is_cpu_cacheable) {
  Result rc = __real_nvMapCreate(m, cpu_addr, size, align, kind, is_cpu_cacheable);
  if (R_SUCCEEDED(rc)) {
    __atomic_fetch_add(&g_maps, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_map_bytes, (uint64_t)size, __ATOMIC_RELAXED);
    __atomic_fetch_add(&g_map_hist[size_class(size, kind)], 1, __ATOMIC_RELAXED);
    big_add(cpu_addr, size, kind);
  }
  return rc; /* a failure has closed it already, through the real nvMapClose */
}

void __wrap_nvMapClose(NvMap *m) {
  if (m && m->has_init && m->cpu_addr) { /* one this program made (nvMapLoadRemote leaves cpu_addr NULL) */
    __atomic_fetch_sub(&g_maps, 1, __ATOMIC_RELAXED);
    __atomic_fetch_sub(&g_map_bytes, (uint64_t)m->size, __ATOMIC_RELAXED);
    __atomic_fetch_sub(&g_map_hist[size_class(m->size, m->kind)], 1, __ATOMIC_RELAXED);
    big_remove(m->cpu_addr, m->size);
  }
  __real_nvMapClose(m);
}

/* One line: the live objects per kind, the GPU memory held, and the bytes
 * uploaded since the last call. */
void ssx_globjs_report(const char *when) {
  static uint64_t buf0, tex0;
  const uint64_t buf = g_buf_bytes, tex = g_tex_bytes;
  debugPrintf("[globj] %s: live %s %d, %s %d, %s %d, %s %d; GPU memory %d buffers, %llu MB; since the last line "
              "glBufferData %llu KB, glTexImage2D %llu KB\n",
              when, k_name[K_BUF], (int)g_live[K_BUF], k_name[K_TEX], (int)g_live[K_TEX], k_name[K_FBO],
              (int)g_live[K_FBO], k_name[K_RBO], (int)g_live[K_RBO], (int)g_maps,
              (unsigned long long)(g_map_bytes >> 20), (unsigned long long)((buf - buf0) >> 10),
              (unsigned long long)((tex - tex0) >> 10));
  buf0 = buf, tex0 = tex;
  /* the live GPU buffers by size, and the change since the last line */
  static int32_t last[64];
  for (int tiled = 0; tiled < 2; tiled++) {
    char line[512];
    int n = snprintf(line, sizeof line, "[globj] GPU buffers by size, %s (count, change):",
                     tiled ? "tiled" : "linear");
    for (int c = 12; c < 32 && n < (int)sizeof line - 32; c++) {
      const int i = c + 32 * tiled;
      const int32_t now = g_map_hist[i];
      if (now || last[i])
        n += snprintf(line + n, sizeof line - n, " %u%s %d (%+d)", c >= 20 ? 1u << (c - 20) : 1u << (c - 10),
                      c >= 20 ? "M" : "K", (int)now, (int)(now - last[i]));
      last[i] = now;
    }
    debugPrintf("%s\n", line);
  }
  /* the live big ones, by the bake they were made after (0: start-up and
   * menus, 1: the first bake's run, ...): copied under the lock, printed
   * after it */
  static struct {
    u32 size;
    uint16_t gen;
    uint8_t tiled;
  } copy[BIG_N];
  int nb = 0;
  mutexLock(&g_big_mx);
  for (int i = 0; i < BIG_N; i++)
    if (g_big[i].addr)
      copy[nb].size = g_big[i].size, copy[nb].gen = g_big[i].gen, copy[nb].tiled = g_big[i].tiled, nb++;
  const int gen = g_gen++, lost = g_big_lost;
  mutexUnlock(&g_big_mx);
  for (int g = 0; g <= gen; g++) {
    char line[512];
    int n = snprintf(line, sizeof line, "[nvmap] live 2 MB+ buffers made after bake %d:", g), k = 0;
    for (int i = 0; i < nb && n < (int)sizeof line - 24; i++)
      if (copy[i].gen == g)
        n += snprintf(line + n, sizeof line - n, " %u%c", (unsigned)(copy[i].size >> 10), copy[i].tiled ? 'T' : 'L'),
            k++;
    if (k)
      debugPrintf("%s (KB; T tiled, L linear)%s\n", line, lost ? " (table full: some not counted)" : "");
  }
  /* the stack traces of the tiled ones made since the last report */
  static int traced;
  const uintptr_t p0 = (uintptr_t)__code_start, g0 = (uintptr_t)g_mod_game.load_virtbase;
  const uintptr_t g1 = g0 + g_mod_game.load_size;
  for (; traced < g_ntrace; traced++) {
    char line[1024];
    int n = snprintf(line, sizeof line, "[nvmap] tiled %u KB made after bake %d, code on its stack:",
                     (unsigned)(g_trace[traced].size >> 10), (int)g_trace[traced].gen);
    for (int i = 0; i < g_trace[traced].n && n < (int)sizeof line - 16; i++) {
      const uintptr_t w = g_trace[traced].pc[i];
      const int in_game = g0 && w >= g0 && w < g1; /* this program can lie above libgame */
      n += snprintf(line + n, sizeof line - n, in_game ? " G+%x" : " P+%x", (unsigned)(in_game ? w - g0 : w - p0));
    }
    debugPrintf("%s\n", line);
  }
}
