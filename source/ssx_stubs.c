/* ssx_stubs.c -- imports of libgame.so that no loaded module and no runtime
 * shim provides.
 *
 * EA MERCURY (libmercury.so, the DLC downloader) and EA NIMBLE (libnimble.so,
 * telemetry) are not loaded: the APK already holds all of the game's content,
 * EA's servers are long gone, and both are Java-heavy SDKs. libgame.so only
 * reaches Mercury through MERCURY::MercuryManager, which ssx_patch.c turns
 * into "already downloaded"; these stubs only have to be safe if called
 * anyway. Nimble's Tracking::isEnabled() gates every telemetry call, so
 * answering "disabled" switches it off. (As the PS Vita port by lukebrl does,
 * source/ssx/stubs.c there.)
 *
 * BIONIC extras: a few libc functions and objects the runtime has no shim for.
 * gen_imports.py binds each b_<name> here to the import <name>. MIT.
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "bionic.h"
#include "util.h"

/* -------------------------------------------------------------- Mercury */
static uint32_t g_mercury_base_dummy[64];

static int ret0(void) { return 0; }

void *b__ZN7MERCURY11MercuryBase11GetInstanceEv(void) { return g_mercury_base_dummy; }
int b__ZN7MERCURY11MercuryBase13StartDownloadElRSs(void) { return ret0(); }
int b__ZN7MERCURY11MercuryBase14GetAllPackagesEv(void) { return ret0(); }
int b__ZN7MERCURY11MercuryBase18CancelAllDownloadsEv(void) { return ret0(); }
int b__ZN7MERCURY11MercuryBase18GetPackagesInQueueEv(void) { return ret0(); }
int b__ZN7MERCURY11MercuryBase8PauseAllEv(void) { return ret0(); }
int b__ZN7MERCURY11MercuryBase9ResumeAllEv(void) { return ret0(); }
int b__ZN7MERCURY14MercuryAndroid4InitEP7_JNIEnvP8_jobjectPNS_15IMercuryHandlerEPciiS7_S7_S7_(void) {
  debugPrintf("[ssx] Mercury init refused (DLC downloader: the APK holds all content)\n");
  return 0;
}
int b__ZN7MERCURY14MercuryAndroid6DeinitEv(void) { return ret0(); }
int b__ZN7MERCURY7Package19isPackageDownloadedEv(void) { return 1; }

/* -------------------------------------------------------------- Nimble */
/* Tracking::getComponent() returns a small handle by value (hidden result
 * pointer in r0). */
void *b__ZN2EA6Nimble8Tracking8Tracking12getComponentEv(void *ret) {
  if (ret)
    memset(ret, 0, sizeof(uint32_t));
  return ret;
}
int b__ZN2EA6Nimble8Tracking8Tracking9isEnabledEv(void *self) { return 0; }
void b__ZN2EA6Nimble8Tracking8Tracking10setEnabledEb(void *self, int on) {}
void b__ZN2EA6Nimble8Tracking8Tracking8logEventESsRKSt3mapISsSsSt4lessISsESaISt4pairIKSsSsEEE(void *self, void *name,
                                                                                            void *params) {}

/* -------------------------------------------------------------- libc */
/* softfp: the double arrives in r0:r1, as the host's own does */
int b___isinf(double x) { return isinf(x) ? (x > 0 ? 1 : -1) : 0; }
int b_isnan(double x) { return isnan(x); }
int b_isprint(int c) { return c >= 0x20 && c < 0x7f; }
void b_bcopy(const void *src, void *dst, size_t n) { memmove(dst, src, n); }

int b_fseek(void *fp, long off, int whence); /* bionic_stdio.c */
void b_clearerr(void *fp);
void b_rewind(void *fp) {
  b_fseek(fp, 0, SEEK_SET);
  b_clearerr(fp);
}
/* Byte orientation is all bionic ever reports for its FILEs. */
int b_fwide(void *fp, int mode) { return mode > 0 ? 0 : mode; }

/* wchar_t is 4 bytes on both sides (Tag_ABI_PCS_wchar_t: 4). */
int b_wcscmp(const wchar_t *a, const wchar_t *b) {
  while (*a && *a == *b)
    a++, b++;
  return (*a > *b) - (*a < *b);
}
wchar_t *b_wcscpy(wchar_t *d, const wchar_t *s) {
  wchar_t *r = d;
  while ((*d++ = *s++))
    ;
  return r;
}

/* The game's clock is UTC: no time zone database here. */
void b_tzset(void) {}
long b_timezone = 0;
char *b_tzname[2] = {"UTC", "UTC"};

/* pthread attributes bionic.h lays out: the policy is kept, the stack only by
 * its size (the runtime's threads run on stacks of their own). */
int b_pthread_attr_setstacksize(b_pthread_attr_t *a, size_t s); /* bionic_pthread.c */
int b_pthread_attr_setschedpolicy(b_pthread_attr_t *a, int policy) {
  if (a)
    a->sched_policy = policy;
  return 0;
}
int b_pthread_attr_setstack(b_pthread_attr_t *a, void *base, size_t size) {
  return b_pthread_attr_setstacksize(a, size);
}
/* Process-shared mutexes: one process here; accepted. */
int b_pthread_mutexattr_setpshared(b_pthread_mutexattr_t *a, int pshared) { return 0; }
