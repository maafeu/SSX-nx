/* ssx_java.c -- the Java side of SSX's APK, answered in C.
 *
 * libgame.so calls back into Java through JNI for the device (com.ea.blast's
 * delegates), its folders (com.ea.EAMIO.StorageDirectory), the activity's
 * services, EGL (javax.microedition.khronos.egl.EGL10: ssx_egl.c), sound
 * (android.media.AudioTrack: ssx_audio.c), movies (com.ea.VideoPlayer:
 * ssx_video.c) and the APK's assets (android.content.res.AssetManager and
 * its streams, here). Every method below is one the Java original has
 * (classes.dex of 0.0.8430; tools/dexdump.py lists them); what each answers
 * is what a phone of 2012 would, for a Switch. The runtime's jni_core.c does
 * the rest, and logs once each call nothing here answers.
 *
 * What the PS Vita port (lukebrl's ssx-vita, source/jni/) found the game
 * needs was the starting point. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "ssx.h"
#include "util.h"

JObj *g_ssx_activity, *g_ssx_asset_manager, *g_ssx_audio_track, *g_ssx_main_thread;

/* ------------------------------------------------------------ helpers */
#define H(fn) static JNI_H_DECL(fn)
#define A_I(n) (a[n].i)
#define A_J(n) (a[n].j)
#define A_L(n) ((JObj *)a[n].l)

static jint call_native_int(const char *sym, jint dflt) {
  jint (*f)(void *, void *) = (jint(*)(void *, void *))ssx_native(sym);
  return f ? f(g_jni_env, NULL) : dflt;
}

/* ------------------------------------------------------------ the activity */
H(h_get_instance) { return jv_l(jni_retain(g_ssx_activity)); }
H(h_get_assets) { return jv_l(jni_retain(g_ssx_asset_manager)); }
/* The game calls it on Back (B, its Circle) on its top menu, where Android
 * would close it. A Switch game is closed from the HOME menu: here B does
 * nothing and the game carries on where it was (the owner's choice). Should a
 * quit ever be wanted, ssx_request_exit() quits cleanly (ssx_boot.c holds the
 * render thread before libnx closes the window). */
H(h_exit_app) {
  debugPrintf("[java] exitApp: the game asked to quit (Back on its top menu): ignored\n");
  return jv_none();
}
H(h_open_url) {
  debugPrintf("[java] openURL(%s): no browser here\n", jni_utf(A_L(0)));
  return jv_none();
}
H(h_command_line) { return jv_l(jni_array('L', 0)); }
H(h_one) { return jv_i(1); }

/* ------------------------------------------------------------ folders */
H(h_internal_dir) { return jv_l(jni_str(SSX_ANDROID_FILES_DIR)); }
H(h_external_dir) { return jv_l(jni_str(SSX_ANDROID_SDCARD "/Android/data/com.ea.ssx/files")); }
H(h_external_root) { return jv_l(jni_str(SSX_ANDROID_SDCARD)); }
H(h_external_state) { return jv_i(2); /* mounted */ }

/* ------------------------------------------------------------ the device */
H(h_platform_version) { return jv_l(jni_str("4.1.2")); }
H(h_api_level) { return jv_l(jni_str("16")); }
H(h_app_version) { return jv_l(jni_str(SSX_VERSION)); }
H(h_app_version_code) { return jv_i(SSX_VERSION_CODE); }
H(h_device_name) { return jv_l(jni_str("Nintendo Switch")); }
H(h_device_model) { return jv_l(jni_str("Switch")); }
H(h_manufacturer) { return jv_l(jni_str("Nintendo")); }
H(h_firmware) { return jv_l(jni_str("4.1.2")); }
H(h_chipset) { return jv_l(jni_str("tegra")); }
H(h_cpu_arch) { return jv_l(jni_str("armeabi-v7a")); }
H(h_hw_float) { return jv_l(jni_str("true")); }
H(h_unique_id) {
  /* an id of this console, hashed (EA's services used it; nothing reads it now) */
  return jv_l(jni_str("5357495443480000"));
}
H(h_locale) { return jv_l(jni_str("en_US")); }
H(h_language) { return jv_l(jni_str("en")); }

/* Display: the window the game draws to (ssx_egl.c). */
H(h_disp_width) {
  int w;
  ssx_egl_size(&w, NULL);
  return jv_i(w);
}
H(h_disp_height) {
  int h;
  ssx_egl_size(NULL, &h);
  return jv_i(h);
}
H(h_dpi) { return jv_f(237.0f); /* a 6.2" 1280x720 screen */ }
H(h_std_orientation) {
  return jv_i(call_native_int("Java_com_ea_blast_DisplayAndroidDelegate_NativeGetOrientationNormal", 0));
}
H(h_orientation_lock) {
  return jv_i(call_native_int("Java_com_ea_blast_DisplayAndroidDelegate_NativeGetDisplayOrientationLocked", 1));
}
H(h_gl_view) { return jv_l(jni_retain(jni_singleton("com/ea/blast/MainView"))); }

/* Battery: the console's charge. */
H(h_battery_level) {
  u32 pct = 100;
  static int up = -1;
  if (up < 0)
    up = R_SUCCEEDED(psmInitialize());
  if (up)
    psmGetBatteryChargePercentage(&pct);
  return jv_f((float)pct / 100.0f);
}
H(h_power_connected) {
  PsmChargerType t = PsmChargerType_Unconnected;
  static int up = -1;
  if (up < 0)
    up = R_SUCCEEDED(psmInitialize());
  if (up)
    psmGetChargerType(&t);
  return jv_i(t != PsmChargerType_Unconnected);
}

/* ------------------------------------------------------------ assets (Java) */
/* AssetManager.open(name[, mode]) -> an InputStream holding an SsxAsset. */
static void stream_finalize(JObj *o) {
  if (o->p)
    ssx_asset_close(o->p);
  o->p = NULL;
}
H(h_am_open) {
  const char *name = jni_utf(A_L(0));
  SsxAsset *as = ssx_asset_open(name);
  if (!as) {
    jni_throw("java/io/FileNotFoundException", name);
    return jv_l(NULL);
  }
  JObj *s = jni_new("android/content/res/AssetManager$AssetInputStream");
  s->p = as;
  s->finalize = stream_finalize;
  return jv_l(s);
}
H(h_am_open_fd) {
  const char *name = jni_utf(A_L(0));
  long size = ssx_asset_size_of(name);
  if (size < 0) {
    jni_throw("java/io/FileNotFoundException", name);
    return jv_l(NULL);
  }
  JObj *f = jni_new("android/content/res/AssetFileDescriptor");
  f->v[0] = size;
  return jv_l(f);
}
H(h_afd_length) { return jv_j(self ? (jlong)self->v[0] : -1); }
H(h_afd_start) { return jv_j(0); }
H(h_afd_close) { return jv_none(); }

typedef struct {
  JObj **names;
  int n, cap;
} ListCtx;
static void list_add(const char *name, void *ctx) {
  ListCtx *c = ctx;
  if (c->n == c->cap) {
    c->cap = c->cap ? c->cap * 2 : 64;
    c->names = realloc(c->names, (size_t)c->cap * sizeof *c->names);
  }
  c->names[c->n++] = jni_str(name);
}
H(h_am_list) {
  ListCtx c = {0};
  ssx_asset_list(jni_utf(A_L(0)), list_add, &c);
  JObj *arr = jni_array('L', c.n);
  for (int i = 0; i < c.n; i++)
    ((JObj **)arr->a.data)[i] = c.names[i]; /* the array takes the references */
  free(c.names);
  return jv_l(arr);
}

/* InputStream: read([B, off, len), read([B), read(), skip(n), available(),
 * close() */
H(h_is_read3) {
  JObj *arr = A_L(0);
  jint off = A_I(1), len = A_I(2);
  if (!self || !self->p || !arr || off < 0 || len < 0 || off + len > arr->a.len)
    return jv_i(-1);
  if (len == 0)
    return jv_i(0);
  int n = ssx_asset_read(self->p, (uint8_t *)arr->a.data + off, (size_t)len);
  return jv_i(n > 0 ? n : -1);
}
H(h_is_read1) {
  JObj *arr = A_L(0);
  if (!self || !self->p || !arr)
    return jv_i(-1);
  int n = ssx_asset_read(self->p, arr->a.data, (size_t)arr->a.len);
  return jv_i(n > 0 ? n : -1);
}
H(h_is_read0) {
  uint8_t b;
  if (!self || !self->p)
    return jv_i(-1);
  return jv_i(ssx_asset_read(self->p, &b, 1) == 1 ? b : -1);
}
static jvalue is_read(JObj *self, const jvalue *a, const JMethod *m) {
  if (!strcmp(m->sig, "([BII)I"))
    return h_is_read3(self, a, m);
  if (!strcmp(m->sig, "([B)I"))
    return h_is_read1(self, a, m);
  return h_is_read0(self, a, m);
}
H(h_is_skip) {
  if (!self || !self->p || A_J(0) <= 0)
    return jv_j(0);
  long cur = ssx_asset_tell(self->p), size = ssx_asset_size(self->p);
  long to = cur + (long)A_J(0) > size ? size : cur + (long)A_J(0);
  ssx_asset_seek(self->p, to, SEEK_SET);
  return jv_j(to - cur);
}
H(h_is_available) {
  if (!self || !self->p)
    return jv_i(0);
  return jv_i((jint)(ssx_asset_size(self->p) - ssx_asset_tell(self->p)));
}
H(h_is_close) {
  if (self)
    stream_finalize(self);
  return jv_none();
}

/* ------------------------------------------------------------ AudioTrack */
H(h_at_play) {
  ssx_audio_play(1);
  return jv_none();
}
H(h_at_stop) {
  ssx_audio_play(0);
  return jv_none();
}
H(h_at_write) {
  /* write(short[] data, int offsetInShorts, int sizeInShorts) */
  JObj *arr = A_L(0);
  jint off = A_I(1), n = A_I(2);
  if (!arr || off < 0 || n < 0 || off + n > arr->a.len)
    return jv_i(-3); /* ERROR_BAD_VALUE */
  return jv_i(ssx_audio_write((const int16_t *)arr->a.data + off, n));
}
H(h_at_rate) { return jv_i(SSX_AUDIO_RATE); }
H(h_at_state) { return jv_i(1); /* STATE_INITIALIZED */ }

/* ------------------------------------------------------------ PlayerAndroid */
H(h_pl_init) {
  ssx_video_init(self, jni_utf(A_L(0)), A_I(1), A_I(2), A_I(3), A_I(4), A_I(5));
  return jv_none();
}
H(h_pl_play) {
  ssx_video_play(self, A_I(0));
  return jv_none();
}
H(h_pl_stop) {
  ssx_video_stop(self);
  return jv_none();
}
H(h_pl_pause) { return jv_i(ssx_video_pause(self)); }
H(h_pl_is_playing) { return jv_z(ssx_video_is_playing(self)); }
H(h_pl_destroy) {
  ssx_video_destroy(self);
  return jv_none();
}

/* ------------------------------------------------------------ EGL (ssx_egl.c) */
JNI_H_DECL(ssx_egl_get_egl);
JNI_H_DECL(ssx_egl_get_surface);
JNI_H_DECL(ssx_egl_no_context);
JNI_H_DECL(ssx_egl_no_display);
JNI_H_DECL(ssx_egl_no_surface);
JNI_H_DECL(ssx_egl10_call);

#define BLAST "com/ea/blast/"
#define EGL10 "javax/microedition/khronos/egl/EGL10"

const JMethodDef jni_method_defs[] = {
    /* the activity */
    {BLAST "MainActivity", "GetInstance", NULL, h_get_instance},
    {"android/content/Context", "getAssets", NULL, h_get_assets},
    {BLAST "MainActivity", "IsActivityStopped", NULL, jni_h_false},
    {BLAST "MainActivity", "IsActivitySuspended", NULL, jni_h_false},
    {BLAST "MainActivity", "IsAppInstalledOnSdCard", NULL, jni_h_false},
    {BLAST "MainActivity", "OnContextLostEvent", NULL, jni_h_void},
    {BLAST "MainActivity", "OnContextResetEvent", NULL, jni_h_void},
    {"com/ea/ssx/MainActivity", "exitApp", NULL, h_exit_app},
    {"com/ea/ssx/MainActivity", "openURL", NULL, h_open_url},
    {"com/ea/ssx/MainActivity", "TryPauseAV", NULL, jni_h_void},
    {"com/ea/ssx/MainActivity", "TryResumeAV", NULL, jni_h_void},
    {"com/ea/ssx/MainActivity", "SignalPauseOrResumeEvent", NULL, jni_h_void},
    {"com/ea/ssx/MainActivityGenerated", "installUpdate", NULL, jni_h_void},
    {"com/ea/EAActivityArguments/EAActivityArguments", "GetCommandLineArguments", NULL, h_command_line},

    /* folders */
    {"com/ea/EAMIO/StorageDirectory", "GetInternalStorageDirectory", NULL, h_internal_dir},
    {"com/ea/EAMIO/StorageDirectory", "GetPrimaryExternalStorageDirectory", NULL, h_external_dir},
    {"com/ea/EAMIO/StorageDirectory", "GetPrimaryExternalStorageDirectoryRoot", NULL, h_external_root},
    {"com/ea/EAMIO/StorageDirectory", "GetPrimaryExternalStorageState", NULL, h_external_state},

    /* the device */
    {BLAST "SystemAndroidDelegate", "GetPlatformVersion", NULL, h_platform_version},
    {BLAST "SystemAndroidDelegate", "GetApiLevel", NULL, h_api_level},
    {BLAST "SystemAndroidDelegate", "GetApplicationVersion", NULL, h_app_version},
    {BLAST "SystemAndroidDelegate", "GetApplicationVersionCode", NULL, h_app_version_code},
    {BLAST "SystemAndroidDelegate", "GetDeviceName", NULL, h_device_name},
    {BLAST "SystemAndroidDelegate", "GetDeviceUniqueId", NULL, h_unique_id},
    {BLAST "SystemAndroidDelegate", "GetDeviceModel", NULL, h_device_model},
    {BLAST "SystemAndroidDelegate", "GetFirmware", NULL, h_firmware},
    {BLAST "SystemAndroidDelegate", "GetManufacturer", NULL, h_manufacturer},
    {BLAST "SystemAndroidDelegate", "GetLocale", NULL, h_locale},
    {BLAST "SystemAndroidDelegate", "GetLanguage", NULL, h_language},
    {BLAST "SystemAndroidDelegate", "GetProcessorArchitecture", NULL, h_cpu_arch},
    {BLAST "SystemAndroidDelegate", "GetHardwareFloatingPointSupport", NULL, h_hw_float},
    {BLAST "SystemAndroidDelegate", "GetChipset", NULL, h_chipset},
    /* the game assumes an accelerometer: with none, GameApplication::
     * AddAccelerationListener dereferences its missing module (it then
     * reads nothing: the delegate is never enabled) */
    {BLAST "SystemAndroidDelegate", "GetAccelerometerCount", NULL, h_one},
    {BLAST "SystemAndroidDelegate", "GetCompassCount", NULL, jni_h_zero},
    {BLAST "SystemAndroidDelegate", "GetGyroscopeCount", NULL, jni_h_zero},
    {BLAST "SystemAndroidDelegate", "GetVibratorCount", NULL, jni_h_zero},
    {BLAST "SystemAndroidDelegate", "GetTouchPadCount", NULL, jni_h_zero},
    /* and a touch screen (GameApplication::AddPointerListener): no touches come */
    {BLAST "SystemAndroidDelegate", "GetTouchScreenCount", NULL, h_one},
    {BLAST "SystemAndroidDelegate", "GetTrackBallCount", NULL, jni_h_zero},
    {BLAST "SystemAndroidDelegate", "GetCameraCount", NULL, jni_h_zero},
    {BLAST "SystemAndroidDelegate", "GetMicrophoneCount", NULL, jni_h_zero},
    {BLAST "SystemAndroidDelegate", "IntentView", NULL, jni_h_false},

    {BLAST "DisplayAndroidDelegate", "GetStdOrientation", NULL, h_std_orientation},
    {BLAST "DisplayAndroidDelegate", "SetStdOrientation", NULL, jni_h_void},
    {BLAST "DisplayAndroidDelegate", "GetDisplayOrientationLock", NULL, h_orientation_lock},
    {BLAST "DisplayAndroidDelegate", "GetDefaultWidth", NULL, h_disp_width},
    {BLAST "DisplayAndroidDelegate", "GetDefaultHeight", NULL, h_disp_height},
    {BLAST "DisplayAndroidDelegate", "GetCurrentWidth", NULL, h_disp_width},
    {BLAST "DisplayAndroidDelegate", "GetCurrentHeight", NULL, h_disp_height},
    {BLAST "DisplayAndroidDelegate", "GetDpiX", NULL, h_dpi},
    {BLAST "DisplayAndroidDelegate", "GetDpiY", NULL, h_dpi},
    {BLAST "DisplayAndroidDelegate", "GetGLView", NULL, h_gl_view},
    {BLAST "DisplayAndroidDelegate", "AttachView", NULL, jni_h_void},
    {BLAST "DisplayAndroidDelegate", "DetachView", NULL, jni_h_void},
    {BLAST "DisplayAndroidDelegate", "BringToFront", NULL, jni_h_void},
    {BLAST "ViewAndroidDelegate", "SetViewFrame", NULL, jni_h_void},

    {BLAST "BatteryAndroidDelegate", "GetBatteryLevel", NULL, h_battery_level},
    {BLAST "BatteryAndroidDelegate", "IsPowerConnected", NULL, h_power_connected},
    {BLAST "PowerManagerAndroid", "ApplyKeepAwake", NULL, jni_h_void},
    {BLAST "AccelerometerAndroidDelegate", "SetEnabled", NULL, jni_h_void},
    {BLAST "AccelerometerAndroidDelegate", "SetUpdateFrequency", NULL, jni_h_void},
    {BLAST "DeviceOrientationHandlerAndroidDelegate", "SetEnabled", NULL, jni_h_void},
    {BLAST "DeviceOrientationHandlerAndroidDelegate", "OnLifeCycleFocusGained", NULL, jni_h_void},
    {BLAST "PhysicalKeyboardAndroidDelegate", "IsPhysicalKeyboardVisible", NULL, jni_h_false},
    {BLAST "PhysicalKeyboardAndroidDelegate", "IsNavigationVisible", NULL, jni_h_false},
    {BLAST "VirtualKeyboardAndroidDelegate", "IsVisible", NULL, jni_h_false},
    {BLAST "VirtualKeyboardAndroidDelegate", NULL, NULL, jni_h_void},
    {BLAST "TouchSurfaceAndroid", "IsTouchScreenMultiTouch", NULL, jni_h_true},
    {BLAST "VibratorAndroidDelegate", "Vibrate", NULL, jni_h_void},
    {BLAST "VibratorAndroidDelegate", "Cancel", NULL, jni_h_void},
    {BLAST "NotificationAndroid", "GenerateUniqueNotificationId", NULL, jni_h_zero},
    {BLAST "NotificationAndroid", "ScheduleLocalNotification", NULL, jni_h_false},
    {BLAST "NotificationAndroid", "CancelLocalNotification", NULL, jni_h_true},
    {BLAST "NotificationAndroid", NULL, NULL, jni_h_void},
    {BLAST "NearFieldCommunicationAndroid", "Init", NULL, jni_h_false},
    {BLAST "NearFieldCommunicationAndroid", "IsAvailable", NULL, jni_h_false},
    {BLAST "NearFieldCommunicationAndroid", "GetPendingNFC", NULL, jni_h_null},
    {BLAST "NearFieldCommunicationAndroid", NULL, NULL, jni_h_void},
    {BLAST "MessageBoxDelegate", "JavaMessageBox", NULL, jni_h_zero},
    {BLAST "LocationManagerAndroid", NULL, NULL, jni_h_zero},

    /* EGL: com.ea.blast.EglAndroidDelegate and EGL10 */
    {BLAST "EglAndroidDelegate", "GetEgl", NULL, ssx_egl_get_egl},
    {BLAST "EglAndroidDelegate", "GetSurface", NULL, ssx_egl_get_surface},
    {BLAST "EglAndroidDelegate", "GetEglNoContext", NULL, ssx_egl_no_context},
    {BLAST "EglAndroidDelegate", "GetEglNoDisplay", NULL, ssx_egl_no_display},
    {BLAST "EglAndroidDelegate", "GetEglNoSurface", NULL, ssx_egl_no_surface},
    {EGL10, NULL, NULL, ssx_egl10_call},

    /* sound */
    {"android/media/AudioTrack", "play", NULL, h_at_play},
    {"android/media/AudioTrack", "stop", NULL, h_at_stop},
    {"android/media/AudioTrack", "pause", NULL, h_at_stop},
    {"android/media/AudioTrack", "flush", NULL, jni_h_void},
    {"android/media/AudioTrack", "release", NULL, jni_h_void},
    {"android/media/AudioTrack", "write", NULL, h_at_write},
    {"android/media/AudioTrack", "getNativeOutputSampleRate", NULL, h_at_rate},
    {"android/media/AudioTrack", "getSampleRate", NULL, h_at_rate},
    {"android/media/AudioTrack", "getState", NULL, h_at_state},
    {"android/media/AudioTrack", "setStereoVolume", NULL, jni_h_zero},

    /* movies */
    {"com/ea/VideoPlayer/PlayerAndroid", "init", NULL, h_pl_init},
    {"com/ea/VideoPlayer/PlayerAndroid", "play", NULL, h_pl_play},
    {"com/ea/VideoPlayer/PlayerAndroid", "stop", NULL, h_pl_stop},
    {"com/ea/VideoPlayer/PlayerAndroid", "pause", NULL, h_pl_pause},
    {"com/ea/VideoPlayer/PlayerAndroid", "isPlaying", NULL, h_pl_is_playing},
    {"com/ea/VideoPlayer/PlayerAndroid", "setViewFrame", NULL, jni_h_void},
    {"com/ea/VideoPlayer/PlayerAndroid", "destroy", NULL, h_pl_destroy},

    /* the APK's assets */
    {"android/content/res/AssetManager", "open", NULL, h_am_open},
    {"android/content/res/AssetManager", "openFd", NULL, h_am_open_fd},
    {"android/content/res/AssetManager", "list", NULL, h_am_list},
    {"android/content/res/AssetFileDescriptor", "getLength", NULL, h_afd_length},
    {"android/content/res/AssetFileDescriptor", "getDeclaredLength", NULL, h_afd_length},
    {"android/content/res/AssetFileDescriptor", "getStartOffset", NULL, h_afd_start},
    {"android/content/res/AssetFileDescriptor", "close", NULL, h_afd_close},
    {"java/io/InputStream", "read", NULL, is_read},
    {"java/io/InputStream", "skip", NULL, h_is_skip},
    {"java/io/InputStream", "available", NULL, h_is_available},
    {"java/io/InputStream", "close", NULL, h_is_close},
    {NULL, NULL, NULL, NULL},
};

/* com.ea.ssx.MainActivity.mActivity (static): the activity */
static jvalue f_activity(JObj *self, const JField *f) { return jv_l(jni_retain(g_ssx_activity)); }

const JFieldDef jni_field_defs[] = {
    {"com/ea/ssx/MainActivity", "mActivity", f_activity, 0, NULL},
    {NULL, NULL, NULL, 0, NULL},
};

const char *const jni_class_supers[][2] = {
    {"com/ea/ssx/Main", "com/ea/ssx/MainActivityGenerated"},
    {"com/ea/ssx/MainActivityGenerated", "com/ea/ssx/MainActivity"},
    {"com/ea/ssx/MainActivity", "com/ea/blast/MainActivity"},
    {"com/ea/blast/MainActivity", "android/app/Activity"},
    {"android/app/Activity", "android/content/Context"},
    {"android/content/res/AssetManager$AssetInputStream", "java/io/InputStream"},
    {"com/ea/blast/VirtualKeyboardAndroidDelegate", "com/ea/blast/KeyboardAndroid"},
    {"com/ea/blast/PhysicalKeyboardAndroid", "com/ea/blast/KeyboardAndroid"},
    {"com/ea/blast/MainThread", "java/lang/Thread"},
    {NULL, NULL},
};

/* classes.txt (made from the APK's classes.dex at setup) answers FindClass;
 * these are the fallback when it is missing. */
const char *const jni_missing_classes[] = {NULL};

void ssx_java_init(void) {
  jni_init();
  g_ssx_activity = jni_singleton("com/ea/ssx/Main");
  g_ssx_asset_manager = jni_singleton("android/content/res/AssetManager");
  g_ssx_audio_track = jni_singleton("android/media/AudioTrack");
  g_ssx_main_thread = jni_singleton("com/ea/blast/MainThread");
}
