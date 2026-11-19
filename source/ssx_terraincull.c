/* ssx_terraincull.c -- terrain chunks outside the view are not drawn.
 *
 * TerrainRenderObj::Render and ::SortObjectsForDispatch collect the chunks to
 * draw from every loaded terrain object: a chunk is kept when its distance (a
 * Bvm2 result) is under a limit and TerrainGameObject::SelectLOD gives it a
 * level. EA::CoreWorld::Bvm2 tests each chunk's box against the frustum and
 * stores a class per chunk (0xC0: outside), but writes the distance and LOD
 * values only for the chunks inside -- the others keep the values of the last
 * frame they were in view -- and the collect loops never read the class. In
 * races every loaded chunk was drawn, about half of them off screen, each a
 * material bind and a draw (CPU and driver time on the render thread).
 *
 * SelectLOD (called only by those two loops) is replaced by the same code
 * that first answers "no level" for a chunk classed outside. When every chunk
 * is classed outside, the results are taken as not computed for this view
 * (seen while a race starts) and nothing is culled. The PS Vita port's
 * analysis and offsets (source/ssx/terraincull.c, lukebrl, MIT): there,
 * 75-107 chunks drawn a frame became 30-58. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_TerrainRenderObj_Render 0xffbd38 /* TerrainRenderObj::Render(IUnknown32*, unsigned, double) */
#define OFF_SortObjectsForDispatch 0xffb7c0  /* TerrainRenderObj::SortObjectsForDispatch(int, unsigned) */
#define OFF_SelectLOD 0xff5a58               /* TerrainGameObject::SelectLOD(float) const */
#define OFF_HandleMap_Index 0xff968c         /* eastl::map<TerrainGameObject const*, Bvm2::BvmHandle>::operator[] */

#define TRO_OBJECTS(t) (*(uint8_t **)((uint8_t *)(t) + 80))
#define TRO_OBJECTS_END(t) (*(uint8_t **)((uint8_t *)(t) + 84))
#define TRO_BVM(t) (*(uint8_t **)((uint8_t *)(t) + 228))
#define TRO_MAP(t) ((uint8_t *)(t) + 232)
#define OBJECT_SIZE 256
#define BVM_CLASS(b) (*(uint8_t **)((b) + 7536))
#define CLASS_OUTSIDE 0xC0
#define OBJ_NLODS(o) (*(int32_t *)((uint8_t *)(o) + 16))
#define OBJ_LODS(o) (*(uint8_t **)((uint8_t *)(o) + 12))
#define LOD_SIZE 32
#define PASS_SHAD 0x53484144
#define PASS_GBUF 0x47425546

typedef uint16_t *(*MapIndexFn)(void *map, void *const *key);
static void (*o_render)(void *, void *, uint32_t, double);
static void (*o_sort)(void *, int, uint32_t);
static MapIndexFn g_map_index;
static __thread void *t_culling; /* the TerrainRenderObj collecting, when its Bvm2 results can be used */
static volatile uint32_t g_culled, g_kept;

static int chunk_class(void *tro, const void *obj) {
  uint16_t h = *g_map_index(TRO_MAP(tro), (void *const *)&obj);
  return BVM_CLASS(TRO_BVM(tro))[h >> 2];
}

static void *cull_with(void *tro) {
  if (!TRO_BVM(tro) || !BVM_CLASS(TRO_BVM(tro)))
    return NULL;
  for (uint8_t *o = TRO_OBJECTS(tro); o && o < TRO_OBJECTS_END(tro); o += OBJECT_SIZE)
    if (chunk_class(tro, *(void **)o) != CLASS_OUTSIDE)
      return tro;
  return NULL;
}

/* The game's SelectLOD: the first level whose switch value is at most the
 * metric (negative metrics read 0), -1 for none; and none outside the view. */
static int select_lod(const void *obj, float metric) {
  if (t_culling && chunk_class(t_culling, obj) == CLASS_OUTSIDE) {
    g_culled++;
    return -1;
  }
  g_kept++;
  float ssx_detail_scale(void); /* ssx_detail.c: [display] detail_distance */
  metric *= ssx_detail_scale();
  if (metric < 0.0f)
    metric = 0.0f;
  const uint8_t *lod = OBJ_LODS(obj);
  for (int i = 0, n = OBJ_NLODS(obj); i < n; i++, lod += LOD_SIZE)
    if (*(const float *)(lod + 24) <= metric)
      return i;
  return -1;
}

static void sort_hook(void *tro, int view, uint32_t pass) {
  t_culling = cull_with(tro);
  o_sort(tro, view, pass);
  t_culling = NULL;
}

static void render_hook(void *tro, void *ctx, uint32_t pass, double t) {
  t_culling = (pass == PASS_SHAD || pass == PASS_GBUF) ? cull_with(tro) : NULL;
  o_render(tro, ctx, pass, t);
  t_culling = NULL;
}

void ssx_terraincull_report(void) {
  uint32_t c = g_culled, k = g_kept;
  if (c || k)
    debugPrintf("[terrain] chunk LOD picks: %u kept, %u culled outside the view (%.0f%%)\n", k, c,
                c + k ? 100.0 * c / (c + k) : 0.0);
  g_culled = g_kept = 0;
}

void ssx_terraincull_patch(void) {
  if (!ssx_hook_word_is(OFF_SelectLOD, 0xee071a10) || !ssx_hook_word_is(OFF_HandleMap_Index, 0xe92d4030)) {
    debugPrintf("[terrain] SelectLOD is not 0.0.8430's: left alone\n");
    return;
  }
  g_map_index = (MapIndexFn)ssx_hook_live(OFF_HandleMap_Index);
  o_render = ssx_hook(OFF_TerrainRenderObj_Render, 0xe92d4ff0, (void *)render_hook, "TerrainRenderObj::Render");
  o_sort = ssx_hook(OFF_SortObjectsForDispatch, 0xe92d4ff0, (void *)sort_hook, "TerrainRenderObj::SortObjectsForDispatch");
  if (!o_render || !o_sort) {
    debugPrintf("[terrain] the collect loops could not be hooked: every chunk still drawn\n");
    return;
  }
  ssx_hook_jump(OFF_SelectLOD, 0xee071a10, (void *)select_lod);
  debugPrintf("[terrain] chunks outside the view are not drawn\n");
}
