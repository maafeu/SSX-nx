# libdrm_nouveau patches

`../deps/libdrm_nouveau-1.0.1` (devkitPro's libdrm_nouveau 1.0.1, the commit pinned in `tools/deps/build_deps.sh`)
with:

- `bo-mem-hooks.patch`: two weak hooks, `nouveau_switch_bo_mem_alloc(size)` and
  `nouveau_switch_bo_mem_free(mem, size)`, for the memory behind buffer objects (by default the
  `memalign(0x1000, size)` / `free` the file always used). The port's `source/ssx_bomem.c` defines them
  to keep big blocks for reuse.

`tools/deps/build_deps.sh fetch` applies it (as LF), and `tools/deps/build_deps.sh drm` builds the library.
