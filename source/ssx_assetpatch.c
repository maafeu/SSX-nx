/* ssx_assetpatch.c -- small edits to two of the game's text files, made in
 * memory as the game reads them from the APK (ssx_assets.c). Nothing is
 * written anywhere, and the player's APK stays as it is.
 *
 * From the PS Vita port's data tool (datatool/ssxdata/patches.py, MIT), which
 * makes the same edits to the files it extracts:
 *
 * common/config/memory.cfg -- the size of each of EA's MemoryFramework heaps.
 *   Android's file asks for ~573 MB, which works on a phone only because
 *   Android backs pages lazily. Here, as on the Vita, the heaps come out of
 *   the process heap at start-up and every page is real: SSX had 817 MB of
 *   its 992 MB heap in use at the first frame and 909 MB in a race (Mesa's
 *   GPU buffers come out of the same heap). The sizes below are the Vita's,
 *   measured at peak use in long sessions, with a margin, for the five where
 *   the Vita saw nearly nothing used or the saving is large; the rest keep
 *   Android's sizes (below). ~155 MB freed.
 *   [performance] trim_heaps = false keeps Android's sizes.
 *
 * common/fe/lua/packages/nav.lua -- the menus' navigation (a Lua state
 *   chart) ran a full Lua garbage collection after every navigation event:
 *   15-60 ms each on the Vita, several per screen change, a dropped frame or
 *   more each time at 60 fps. Lua's incremental collector, paced by
 *   allocation, keeps up on its own. [performance] menu_full_gc = true keeps
 *   the game's own collection.
 *
 * mobile/scrape/main.scrape: the 3D scene's render target becomes [display]
 * render_resolution (patch_main_scrape). The PS Vita port rendered it at
 * 960x544 for its screen; the game's own is 1280x720.
 *
 * Each edit first checks that the text is what it expects (the APK version
 * 0.0.8430's); if not, the file is served unchanged and debug.log says so.
 * MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dcr_config.h"
#include "util.h"

#define MEMORY_CFG "common/config/memory.cfg"
#define NAV_LUA "common/fe/lua/packages/nav.lua"
#define MAIN_SCRAPE "mobile/scrape/main.scrape"

/* Heap -> size here. Comments: Android's size; the Vita's, and what it saw. */
static const struct {
  const char *heap, *size;
} k_heaps[] = {
    {"GAMEWORLD_GEN", "4M"},        /* 20M; Vita 4M: ~10 KB used in menus, races and the tutorial */
    {"STL_GEN", "4M"},              /* 10M; Vita 2M: at most 0.6M in a long session */
    {"FE_SFGFX_ASCRIPT", "4M"},     /* 12M; Vita 2M: at most 0.1M in a long session */
    {"MEMORY_RESIDENT_GEN", "1M"},  /* 5M; Vita 1M: never used through boot, menus and race loads */
    {"RENDER_GEN", "128M"},         /* 250M; Vita 52M (48M ran out after the tutorial) with ~27 MB less texture */
};
/* Unchanged: AUDIODATA_GEN 60M (the Vita's 36M is for its stereo ambience
 * bank), FE_SFGFX_GEN_A 35M, GAMEPLAY_GEN 20M, GAMEWORLD_SLOTALLOC 25M,
 * GLOBAL_GEN 15M and the rest (the Vita kept them, or the saving is small).
 * Also unchanged, after two emulator crashes at the first movie with them
 * at the Vita's sizes plus a margin (emu27, emu30: a NULL, no OOM report):
 * FE_GEN 12M, FE_SFGFX_GEN_B 5M, FE_SFGFX_REN_SBA 10M, FE_SFGFX_RENDER 10M,
 * FE_UX_GEN 5M, GLOBAL_ASSETTMP 35M. debug.log's [heaps] lines (ssx_patch2.c)
 * give each heap's peak, to size them from this port's own numbers. */

static int is_space(char c) { return c == ' ' || c == '\t'; }
static int is_word(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

typedef struct {
  char *p;
  size_t n, cap;
} Buf;

static int put(Buf *b, const char *s, size_t n) {
  if (b->n + n + 1 > b->cap) {
    size_t cap = b->cap ? b->cap * 2 : 4096;
    while (cap < b->n + n + 1)
      cap *= 2;
    char *q = realloc(b->p, cap);
    if (!q)
      return -1;
    b->p = q, b->cap = cap;
  }
  memcpy(b->p + b->n, s, n);
  b->n += n;
  b->p[b->n] = 0;
  return 0;
}

static int puts_(Buf *b, const char *s) { return put(b, s, strlen(s)); }

/* One line, without its ending: "AddAllocator[.android] NAME KIND [ ... size=V ... ]".
 * Returns the heap's index in k_heaps and the span of V, or -1. */
static int allocator_line(const char *s, size_t n, size_t *vi, size_t *vj) {
  size_t i = 0;
  while (i < n && is_space(s[i]))
    i++;
  static const char kw[] = "AddAllocator";
  if (n - i < sizeof kw - 1 || memcmp(s + i, kw, sizeof kw - 1))
    return -1;
  i += sizeof kw - 1;
  if (i < n && s[i] == '.') { /* a platform: only Android's (none = every platform) */
    if (n - i < 8 || memcmp(s + i, ".android", 8) || (i + 8 < n && is_word(s[i + 8])))
      return -1;
    i += 8;
  }
  if (i >= n || !is_space(s[i]))
    return -1;
  while (i < n && is_space(s[i]))
    i++;
  size_t h0 = i;
  while (i < n && is_word(s[i]))
    i++;
  size_t hl = i - h0;
  int heap = -1;
  for (unsigned k = 0; k < sizeof k_heaps / sizeof k_heaps[0]; k++)
    if (strlen(k_heaps[k].heap) == hl && !memcmp(s + h0, k_heaps[k].heap, hl))
      heap = (int)k;
  if (heap < 0)
    return -1;
  while (i < n && is_space(s[i]))
    i++;
  while (i < n && is_word(s[i])) /* the allocator's kind */
    i++;
  while (i < n && is_space(s[i]))
    i++;
  if (i >= n || s[i] != '[')
    return -1;
  /* "size" then '=' then the value, which may hold {{cond}?a:b}, nested */
  for (; i + 4 <= n; i++)
    if (!memcmp(s + i, "size", 4) && (i == 0 || !is_word(s[i - 1])))
      break;
  if (i + 4 > n)
    return -1;
  i += 4;
  while (i < n && is_space(s[i]))
    i++;
  if (i >= n || s[i] != '=')
    return -1;
  i++;
  while (i < n && is_space(s[i]))
    i++;
  size_t j = i;
  int depth = 0;
  for (; j < n; j++) {
    if (s[j] == '{')
      depth++;
    else if (s[j] == '}')
      depth--;
    else if (depth <= 0 && (s[j] == ',' || s[j] == ']' || is_space(s[j])))
      break;
  }
  if (j == i)
    return -1;
  *vi = i, *vj = j;
  return heap;
}

static char *patch_memory_cfg(const char *src, size_t len, size_t *out_len) {
  Buf b = {0};
  unsigned done = 0;
  size_t at = 0;
  while (at < len) {
    size_t e = at;
    while (e < len && src[e] != '\n' && src[e] != '\r')
      e++;
    size_t vi, vj;
    int heap = allocator_line(src + at, e - at, &vi, &vj);
    size_t end = e; /* the line's ending is copied with the next stretch */
    if (heap >= 0) {
      if (put(&b, src + at, vi) || put(&b, k_heaps[heap].size, strlen(k_heaps[heap].size)) ||
          put(&b, src + at + vj, end - at - vj))
        goto fail;
      done |= 1u << heap;
    } else if (put(&b, src + at, end - at)) {
      goto fail;
    }
    at = end;
    while (at < len && (src[at] == '\n' || src[at] == '\r')) {
      if (put(&b, src + at, 1))
        goto fail;
      at++;
    }
  }
  const unsigned all = (1u << (sizeof k_heaps / sizeof k_heaps[0])) - 1;
  if (done != all) {
    for (unsigned k = 0; k < sizeof k_heaps / sizeof k_heaps[0]; k++)
      if (!(done & (1u << k)))
        debugPrintf("[assetpatch] memory.cfg: heap %s not found: the file is left as it is\n", k_heaps[k].heap);
    goto fail;
  }
  *out_len = b.n;
  return b.p;
fail:
  free(b.p);
  return NULL;
}

/* "function P:collect()" ... the next line that is just "end": the body
 * becomes a comment. */
static char *patch_nav_lua(const char *src, size_t len, size_t *out_len) {
  static const char head[] = "function P:collect()";
  const char *h = NULL;
  for (size_t i = 0; i + sizeof head - 1 <= len; i++)
    if (!memcmp(src + i, head, sizeof head - 1) && (i == 0 || src[i - 1] == '\n')) {
      h = src + i;
      break;
    }
  if (!h) {
    debugPrintf("[assetpatch] nav.lua: P:collect() not found: the file is left as it is\n");
    return NULL;
  }
  const char *body = h + sizeof head - 1;
  const char *nl = (body < src + len && *body == '\r') ? "\r\n" : "\n";
  /* the first line that is exactly "end" */
  const char *p = body, *end_at = NULL;
  while (p < src + len) {
    const char *ln = memchr(p, '\n', (size_t)(src + len - p));
    if (!ln)
      break;
    const char *s = ln + 1;
    if (src + len - s >= 3 && !memcmp(s, "end", 3) &&
        (s + 3 == src + len || s[3] == '\r' || s[3] == '\n')) {
      end_at = s;
      break;
    }
    p = s;
  }
  if (!end_at || end_at - body > 2048) {
    debugPrintf("[assetpatch] nav.lua: the end of P:collect() not found: the file is left as it is\n");
    return NULL;
  }
  Buf b = {0};
  if (put(&b, src, (size_t)(body - src)) || put(&b, nl, strlen(nl)) ||
      puts_(&b, "\t-- SSX for Switch: no full collection here (as the PS Vita port);") || puts_(&b, nl) ||
      puts_(&b, "\t-- Lua's incremental collector runs as memory gets allocated") || puts_(&b, nl) ||
      put(&b, end_at, (size_t)(src + len - end_at))) {
    free(b.p);
    return NULL;
  }
  *out_len = b.n;
  return b.p;
}

/* mobile/scrape/main.scrape: the 3D scene's own render target, set in the
 * script as 1280 720 ("var texture FrameBufferTexture 1280 720 format" and its
 * depth, "var surface FrameBufferSurfaceDepth edramdepth 1280 720 D24S8"); the
 * post-effects pass then draws it to the window, scaled. Both become the
 * render resolution ([display] render_resolution). The script's other
 * targets are its own sizes (shadows, a 32x16 post-effects depth). */
static int edit_line(Buf *b, const char *src, size_t len, size_t *done, const char *key, const char *what,
                     const char *with) {
  /* the line holding key: its "what" becomes "with" */
  const char *k = NULL;
  for (size_t i = *done; i + strlen(key) <= len; i++)
    if (!memcmp(src + i, key, strlen(key))) {
      k = src + i;
      break;
    }
  if (!k)
    return -1;
  const char *eol = memchr(k, '\n', (size_t)(src + len - k));
  if (!eol)
    eol = src + len;
  const char *w = NULL;
  for (const char *p = k; p + strlen(what) <= eol; p++)
    if (!memcmp(p, what, strlen(what))) {
      w = p;
      break;
    }
  if (!w)
    return -1;
  if (put(b, src + *done, (size_t)(w - (src + *done))) || puts_(b, with))
    return -2;
  *done = (size_t)(w + strlen(what) - src);
  return 0;
}

static char *patch_main_scrape(const char *src, size_t len, size_t *out_len, int w, int h) {
  char size[32];
  snprintf(size, sizeof size, "%d %d", w, h);
  Buf b = {0};
  size_t done = 0;
  int r1 = edit_line(&b, src, len, &done, "FrameBufferTexture\t", "1280 720", size);
  int r2 = r1 ? r1 : edit_line(&b, src, len, &done, "FrameBufferSurfaceDepth", "1280 720", size);
  if (r1 || r2 || put(&b, src + done, len - done)) {
    debugPrintf("[assetpatch] main.scrape: the scene's 1280 720 target not found: the file is left as it is\n");
    free(b.p);
    return NULL;
  }
  *out_len = b.n;
  return b.p;
}

static int g_trim = -1, g_nogc = -1, g_scene_w, g_scene_h;
static void settings(void) {
  if (g_trim < 0) {
    g_trim = dcr_config()->trim_heaps;
    g_nogc = !dcr_config()->menu_full_gc;
    void ssx_render_scene_size(int *w, int *h); /* ssx_render.c */
    ssx_render_scene_size(&g_scene_w, &g_scene_h);
  }
}

int ssx_assetpatch_wants(const char *name) {
  settings();
  return (g_trim && !strcmp(name, MEMORY_CFG)) || (g_nogc && !strcmp(name, NAV_LUA)) ||
         ((g_scene_w != 1280 || g_scene_h != 720) && !strcmp(name, MAIN_SCRAPE));
}

/* The edited copy (malloc'd; *out_len its size), or NULL: serve the original. */
uint8_t *ssx_assetpatch_apply(const char *name, const uint8_t *data, uint32_t len, uint32_t *out_len) {
  size_t n = 0;
  char *r = NULL;
  if (!ssx_assetpatch_wants(name))
    return NULL;
  if (!strcmp(name, MEMORY_CFG))
    r = patch_memory_cfg((const char *)data, len, &n);
  else if (!strcmp(name, NAV_LUA))
    r = patch_nav_lua((const char *)data, len, &n);
  else if (!strcmp(name, MAIN_SCRAPE))
    r = patch_main_scrape((const char *)data, len, &n, g_scene_w, g_scene_h);
  if (r) {
    static int logged[3];
    int k = !strcmp(name, NAV_LUA) ? 1 : !strcmp(name, MAIN_SCRAPE) ? 2 : 0;
    if (!logged[k]++)
      debugPrintf("[assetpatch] %s edited (%u -> %u bytes)\n", name, (unsigned)len, (unsigned)n);
    *out_len = (uint32_t)n;
  }
  return (uint8_t *)r;
}
