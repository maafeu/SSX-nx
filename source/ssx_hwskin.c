/* ssx_hwskin.c -- the riders skinned on the GPU.
 *
 * CharacterRenderObj::Init gives every character a CpuSkinningFeatureImpl,
 * which skins its meshes on the CPU in EA::Jobs jobs (several ms a race frame,
 * on every core). The game also has GpuSkinningFeatureImpl (vertex-shader
 * skinning with a matrix palette, g_aMatrixPalette / BLENDINDICES /
 * BLENDWEIGHT, which the rider shaders have variants for), used by
 * GameObjectRenderObj when its config says "gpuSkinning". Both implement
 * ISkinningFeature with the same virtual functions and nothing calls the CPU
 * one directly, so Init builds the GPU one instead (mergeSkeletons off: a
 * merged palette holds 50 bones and a rider has more; useCpuMem off).
 *
 * The GPU feature needs its draws bracketed as GameObjectRenderObj::
 * DispatchMeshes does (GpuLockSkinningRegisters, GpuUpdateSkinningRegisters
 * before each MeshRenderObject::RenderImmediate, GpuUnlockSkinningRegisters),
 * and the meshes need the "<technique>_GpuSkinned<bones>" techniques
 * (FindTechniqueByName tries those first while CharacterRenderObj::
 * CacheMeshData runs for a GPU-skinned character).
 *
 * In the emulator the CPU-skinned riders were not drawn at all (the board
 * was, the rider on it not: emu14); GPU skinning is also the PS Vita port's
 * largest single speed-up in races (15-28 -> ~29 fps there). The method,
 * the offsets and the field layouts are the PS Vita port's (source/ssx/
 * hwskin.c, lukebrl, MIT). MIT.
 */
#include <GLES2/gl2.h>
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_AllocSize       0xfca5f0  /* mov r1, #52: sizeof(CpuSkinningFeatureImpl) */
#define OFF_InlineCtor      0xfca600  /* CpuSkinningFeatureImpl's inlined constructor (to 0xfca648) */
#define OFF_StoreFeature    0xfca648  /* str r7, [r5, #188]: the feature, then its Init() */
#define OFF_GpuFeatureCtor  0xfd32c4  /* GpuSkinningFeatureImpl::GpuSkinningFeatureImpl(bool, bool, int) */
#define OFF_GpuFeatureVtbl  0x175d440 /* _ZTV22GpuSkinningFeatureImpl (vptr = +8) */
#define OFF_DispatchMeshes  0xfcab48  /* CharacterRenderObj::DispatchMeshes(IUnknown32*, RenderPass) */
#define OFF_DispatchBoard   0xfcae84  /* CharacterRenderObj::DispatchBoardMeshForCascadeShadows(IUnknown32*, int) */
#define OFF_RenderImmediate 0x5cfec8  /* MeshRenderObject::RenderImmediate(IUnknown32*, CachedMeshData&, bool) */
#define OFF_CacheMeshData   0xfcb0ac  /* CharacterRenderObj::CacheMeshData(IUnknown32*) */
#define OFF_FindTechnique   0xdb1f34  /* RNA::ShaderC::FindTechniqueByName(char const*) const */

#define CHAR_FEATURE(o) (*(void ***)((uint8_t *)(o) + 188))
#define MESH_DATA(o) (*(uint8_t **)((uint8_t *)(o) + 36))
#define MESH_DATA_SIZE 40
#define VT_LOCK 28
#define VT_UPDATE 32
#define VT_UNLOCK 36
#define FEATURE_SKIN_DATA(f) (*(uint8_t **)((uint8_t *)(f) + 16))
#define SKIN_DATA_SIZE 28
#define SKIN_DATA_BONES(d) (*(int32_t *)((uint8_t *)(d) + 24))
#define LOOKUPS_PER_MESH 6

typedef uint64_t (*Fn8)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static Fn8 o_dispatch_meshes, o_dispatch_board, o_render_immediate, o_cache_mesh_data;
static void *(*o_find_technique)(void *shader, const char *name);
static void *(*g_gpu_ctor)(void *self, int merge_skeletons, int use_cpu_mem, int force_vert_num);
static uintptr_t g_gpu_vptr;
static __thread void *t_dispatching; /* the CharacterRenderObj being drawn (render thread) */
static __thread void **t_caching;    /* the GPU feature of the character in CacheMeshData */
static __thread int t_lookups;
static int g_created, g_missing_logged;
static volatile uint32_t g_n_dispatch, g_n_render;

static void **gpu_feature(void *character) {
  void **f = CHAR_FEATURE(character);
  return f && (uintptr_t)f[0] == g_gpu_vptr ? f : NULL;
}

#define VCALL(f, off, type) ((type)(*(void ***)(f))[(off) / 4])

static uint64_t cache_mesh_data(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                uint32_t a6, uint32_t a7) {
  void **f = gpu_feature((void *)a0);
  if (!f)
    return o_cache_mesh_data(a0, a1, a2, a3, a4, a5, a6, a7);
  t_caching = f;
  t_lookups = 0;
  uint64_t r = o_cache_mesh_data(a0, a1, a2, a3, a4, a5, a6, a7);
  t_caching = NULL;
  return r;
}

static void *find_technique(void *shader, const char *name) {
  if (!t_caching || !name)
    return o_find_technique(shader, name);
  int mesh = t_lookups++ / LOOKUPS_PER_MESH;
  int bones = SKIN_DATA_BONES(FEATURE_SKIN_DATA(t_caching) + mesh * SKIN_DATA_SIZE);
  if (bones >= 1 && bones <= 4) {
    char gpu_name[64];
    snprintf(gpu_name, sizeof gpu_name, "%s_GpuSkinned%d", name, bones);
    void *t = o_find_technique(shader, gpu_name);
    if (t)
      return t;
    if (g_missing_logged++ < 8)
      debugPrintf("[hwskin] no technique %s in a character shader: %s used\n", gpu_name, name);
  }
  return o_find_technique(shader, name);
}

#define DISPATCH_HOOK(name, orig)                                                                          \
  static uint64_t name(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,     \
                       uint32_t a6, uint32_t a7) {                                                         \
    void **f = gpu_feature((void *)a0);                                                                    \
    if (!f)                                                                                                \
      return orig(a0, a1, a2, a3, a4, a5, a6, a7);                                                         \
    VCALL(f, VT_LOCK, void (*)(void *, void *))(f, (void *)a1);                                            \
    g_n_dispatch++;                                                                                        \
    void *outer = t_dispatching;                                                                           \
    t_dispatching = (void *)a0;                                                                            \
    uint64_t r = orig(a0, a1, a2, a3, a4, a5, a6, a7);                                                     \
    t_dispatching = outer;                                                                                 \
    VCALL(f, VT_UNLOCK, void (*)(void *))(f);                                                              \
    return r;                                                                                              \
  }
DISPATCH_HOOK(dispatch_meshes, o_dispatch_meshes)
DISPATCH_HOOK(dispatch_board, o_dispatch_board)

static uint64_t render_immediate(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                 uint32_t a6, uint32_t a7) {
  if (t_dispatching && (void *)a0 == t_dispatching) {
    void **f = gpu_feature((void *)a0);
    int mesh = (int)(((uint8_t *)a2 - MESH_DATA((void *)a0)) / MESH_DATA_SIZE);
    g_n_render++;
    if (f)
      VCALL(f, VT_UPDATE, void (*)(void *, void *, int))(f, (void *)a1, mesh);
  }
  /* (A glGetError after each rider mesh was here; the errors it found were the
   * frame counter's, and with glthread it would make the render thread wait for
   * Mesa's worker at every mesh. The runtime's KHR_debug log reports errors.) */
  return o_render_immediate(a0, a1, a2, a3, a4, a5, a6, a7);
}

/* Called from the patched Init with the allocation in r0; returns it built. */
static void *make_gpu_feature(void *mem) {
  g_gpu_ctor(mem, 0, 0, -1);
  if (g_created++ == 0)
    debugPrintf("[hwskin] riders are skinned on the GPU (GpuSkinningFeatureImpl)\n");
  return mem;
}

static uint32_t arm_b(uint32_t from_vaddr, uint32_t to_vaddr) {
  return 0xEA000000u | (((to_vaddr - (from_vaddr + 8)) >> 2) & 0x00FFFFFFu);
}

void ssx_hwskin_report(void) {
  static u64 t_prev;
  u64 now = armGetSystemTick();
  if (t_prev && (g_n_dispatch || g_n_render)) {
    double s = (double)armTicksToNs(now - t_prev) / 1e9;
    debugPrintf("[hwskin] %.0f character dispatches, %.0f skinned meshes a second\n", g_n_dispatch / s,
                g_n_render / s);
  }
  g_n_dispatch = g_n_render = 0;
  t_prev = now;
  void ssx_hwskin_diag_report(void);
  ssx_hwskin_diag_report();
}

void ssx_hwskin_patch(void) {
  if (!ssx_hook_word_is(OFF_AllocSize, 0xE3A01034) || !ssx_hook_word_is(OFF_InlineCtor, 0xE59F3294) ||
      !ssx_hook_word_is(OFF_StoreFeature, 0xE58570BC) || !ssx_hook_word_is(OFF_GpuFeatureCtor, 0xE59FC170)) {
    debugPrintf("[hwskin] CharacterRenderObj::Init is not 0.0.8430's: left alone\n");
    return;
  }
  g_gpu_ctor = (void *)ssx_hook_live(OFF_GpuFeatureCtor);
  g_gpu_vptr = ssx_hook_live(OFF_GpuFeatureVtbl) + 8;
  o_dispatch_meshes = ssx_hook(OFF_DispatchMeshes, 0xe92d4ff0, (void *)dispatch_meshes, "CharacterRenderObj::DispatchMeshes");
  o_dispatch_board = ssx_hook(OFF_DispatchBoard, 0xe92d4370, (void *)dispatch_board, "CharacterRenderObj::DispatchBoard");
  o_render_immediate = ssx_hook(OFF_RenderImmediate, 0xe92d41f0, (void *)render_immediate, "MeshRenderObject::RenderImmediate");
  o_cache_mesh_data = ssx_hook(OFF_CacheMeshData, 0xe92d4ff0, (void *)cache_mesh_data, "CharacterRenderObj::CacheMeshData");
  o_find_technique = ssx_hook(OFF_FindTechnique, 0xe92d41f0, (void *)find_technique, "ShaderC::FindTechniqueByName");
  if (!o_dispatch_meshes || !o_dispatch_board || !o_render_immediate || !o_cache_mesh_data || !o_find_technique) {
    debugPrintf("[hwskin] the character draw functions could not all be hooked: GPU skinning off\n");
    return;
  }
  /* Init: the allocation 56 bytes (GpuSkinningFeatureImpl), and the inlined
   * CPU constructor replaced by a call of make_gpu_feature. */
  const uint32_t c = OFF_InlineCtor;
  ssx_poke_word(OFF_AllocSize, 0xE3A01038);            /* mov r1, #56 */
  ssx_poke_word(c + 0, 0xE1A07000);                    /* mov r7, r0 */
  ssx_poke_word(c + 4, 0xE59FC000);                    /* ldr r12, [pc, #0] (c + 12) */
  ssx_poke_word(c + 8, arm_b(c + 8, c + 16));          /* b c + 16 */
  ssx_poke_word(c + 12, (uint32_t)(uintptr_t)make_gpu_feature);
  ssx_poke_word(c + 16, 0xE1A00007);                   /* mov r0, r7 */
  ssx_poke_word(c + 20, 0xE12FFF3C);                   /* blx r12 */
  ssx_poke_word(c + 24, arm_b(c + 24, OFF_StoreFeature)); /* b StoreFeature: str r7, [r5, #188] */
  debugPrintf("[hwskin] GPU skinning patched in\n");
  void ssx_hwskin_diag_patch(void);
  ssx_hwskin_diag_patch();
}

/* Diagnostics: GameSkinning::SetJointPaletteData(Vector4 const*, int count),
 * the bone matrices a skinned draw uploads (3 vec4 a bone): how many hold NaN
 * (the rider's vertices came out NaN under the emulator: emu-rdc1). */
#define OFF_SetJointPalette 0x1019820
static void (*o_set_palette)(const float *data, int count);
static volatile uint32_t g_pal_calls, g_pal_nan_calls;
static void set_palette(const float *data, int count) {
  int nan = 0;
  for (int i = 0; i < count * 4; i++)
    nan += data[i] != data[i];
  g_pal_calls++;
  if (nan) {
    g_pal_nan_calls++;
    if (g_pal_nan_calls <= 6)
      debugPrintf("[hwskin] palette of %d vec4 with %d NaN: %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f\n", count, nan,
                  data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7]);
  } else if (g_pal_calls <= 3) {
    debugPrintf("[hwskin] palette of %d vec4: %.3f %.3f %.3f %.3f / %.3f %.3f %.3f %.3f\n", count, data[0], data[1],
                data[2], data[3], data[4], data[5], data[6], data[7]);
  }
  o_set_palette(data, count);
}
void ssx_hwskin_diag_patch(void) {
  o_set_palette = ssx_hook(OFF_SetJointPalette, 0xe52d4004, (void *)set_palette, "GameSkinning::SetJointPaletteData");
}
void ssx_hwskin_diag_report(void) {
  if (g_pal_calls)
    debugPrintf("[hwskin] %u palettes uploaded, %u with NaN\n", g_pal_calls, g_pal_nan_calls);
}
