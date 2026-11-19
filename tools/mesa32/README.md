# mesa32 changes kept with this port

mesa32 (Mesa 20.1.0-rc3 for the Switch, aks796/mesa32) is built beside this repository by `tools/deps/build_deps.sh`.
Changes made for SSX-nx are kept here as patches against that tree: `build_deps.sh fetch` applies them (`patch -p1`
from the mesa32 folder does the same).

- `shader-cache-switch.patch` (2026-11-17): Mesa's shader disk cache on the Switch. `src/util/disk_cache_switch.c`
  implements `disk_cache.h` with one pack file (no mmap, flock, nftw or zlib); meson enables `ENABLE_SHADER_CACHE`
  for horizon; nouveau names its cache by `MESA_SHADER_CACHE_BUILD_ID` (the port's hash of the Mesa libraries).
  The port turns it on with `[performance] shader_cache` (`source/ssx_shadercache.c`).
  Status: works in Ryujinx (second start: 839 programs from the cache, 1 compiled) and fills the cache on the
  console (694 programs, 5.2 MB). The first console start that read the cache was followed by the whole console
  going down within ~10 s of the first picture; 8 later warm starts were fine. Not understood. The port's default
  is on, with a safety net: a start that dies in its first minute turns the cache off for the following starts.
- `fence-wait-stats.patch` (2026-11-17): a diagnostic. `nouveau_fence_wait` times the waits that block (the fence
  not yet signalled) per caller, and per caller of `nouveau_screen_fence_finish` when it is that one;
  `nouveau_fence_wait_report()` formats and resets them, and the port logs them every 10 s as `[fence]`
  (`source/ssx_gl.c`; the addresses are offsets from `nouveau_fence_wait`, to be put to names with `ssx_nx.elf`).
  It found the game's per-frame `glFinish` (`LoopRender::RenderFrame`) as the only blocking wait:
  `st_finish` -> `nouveau_screen_fence_finish`, ~55 a second. Costs nothing while no wait blocks.
