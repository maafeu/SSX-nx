/* ssx_render.c -- [display] render_resolution: the size the 3D scene is drawn at.
 *
 * SSX draws its 3D scene into a render target its render script defines
 * (mobile/scrape/main.scrape: "var texture FrameBufferTexture 1280 720
 * format" and a 1280 720 D24S8 depth surface), then its post-effects pass
 * draws that texture to the window, scaled to the window's size; the menus
 * and the HUD are drawn at the window's size directly. So the scene's
 * resolution and the window's are separate: the window is [display]
 * resolution (720, 1080, or auto: 1080 docked), the scene is this setting,
 * applied by editing the script as it is read (ssx_assetpatch.c). auto: the
 * window's size. A scene larger than the window (1080 on the 720 handheld
 * screen) is scaled down: supersampling.
 *
 * Found by logging every render target the game's RNA layer makes, with its
 * size, name and caller (RNA::SurfacePlatC::Create, RNA::TexturePlatC::Create
 * / CreateAndSet): the "Scrape" texture and its depth surface were the
 * 1280x720 ones (Ryujinx, 2026-11-17). The logging stays: render targets
 * (flags 0x10) and surfaces, once each. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#include "dcr_config.h"
#include "rt_window.h"
#include "ssx.h"
#include "ssx_hook.h"
#include "util.h"

#define OFF_SurfacePlatC_Create 0xdc1724  /* RNA::SurfacePlatC::Create(DeviceC*, int w, int h, SurfaceFormatE, unsigned, MsaaE, char const*) */
#define OFF_TexturePlatC_Create 0xdac8dc  /* RNA::TexturePlatC::Create(DeviceC*, TextureTypeE, int w, int h, int d, SurfaceFormatE, int, int, unsigned, char const*) */
#define OFF_TexturePlatC_CreateAndSet 0xdc18e8 /* the same, then the pixels */
#define FLAG_RENDER_TARGET 0x10

/* The scene's size: [display] render_resolution's height (16:9), or the
 * window's. */
void ssx_render_scene_size(int *w, int *h) {
  int ww = 1280, wh = 720;
  dcr_window_size(&ww, &wh);
  int rh = dcr_config()->render_height;
  if (rh <= 0) {
    *w = ww, *h = wh;
    return;
  }
  *h = rh;
  *w = (rh * 16 + 8) / 9 & ~1; /* 720 -> 1280, 900 -> 1600, 1080 -> 1920 */
}

typedef uint32_t (*Fn12)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                         uint32_t, uint32_t, uint32_t);
static Fn12 o_surface_create, o_texture_create, o_texture_create_set;
static unsigned g_logged;

static const char *safe_name(uint32_t p) {
  /* the name argument: a C string in the game's data, or nothing */
  uintptr_t base = (uintptr_t)g_mod_game.load_virtbase;
  if (p < base || p >= base + g_mod_game.load_size)
    return "";
  return (const char *)(uintptr_t)p;
}

static uint32_t caller(void *lr) {
  uintptr_t base = (uintptr_t)g_mod_game.load_virtbase, a = (uintptr_t)lr;
  return a >= base && a < base + g_mod_game.load_size ? (uint32_t)(a - base) : 0;
}

static uint32_t surface_create(uint32_t self, uint32_t dev, uint32_t w, uint32_t h, uint32_t fmt, uint32_t flags,
                               uint32_t msaa, uint32_t name, uint32_t a8, uint32_t a9, uint32_t a10, uint32_t a11) {
  if (g_logged++ < 64)
    debugPrintf("[render] surface %ux%u, format %u, msaa %u \"%s\" (libgame+0x%x, frame %u)\n", w, h, fmt, msaa,
                safe_name(name), caller(__builtin_return_address(0)), g_ssx_frames_presented);
  return o_surface_create(self, dev, w, h, fmt, flags, msaa, name, a8, a9, a10, a11);
}

static uint32_t texture_create(uint32_t self, uint32_t dev, uint32_t type, uint32_t w, uint32_t h, uint32_t d,
                               uint32_t fmt, uint32_t a7, uint32_t a8, uint32_t flags, uint32_t name, uint32_t a11) {
  if ((flags & FLAG_RENDER_TARGET) && g_logged++ < 64)
    debugPrintf("[render] render target %ux%u, format %u \"%s\" (libgame+0x%x, frame %u)\n", w, h, fmt,
                safe_name(name), caller(__builtin_return_address(0)), g_ssx_frames_presented);
  return o_texture_create(self, dev, type, w, h, d, fmt, a7, a8, flags, name, a11);
}

static uint32_t texture_create_set(uint32_t self, uint32_t dev, uint32_t type, uint32_t w, uint32_t h, uint32_t d,
                                   uint32_t fmt, uint32_t a7, uint32_t a8, uint32_t flags, uint32_t name, uint32_t px) {
  if ((flags & FLAG_RENDER_TARGET) && g_logged++ < 64)
    debugPrintf("[render] render target %ux%u, format %u \"%s\" (libgame+0x%x, frame %u)\n", w, h, fmt,
                safe_name(name), caller(__builtin_return_address(0)), g_ssx_frames_presented);
  return o_texture_create_set(self, dev, type, w, h, d, fmt, a7, a8, flags, name, px);
}

void ssx_render_patch(void) {
  int sw, sh, ww = 1280, wh = 720;
  ssx_render_scene_size(&sw, &sh);
  dcr_window_size(&ww, &wh);
  debugPrintf("[render] the 3D scene at %dx%d, the window %dx%d%s\n", sw, sh, ww, wh,
              sw * sh > ww * wh ? " (scaled down: supersampled)" : sw * sh < ww * wh ? " (scaled up)" : "");
  o_surface_create = ssx_hook(OFF_SurfacePlatC_Create, 0xe92d40f8, (void *)surface_create, "RNA::SurfacePlatC::Create");
  o_texture_create = ssx_hook(OFF_TexturePlatC_Create, 0xe52de004, (void *)texture_create, "RNA::TexturePlatC::Create");
  o_texture_create_set =
      ssx_hook(OFF_TexturePlatC_CreateAndSet, 0xe92d40f8, (void *)texture_create_set, "RNA::TexturePlatC::CreateAndSet");
}
