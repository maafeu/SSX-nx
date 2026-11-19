/* ssx_clocks.c -- the console's clocks for SSX.
 *
 * What the system gives the game (handheld or docked, and the performance
 * configuration apm holds for each), the clocks actually running (clkrst, read
 * back), and [performance] handheld_gpu: in handheld mode the GPU at 460.8 MHz,
 * through the performance configuration Nintendo added for handheld games
 * (0x92220007: CPU 1020, GPU 460.8, memory 1600 MHz; 0x92220008 the same with
 * the memory at 1331.2 if the first is refused). The CPU stays at 1020 MHz, the
 * clock every retail game runs at; docked play is left to the system (GPU 768).
 *
 * Why: the console run of 2026-11-16 found SSX's busier drops (Curnoe's
 * Moustache, dense trees) render-bound in handheld mode, 55-59 frames a second
 * with the race simulation itself at full speed; at 460.8 MHz the riding
 * windows under 59 fps went from 12 of 43 to 1 of 47 (the default since). Every 10 s the CPU report
 * (dcr_boost_cpu_report: each busy thread's share of a core) and the running
 * clocks go to the log with the [fps] line, so a log shows which limit a drop
 * hits: the render thread's CPU or the GPU. MIT.
 */
#include <stdio.h>
#include <string.h>
#include <switch.h>

#include "dcr_boost.h"
#include "dcr_config.h"
#include "util.h"

static int g_apm, g_clk, g_pcv;
static ClkrstSession g_ses[3];

static void running(char *out, size_t cap) {
  u32 hz[3] = {0, 0, 0};
  if (g_clk) {
    for (int i = 0; i < 3; i++)
      clkrstGetClockRate(&g_ses[i], &hz[i]);
  } else if (g_pcv) {
    static const PcvModule mods[3] = {PcvModule_CpuBus, PcvModule_GPU, PcvModule_EMC};
    for (int i = 0; i < 3; i++)
      pcvGetClockRate(mods[i], &hz[i]);
  } else {
    snprintf(out, cap, "clocks unreadable");
    return;
  }
  snprintf(out, cap, "CPU %u MHz, GPU %.1f MHz, memory %.1f MHz", (unsigned)(hz[0] / 1000000u),
           (double)hz[1] / 1e6, (double)hz[2] / 1e6);
}

static void open_clocks(void) {
  if (hosversionAtLeast(8, 0, 0)) {
    static const PcvModuleId ids[3] = {PcvModuleId_CpuBus, PcvModuleId_GPU, PcvModuleId_EMC};
    if (R_SUCCEEDED(clkrstInitialize())) {
      int ok = 1;
      for (int i = 0; i < 3 && ok; i++)
        ok = R_SUCCEEDED(clkrstOpenSession(&g_ses[i], ids[i], 3));
      g_clk = ok;
    }
  } else if (R_SUCCEEDED(pcvInitialize())) {
    g_pcv = 1;
  }
}

/* GPU busy, as Status Monitor reads it: nvhost-ctrl-gpu's PMU load (tenths of
 * a percent, the GPU's own busy average), sampled 60 times a second by a
 * thread of ours; every 10 s the mean and the highest sample go to the log.
 * Locked 60 needs the peaks, not the mean, under 100%. */
#define NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD 0x80044715
static u32 g_nvfd;
static int g_gpu_ok;
static Mutex g_gpu_lock;
static u64 g_gpu_sum, g_gpu_n;
static u32 g_gpu_max;
static Thread g_gpu_thr;

static void gpu_sampler(void *arg) {
  (void)arg;
  for (;;) {
    svcSleepThread(16666667);
    u32 load = 0;
    if (R_FAILED(nvIoctl(g_nvfd, NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD, &load)))
      continue;
    mutexLock(&g_gpu_lock);
    g_gpu_sum += load, g_gpu_n++;
    if (load > g_gpu_max)
      g_gpu_max = load;
    mutexUnlock(&g_gpu_lock);
  }
}

static void gpu_start(void) {
  Result rc = nvInitialize();
  if (R_SUCCEEDED(rc))
    rc = nvOpen(&g_nvfd, "/dev/nvhost-ctrl-gpu");
  u32 probe = 0;
  if (R_SUCCEEDED(rc))
    rc = nvIoctl(g_nvfd, NVGPU_GPU_IOCTL_PMU_GET_GPU_LOAD, &probe);
  if (R_FAILED(rc)) {
    debugPrintf("[gpu] the GPU's busy counter is not readable here (0x%x)\n", (unsigned)rc);
    return;
  }
  if (R_FAILED(threadCreate(&g_gpu_thr, gpu_sampler, NULL, NULL, 0x2000, 0x2C, -2)) ||
      R_FAILED(threadStart(&g_gpu_thr)))
    return;
  g_gpu_ok = 1;
  debugPrintf("[gpu] busy counter read 60 times a second (now %u.%u%%)\n", (unsigned)probe / 10,
              (unsigned)probe % 10);
}

void ssx_clocks_start(void) {
  if (dcr_is_emulator()) {
    debugPrintf("[clocks] emulator: clocks left as they are\n");
    return;
  }
  gpu_start();
  int docked = appletGetOperationMode() == AppletOperationMode_Console;
  g_apm = R_SUCCEEDED(apmInitialize());
  open_clocks();
  u32 hh = 0, dk = 0;
  if (g_apm) {
    apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &hh);
    apmGetPerformanceConfiguration(ApmPerformanceMode_Boost, &dk);
  }
  char now[96];
  running(now, sizeof now);
  debugPrintf("[clocks] %s; performance configurations: handheld 0x%08x, docked 0x%08x; running: %s\n",
              docked ? "docked" : "handheld", (unsigned)hh, (unsigned)dk, now);
  if (!dcr_config()->handheld_gpu)
    return;
  if (!g_apm) {
    debugPrintf("[clocks] handheld_gpu 460: apm unavailable, the system's clocks stay\n");
    return;
  }
  static const u32 want[2] = {0x92220007u, 0x92220008u};
  Result rc = 0;
  int k;
  for (k = 0; k < 2; k++)
    if (R_SUCCEEDED(rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, want[k])))
      break;
  u32 got = 0;
  apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &got);
  if (k < 2)
    debugPrintf("[clocks] handheld GPU 460.8 MHz: configuration 0x%08x set (handheld now 0x%08x)\n",
                (unsigned)want[k], (unsigned)got);
  else
    debugPrintf("[clocks] handheld GPU 460.8 MHz refused (0x%x): handheld stays 0x%08x\n", (unsigned)rc,
                (unsigned)got);
}

/* Every 10 s (ssx_boot.c's report): the CPU report, and the clocks when they
 * changed since the last line (a load's boost, a dock or undock). */
void ssx_clocks_report(void) {
  if (dcr_is_emulator())
    return;
  dcr_boost_cpu_report();
  if (g_gpu_ok) {
    mutexLock(&g_gpu_lock);
    u64 sum = g_gpu_sum, n = g_gpu_n;
    u32 max = g_gpu_max;
    g_gpu_sum = g_gpu_n = 0, g_gpu_max = 0;
    mutexUnlock(&g_gpu_lock);
    if (n)
      debugPrintf("[gpu] busy %llu.%llu%% on average, %u.%u%% at most (%llu samples)\n",
                  (unsigned long long)(sum / n / 10), (unsigned long long)(sum / n % 10), (unsigned)max / 10,
                  (unsigned)max % 10, (unsigned long long)n);
  }
  static char last[96];
  char now[96];
  running(now, sizeof now);
  if (strcmp(now, last)) {
    int docked = appletGetOperationMode() == AppletOperationMode_Console;
    debugPrintf("[clocks] running (%s): %s\n", docked ? "docked" : "handheld", now);
    snprintf(last, sizeof last, "%s", now);
  }
}
