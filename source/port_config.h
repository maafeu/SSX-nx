/* port_config.h -- SSX's settings for the android32 runtime.
 *
 * Macros only: the runtime's C files, its assembly and the launcher all read
 * this (runtime/source/rt_settings.h). What each setting does is next to its
 * default in the runtime; runtime/docs/ lists them all. MIT.
 */
#ifndef PORT_CONFIG_H
#define PORT_CONFIG_H

/* ------------------------------------------------------------------ the game */
#define PORT_TITLE    "SSX"
/* "ssx-android", not "ssx-nx": the Xbox 360 port of SSX (the other session's, a
 * ReXGlue NRO) already lives in /switch/ssx-nx; the two must not share a folder.
 * (No PORT_OLD_ROOT_PATHS: that folder is the other port's, never moved.) */
#define PORT_NAME     "ssx-android"
#define PORT_PACKAGE  "com.ea.ssx"
#define PORT_BANNER   "ssx_nx: SSX (EA Sports, 2012; EA's Blast/RenderWare engine, armeabi)"
#define PORT_ABI_DIR  "lib/armeabi/"
/* libgame.so 0.0.8430: highest p_vaddr+p_memsz = 0x18a8434 (~24.7 MB); each
 * module gets its own reservation of at most this */
#define PORT_SO_REGION_BYTES (32u * 1024 * 1024)

/* The APK, by what is in it (any name; it is not renamed). */
#define PORT_APK_DESC "SSX 0.0.8430 (com.ea.ssx, armeabi)"
#define PORT_APK_ROLES                                                             \
  {.what = "the game", .name = "game.apk",                                       \
   .need = (const char *const[]){"lib/armeabi/libgame.so", "assets/common/config/memory.cfg", NULL}, \
   .flags = RT_APK_ANY_FILE}
#define PORT_LAUNCHER_START_NOTE "(the first start unpacks the game's libraries from the APK)"
#define PORT_SETUP_NOTE "Getting SSX ready (the first start copies its music and movies)"

/* ------------------------------------------------------------------ frames */
#define RT_PAD_MAX_PLAYERS 8

/* ------------------------------------------------------------------ setup */
#define RT_SETUP_UPDATE_PERMILLE 500

#endif
