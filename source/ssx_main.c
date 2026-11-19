/* ssx_main.c -- SSX's part of the boot sequence, and its first-launch setup.
 *
 * The runtime's main() (runtime/source/main.c) does everything up to the
 * player's APK: the log, config.ini, the start-up boost, the NRO self-update,
 * the APK found by what is in it (PORT_APK_ROLES: the zip holding
 * lib/armeabi/libgame.so and the game's assets, any name). Here: the two
 * libraries and classes.txt out of the APK (the plan below), the APK's
 * assets indexed in place (ssx_assets.c), the modules loaded and patched;
 * then the constructors and the game (ssx_boot.c). MIT.
 */
#include <switch.h>

#include "dcr_path.h"
#include "dcr_setup.h"
#include "error.h"
#include "rt_boot.h"
#include "rt_settings.h"
#include "ssx.h"
#include "util.h"

/* ---------------------------------------------------------------- setup plan
 * The bar, in permille of the first launch:
 *     0- 850  libgame.so and libgnustl_shared.so unpacked (by bytes; 41 MB)
 *   850- 950  the Java class list (classes.txt)
 *        1000 the game starts (its own first start then copies its music
 *             and movies out of the APK into data/files: ~750 MB, once)
 * .setup keys (never rename them): libgame.so, libgnustl_shared.so,
 * classes.txt. */
static const char *const k_libs[] = {SSX_LIB_GAME, SSX_LIB_STL};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 2,
    .libs_what = "Unpacking the game's libraries",
    .apk_requirement = "This port needs SSX 0.0.8430 (com.ea.ssx) for 32-bit ARM\n"
                       "(lib/armeabi/): use the APK of your own copy.",
    .libs_p0 = 0,
    .libs_p1 = 850,
    .classes_p0 = 850,
    .classes_p1 = 950,
};

/* The error screens' advice when the folder holds no APK of the game. */
const char *port_apk_help(void) {
  return "Copy the APK of your own SSX (com.ea.ssx 0.0.8430,\n"
         "ssx-v0_0_8430.apk, about 1.3 GB) into that folder:\n"
         "any name will do. The game's libraries, pictures, music\n"
         "and movies are all read from it.";
}

int port_load(const char *apk) {
  dcr_setup_from_apk(apk);
  if (ssx_assets_init(apk) != 0)
    fatal_error("Could not read the assets in %s.\n\nIs it the APK of SSX (com.ea.ssx 0.0.8430)?", apk);
  if (ssx_load_modules() != 0)
    fatal_error("Could not load the game's libraries from %s.\n\n"
                "They are unpacked from the APK (lib/armeabi/) on launch: delete\n"
                SSX_LIB_GAME ", " SSX_LIB_STL " and .setup there to unpack them again.",
                dcr_game_root());
  return 0;
}

/* MainActivityGenerated's static initialiser: System.loadLibrary("game") --
 * the constructors now (libgnustl_shared.so's first), JNI_OnLoad and the
 * activity in ssx_boot.c. */
void port_run(void) {
  ssx_run_constructors();
  ssx_boot_run();
}
