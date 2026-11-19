/* ssx_bakesort.c -- the 0.7 s frame at every drop start and Restart.
 *
 * Measured on the console (prof-long.txt, [debug] profile): in that frame the
 * render thread is in StaticInstancesRenderObj::BakeRNAMeshToGPU, which bakes
 * the course's static instances (trees, props) into meshes and sorts each
 * mesh's triangles by height:
 *
 *   qsort(indices, triangles, 6, BakeComparator)
 *
 * BakeComparator reads both triangles' three vertex positions through
 * ValueFromStream (half floats from the locked vertex stream: stream, stride
 * and position element in three globals the bake sets before the call), takes
 * each triangle's highest y, (int16)(int) of it, and returns 1 when the second
 * triangle's is higher, -1 otherwise (never 0: ties come in any order). Six
 * vertex decodes per comparison, ~n log n comparisons: ~80% of the render
 * thread's time in that frame, while the simulation thread yields waiting for
 * it.
 *
 * libgame's qsort is b_qsort here. For that comparator (and only it) it takes
 * each triangle's key once (the same ValueFromStream calls and the same max
 * and conversions as BakeComparator), sorts the triangles by key, highest
 * first, with a stable radix sort in cached memory, and writes them back once.
 * The result is an order BakeComparator agrees with (ties keep the mesh's own
 * order instead of qsort's arbitrary one). Every other qsort is newlib's.
 * The code is checked first (the comparator's and ValueFromStream's first
 * instructions); a mismatch keeps qsort for everything. MIT.
 */
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx.h"
#include "util.h"

#define OFF_BakeComparator 0xfdfea0  /* StaticInstancesRenderObj::BakeComparator(void const*, void const*) */
#define OFF_ValueFromStream 0xfdfc60 /* StaticInstancesRenderObj::ValueFromStream(uchar const*, ElementT const*) */
#define OFF_BakeGlobals 0x186ef50    /* the stream, its stride and the position element, set for the sort */
#define WORD_BakeComparator 0xe92d4df0  /* push {r4-r8, sl, fp, lr} */
#define WORD_ValueFromStream 0xe92d41f0 /* push {r4-r8, lr} */

typedef struct {
  const uint8_t *data;
  int stride;
  const void *elem;
} BakeGlobals;

/* returns rw::math::vpu::Vector4 through the hidden pointer (r0) */
typedef void (*ValueFromStreamFn)(float *out, const uint8_t *p, const void *elem);

static int g_state; /* 0 unchecked, 1 on, -1 off */
static uintptr_t g_cmp;
static ValueFromStreamFn g_vfs;
static const BakeGlobals *g_glob;
static volatile uint32_t g_sorts, g_tris, g_us, g_fallbacks;

static int check(void) {
  if (g_state)
    return g_state > 0;
  g_cmp = ssx_text(OFF_BakeComparator);
  g_vfs = (ValueFromStreamFn)ssx_text(OFF_ValueFromStream);
  g_glob = (const BakeGlobals *)ssx_text(OFF_BakeGlobals);
  const int ok = *(const uint32_t *)g_cmp == WORD_BakeComparator &&
                 *(const uint32_t *)(uintptr_t)g_vfs == WORD_ValueFromStream;
  g_state = ok ? 1 : -1;
  debugPrintf("[bake] %s\n", ok ? "the static meshes' triangle sort takes each triangle's height once"
                                : "BakeComparator not where expected: the game's qsort as it is");
  return ok;
}

/* VCVT.S32.F32: toward zero, saturating, NaN -> 0 */
static int32_t cvt_s32(float f) {
  if (f != f)
    return 0;
  if (f >= 2147483648.0f)
    return INT32_MAX;
  if (f <= -2147483648.0f)
    return INT32_MIN;
  return (int32_t)f;
}

static float vertex_y(const BakeGlobals *g, int16_t idx) {
  float v[4] __attribute__((aligned(16)));
  g_vfs(v, g->data + g->stride * idx, g->elem); /* the index is read signed, as BakeComparator does */
  return v[1];
}

/* BakeComparator's key: the highest of the three y (its compare order, so a
 * NaN falls where it falls there), (int16)(int). */
static int16_t tri_key(const BakeGlobals *g, const int16_t *t) {
  const float y0 = vertex_y(g, t[0]), y1 = vertex_y(g, t[1]), y2 = vertex_y(g, t[2]);
  float m = y1 <= y2 ? y2 : y1;
  if (y0 > m)
    m = y0;
  return (int16_t)cvt_s32(m);
}

void b_qsort(void *base, size_t n, size_t size, int (*cmp)(const void *, const void *)) {
  if (size != 6 || n < 2 || !check() || (uintptr_t)cmp != g_cmp) {
    qsort(base, n, size, cmp);
    return;
  }
  const u64 t0 = armGetSystemTick();
  /* [debug] profile: the heap at the start of each bake (the first sort after
   * 5 s without one: a drop start or a Restart). mallinfo() walks the heap
   * with the malloc lock held (~28 ms), invisible inside the bake's long
   * frame: memory in use against free space inside the arena, a leak from
   * fragmentation. */
  static u64 last;
  if (dcr_config()->profile && (!last || armTicksToNs(t0 - last) > 5000000000ull)) {
    struct mallinfo mi = mallinfo();
    debugPrintf("[mem] a bake begins: in use %u MB, free inside the arena %u MB (%u free blocks), arena %u MB\n",
                (unsigned)(mi.uordblks >> 20), (unsigned)(mi.fordblks >> 20), (unsigned)mi.ordblks,
                (unsigned)(mi.arena >> 20));
    void ssx_globjs_report(const char *when); /* ssx_globjs.c */
    ssx_globjs_report("a bake begins");
  }
  last = t0;
  const BakeGlobals g = *g_glob;
  /* Two index arrays for the radix passes, each triangle's sort value (0 =
   * highest) and the triangles (a cached copy), in one scratch block kept
   * from one sort to the next (grown, never freed: a bake is ~735 sorts, and
   * four malloc/free pairs each, between the bake's own allocations, would
   * only fragment the heap). The bake runs on the render thread alone. */
  static uint8_t *scratch;
  static size_t scratch_n;
  if (n > scratch_n) {
    size_t want = scratch_n ? scratch_n : 4096;
    while (want < n)
      want *= 2;
    uint8_t *p = realloc(scratch, want * 16);
    if (p)
      scratch = p, scratch_n = want;
  }
  if (n > scratch_n || !g.data) {
    g_fallbacks++;
    qsort(base, n, size, cmp);
    return;
  }
  uint32_t *a = (uint32_t *)scratch, *b = a + n;
  uint16_t *key = (uint16_t *)(b + n);
  int16_t *tri = (int16_t *)(key + n);
  memcpy(tri, base, n * 6);
  for (size_t i = 0; i < n; i++) {
    key[i] = (uint16_t)(32767 - tri_key(&g, tri + 3 * i)); /* highest key first */
    a[i] = (uint32_t)i;
  }
  /* stable LSD radix sort on the 16-bit value: low byte, then high byte */
  for (int shift = 0; shift < 16; shift += 8) {
    uint32_t count[257] = {0};
    for (size_t i = 0; i < n; i++)
      count[((key[a[i]] >> shift) & 0xff) + 1]++;
    for (int k = 0; k < 256; k++)
      count[k + 1] += count[k];
    for (size_t i = 0; i < n; i++)
      b[count[(key[a[i]] >> shift) & 0xff]++] = a[i];
    uint32_t *t = a;
    a = b, b = t;
  }
  uint8_t *out = base;
  for (size_t i = 0; i < n; i++)
    memcpy(out + 6 * i, tri + 3 * a[i], 6);
  g_sorts++;
  g_tris += (uint32_t)n;
  g_us += (uint32_t)(armTicksToNs(armGetSystemTick() - t0) / 1000);
}

/* ssx_boot.c's 10 s report: the sorts since the last one. */
void ssx_bakesort_report(void) {
  static uint32_t sorts_seen;
  const uint32_t sorts = g_sorts, tris = g_tris, us = g_us;
  if (sorts == sorts_seen)
    return;
  sorts_seen = sorts;
  debugPrintf("[bake] %u triangle sorts so far (%u triangles) in %u.%03u ms in all%s\n", (unsigned)sorts,
              (unsigned)tris, (unsigned)(us / 1000), (unsigned)(us % 1000),
              g_fallbacks ? " (some by qsort: out of memory)" : "");
}
