/* ssx_loader.c -- loading SSX's libraries.
 *
 * libgame.so (40 MB, 25 MB mapped) needs libdl, libOpenSLES, liblog,
 * libandroid, libEGL, libGLESv2, bionic's libc/libstdc++/libm, and three
 * libraries of the APK: libgnustl_shared.so (GNU libstdc++ for the NDK,
 * loaded here first: libgame.so binds its std:: imports to it), libnimble.so
 * and libmercury.so (EA telemetry and the DLC downloader: not loaded, their
 * few imports are ssx_stubs.c). Both modules are built for ARMv7-A (softfp,
 * ARM and Thumb-2, NEON) by NDK r8e.
 *
 * KERNEL USER HELPERS: libgcc's __sync_* in both call the Linux kernel's user
 * helpers (0xffff0fc0 cmpxchg, 0xffff0fa0 barrier); the literals are pointed
 * at kuser.S before the code is sealed (so_fix_kuser_helpers).
 *
 * The libgame.so this port's offsets are for is checked by size and by the
 * first words of a few functions it patches (ssx_patch.c); another build
 * loads, but unpatched, with a warning. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_path.h"
#include "error.h"
#include "imports.h"
#include "ssx.h"
#include "util.h"

so_module g_mod_game, g_mod_stl;
uintptr_t g_ssx_base;

void *ssx_native(const char *symbol) { return (void *)so_try_find_addr_rx(&g_mod_game, symbol); }

static int load_one(so_module *m, const char *lib, size_t max, int patch) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s", dcr_game_root(), lib);
  u64 t0 = armGetSystemTick();
  int rc = so_load(m, path, NULL, max);
  if (rc < 0) {
    const char *why = rc == -1 ? "cannot open it, or it is not a 32-bit ARM ELF"
                    : rc == -2 ? "out of memory"
                    : rc == -3 ? "larger than its region"
                    : rc == -4 ? "too many program headers" : "?";
    debugPrintf("[boot] so_load(%s) failed rc=%d: %s\n", path, rc, why);
    return -1;
  }
  so_relocate(m);
  int missing = so_resolve(m, dcr_imports, dcr_imports_count, 1);
  int kuser = so_fix_kuser_helpers(m);
  if (patch) {
    g_ssx_base = (uintptr_t)m->load_virtbase;
    ssx_patch_game(m);
  }
  so_finalize(m);
  so_flush_caches(m);
  debugPrintf("[boot] %s %u KB at %p (%d unresolved imports, %d kernel helper literals) in %llu ms\n",
              m->base_name, (unsigned)(m->load_size >> 10), m->load_virtbase, missing, kuser,
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
  return 0;
}

int ssx_load_modules(void) {
  if (load_one(&g_mod_stl, SSX_LIB_STL, 4u << 20, 0) != 0)
    return -1;
  if (load_one(&g_mod_game, SSX_LIB_GAME, PORT_SO_REGION_BYTES, 1) != 0)
    return -1;
  return 0;
}

/* System.loadLibrary: the constructors, libstdc++'s first (JNI_OnLoad:
 * ssx_boot.c). */
void ssx_run_constructors(void) {
  extern int g_so_trace_ctors;
  g_so_trace_ctors = 0;
  u64 t0 = armGetSystemTick();
  so_execute_init_array(&g_mod_stl);
  so_execute_init_array(&g_mod_game);
  debugPrintf("[boot] constructors done in %llu ms\n",
              (unsigned long long)(armTicksToNs(armGetSystemTick() - t0) / 1000000ull));
}
