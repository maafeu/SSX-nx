/* ssx_detail.c -- [display] detail_distance: how far the world keeps its detail.
 *
 * What is drawn, and at which level of detail, is decided per object by
 * SelectLOD(metric): the metric is Bvm2's per-object value for the view (it
 * grows as an object gets nearer; Bvm2 has no far plane worth the name: the
 * visibility frustum's far clip is 1,000,000), each LOD has a switch value,
 * and the first LOD whose value is at most the metric is drawn; below the last
 * one the object is not drawn at all. There are two: EA::CoreWorld::
 * GameObject::SelectLOD (props, trees, the instanced scenery; called only by
 * GameObjectRenderObj::RenderSetup and InstanceRenderObjBase's sort and
 * dispatch) and TerrainGameObject::SelectLOD (the terrain chunks,
 * ssx_terraincull.c).
 *
 * Both take the metric times detail_distance/100 here: above 100 every LOD
 * switch and the final cut-off move farther away (more detail, more draws:
 * render time), below 100 nearer (fewer draws, earlier pop-in). 100 is the
 * game as shipped. GameObject::SelectLOD is replaced by the same code with the
 * scale (negative metrics read 0, as in the original). MIT.
 */
#include <stdint.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_GameObject_SelectLOD 0x595878 /* EA::CoreWorld::GameObject::SelectLOD(float) const */
#define GO_LODS(o) (*(const uint8_t *const *)((const uint8_t *)(o) + 16))
#define GO_LODS_END(o) (*(const uint8_t *const *)((const uint8_t *)(o) + 20))
#define GO_LOD_SIZE 8 /* the switch value at +4 */

static float g_scale = 1.0f;
static volatile uint32_t g_picks, g_none;

float ssx_detail_scale(void) { return g_scale; }

static int object_select_lod(const void *obj, float metric) {
  metric *= g_scale;
  if (metric < 0.0f)
    metric = 0.0f;
  g_picks++;
  const uint8_t *l = GO_LODS(obj), *e = GO_LODS_END(obj);
  for (int i = 0; l < e; l += GO_LOD_SIZE, i++)
    if (*(const float *)(l + 4) <= metric)
      return i;
  g_none++;
  return -1;
}

void ssx_detail_patch(void) {
  int pct = dcr_config()->detail_distance;
  if (pct <= 0)
    pct = 100;
  g_scale = (float)pct / 100.0f;
  /* replaced at 100 too (the same choices), so the report counts the picks */
  if (!ssx_hook_jump(OFF_GameObject_SelectLOD, 0xee071a10, (void *)object_select_lod)) {
    debugPrintf("[detail] GameObject::SelectLOD is not 0.0.8430's: objects keep the game's distances\n");
    return;
  }
  debugPrintf("[detail] detail_distance %d: object and terrain LOD metrics x%.2f%s\n", pct, (double)g_scale,
              pct == 100 ? " (the game's own distances)" : "");
}

void ssx_detail_report(void) {
  uint32_t p = g_picks, n = g_none;
  if (p)
    debugPrintf("[detail] object LOD picks: %u, %u not drawn (beyond the last LOD)\n", (unsigned)p, (unsigned)n);
  g_picks = g_none = 0;
}
