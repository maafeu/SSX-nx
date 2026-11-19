/* ssx_saltynx.c -- [display] saltynx: SSX's frame rate in Status Monitor Overlay.
 *
 * Status Monitor Overlay (Tesla) shows a game's FPS from SaltyNX's NX-FPS
 * plugin: NX-FPS, injected into a retail game, hooks its present call and
 * writes an NxFpsSharedBlock into the 4 KB shared memory SaltyNX's
 * saltysd_proc owns; the overlay maps that memory and takes the first block
 * whose magic is 0x465053. SaltyNX does not inject into this program (a
 * homebrew title id: saltysd_proc's hijack.c skips ids above
 * 0x01FFFFFFFFFFFFFF), and the Android game has no NVN or SDK present for it
 * to hook. So the port does NX-FPS's part itself: before libnx opens sm
 * (an application may hold only one session from a named port, and sm keeps
 * it: port_app_init_early) it asks saltysd_proc for room and the memory
 * handle, and before every swap it writes what NX-FPS would.
 *
 * The protocol, read in the sources (SaltyNX 2.1.4 @6d4b272: saltysd_proc/
 * source/service.c, saltysd_core/source/NX-FPS*.{cpp,hpp}; Status-Monitor-
 * Overlay 1.4.1 @0434c0b: source/Utils.hpp, include/SaltyNX.h):
 *   - named port "SaltySD" (one session: saltysd_proc's main loop waits while
 *     it is open, so it is closed again at once), old-style IPC: a Request
 *     with the PID sent, raw {u64 'SFCI', u64 command, args}, reply raw
 *     {u64 'SFCO', u64 result, ...}. 6 CheckIfSharedMemoryAvailable(u64 size)
 *     -> offset (a bump allocator; bytes 0-3 are saltysd_proc's own, so a
 *     dummy byte is asked for first, as Core does); 7 GetSharedMemoryHandle
 *     -> a copy handle; 0 ends the session. No PID check on these.
 *   - the block: Status Monitor 1.4.1's 174-byte NxFpsSharedBlock (packed;
 *     SaltyNX's own appends performance counters the overlay does not read).
 *     The overlay shows FPS (frames in the last second) checked against the
 *     mean of FPSticks (system ticks between frames, 19.2 MHz), draws its
 *     graph point when frameNumber changes, and takes the game as running
 *     only if pluginActive, which it clears, is set again within 100 ms: it
 *     is set every frame. API stays 0 (no resolution handshake).
 *   - saltysd_proc never resets its allocator or clears old blocks for a
 *     homebrew title: a block left by an earlier run (ours, or a game's) is
 *     cleared here before ours is written (the overlay takes the first
 *     magic), and ours is marked 'SSXF' when the game leaves, to be reused by
 *     the next start instead of taking more of the 4 KB. MIT.
 */
#include <stdint.h>
#include <string.h>
#include <switch.h>

#include "dcr_config.h"
#include "util.h"

#define SFCI 0x49434653ull
#define SFCO 0x4f434653ull
#define NXFPS_MAGIC 0x465053u
#define SSX_MARK 0x46585353u /* "SSXF": our block, the game gone */
#define SHMEM_SIZE 0x1000

typedef struct {
  uint16_t width, height, calls;
} __attribute__((packed)) ResolutionCalls;

typedef struct {
  uint32_t MAGIC;
  uint8_t FPS;
  float FPSavg;
  uint8_t pluginActive;
  uint8_t FPSlocked;
  uint8_t FPSmode;
  uint8_t ZeroSync;
  uint8_t patchApplied;
  uint8_t API;
  uint32_t FPSticks[10];
  uint8_t Buffers, SetBuffers, ActiveBuffers, SetActiveBuffers;
  uint8_t displaySync;
  ResolutionCalls renderCalls[8];
  ResolutionCalls viewportCalls[8];
  uint8_t forceOriginalRefreshRate;
  uint8_t dontForce60InDocked;
  uint8_t forceSuspend;
  uint8_t currentRefreshRate;
  float readSpeedPerSecond;
  uint8_t FPSlockedDocked;
  uint64_t frameNumber;
  int8_t expectedSetBuffers;
} __attribute__((packed)) NxFpsSharedBlock;
_Static_assert(sizeof(NxFpsSharedBlock) == 174, "Status Monitor 1.4.1's NxFpsSharedBlock is 174 bytes");

static SharedMemory g_shm;
static volatile NxFpsSharedBlock *g_block;
static uint64_t g_last_tick, g_second_tick;
static unsigned g_tick_i, g_frames_this_second;
static uint64_t g_frame_number;

/* One old-style IPC request on the session: raw {SFCI, cmd, arg}. Returns the
 * kernel's result, else the reply's; *out_val the first u64 after the result,
 * *out_handle the first copy handle. */
static Result salty_request(Handle h, uint64_t cmd, uint64_t arg, uint64_t *out_val, Handle *out_handle) {
  uint32_t *buf = (uint32_t *)armGetTls();
  memset(buf, 0, 0x100);
  buf[0] = 4;                /* Request, no buffer descriptors */
  buf[1] = 14u | 0x80000000; /* raw size in words, with the 16-byte padding allowance (Status Monitor's
                                largest request is 40 bytes: 10 + 4); a handle descriptor follows */
  buf[2] = 1;                /* send the PID (the kernel fills buf[3..4]) */
  uint64_t *raw = (uint64_t *)(buf + 8); /* 20 bytes of header, padded to 32 */
  raw[0] = SFCI;
  raw[1] = cmd;
  raw[2] = arg;
  Result rc = svcSendSyncRequest(h);
  if (R_FAILED(rc))
    return rc;
  unsigned i = 2, copies = 0;
  Handle first_copy = 0;
  if (buf[1] & 0x80000000) {
    uint32_t d = buf[i++];
    if (d & 1)
      i += 2; /* a PID */
    copies = (d >> 1) & 15;
    unsigned moves = (d >> 5) & 15;
    if (copies)
      first_copy = buf[i];
    i += copies + moves;
  }
  i += ((buf[0] >> 16) & 15) * 2;                                              /* X descriptors */
  i += (((buf[0] >> 20) & 15) + ((buf[0] >> 24) & 15) + ((buf[0] >> 28) & 15)) * 3; /* A, B, W */
  uintptr_t r = ((uintptr_t)(buf + i) + 15) & ~(uintptr_t)15;
  const uint64_t *reply = (const uint64_t *)r;
  if (reply[0] != SFCO)
    return MAKERESULT(Module_Libnx, LibnxError_BadInput);
  if (out_val)
    *out_val = reply[2];
  if (out_handle && copies)
    *out_handle = first_copy;
  return (Result)reply[1];
}

/* What the handshake before sm found (port_app_init_early): logged and used
 * once config.ini is read (ssx_saltynx_start). */
static Result g_hs_port_rc, g_hs_rc, g_hs_rc6;
static int g_hs_tried;
static uint8_t *g_hs_base;
static long g_hs_off = -1;
static int g_hs_reused;

/* runtime/source/nx_init.c, first thing in __appInit, before sm takes this
 * application's one named-port session (1 of 1 on the console: tried after
 * sm, both SaltyNX ports answered LimitReached, 0x10801). No log yet. */
void port_app_init_early(void) {
  Handle h = 0;
  g_hs_tried = 1;
  for (int i = 0; i < 100; i++) { /* the port takes one client at a time */
    g_hs_port_rc = svcConnectToNamedPort(&h, "SaltySD");
    if (g_hs_port_rc != KERNELRESULT(OutOfSessions)) /* only "busy" is worth waiting for */
      break;
    svcSleepThread(10000000ll);
  }
  if (R_FAILED(g_hs_port_rc))
    return;
  Handle mem = 0;
  g_hs_rc = salty_request(h, 7, 0, NULL, &mem);
  if (R_SUCCEEDED(g_hs_rc) && mem) {
    shmemLoadRemote(&g_shm, mem, SHMEM_SIZE, Perm_Rw);
    g_hs_rc = shmemMap(&g_shm);
  }
  uint8_t *base = R_SUCCEEDED(g_hs_rc) && mem ? shmemGetAddr(&g_shm) : NULL;
  if (base) {
    /* A block already there is reused: ours of an earlier start (marked at
     * exit), else any NX-FPS block -- one application runs at a time, so it
     * is a closed program's (ours killed from the HOME menu, which never
     * reaches the exit path, or a game's). saltysd_proc's allocator only
     * resets when it injects a retail game; without reuse every start would
     * take 176 more bytes of its 4 KB. */
    for (int pass = 0; pass < 2 && g_hs_off < 0; pass++)
      for (unsigned o = 4; o + sizeof(NxFpsSharedBlock) <= SHMEM_SIZE; o += 4)
        if (*(volatile uint32_t *)(base + o) == (pass ? NXFPS_MAGIC : SSX_MARK)) {
          g_hs_off = o;
          g_hs_reused = 1;
          break;
        }
    if (g_hs_off < 0) {
      uint64_t dummy = 0, off = 0;
      g_hs_rc6 = salty_request(h, 6, 1, &dummy, NULL); /* not bytes 0-3: saltysd_proc writes there */
      if (R_SUCCEEDED(g_hs_rc6))
        g_hs_rc6 = salty_request(h, 6, sizeof(NxFpsSharedBlock), &off, NULL);
      if (R_SUCCEEDED(g_hs_rc6) && off >= 4 && !(off & 3) && off + sizeof(NxFpsSharedBlock) <= SHMEM_SIZE) {
        g_hs_off = (long)off;
        /* ours from now: a start that restarts before writing it (the launcher
         * installing a new build) leaves it marked for the next one */
        *(volatile uint32_t *)(base + off) = SSX_MARK;
      }
    }
  }
  salty_request(h, 0, 0, NULL, NULL); /* end the session: saltysd_proc waits while it is open */
  svcCloseHandle(h);
  g_hs_base = base;
}

void ssx_saltynx_start(void) {
  uint8_t *base = g_hs_base;
  if (!dcr_config()->saltynx || dcr_is_emulator()) {
    if (base)
      shmemClose(&g_shm);
    return;
  }
  if (!g_hs_tried || R_FAILED(g_hs_port_rc)) {
    debugPrintf("[saltynx] SaltyNX is not running: no FPS for Status Monitor (SaltySD port 0x%x%s)\n",
                (unsigned)g_hs_port_rc, g_hs_port_rc == KERNELRESULT(NotFound) ? ": not installed" : "");
    return;
  }
  if (!base) {
    debugPrintf("[saltynx] no shared memory from SaltyNX (0x%x): no FPS for Status Monitor\n", (unsigned)g_hs_rc);
    return;
  }
  if (g_hs_off < 0) {
    debugPrintf("[saltynx] SaltyNX has no room left in its shared memory (0x%x): no FPS for Status Monitor\n",
                (unsigned)g_hs_rc6);
    shmemClose(&g_shm);
    g_hs_base = NULL;
    return;
  }
  unsigned off = (unsigned)g_hs_off;
  /* blocks of games gone: the overlay takes the first magic it finds */
  for (unsigned o = 0; o + 4 <= SHMEM_SIZE; o += 4)
    if (o != off && *(volatile uint32_t *)(base + o) == NXFPS_MAGIC)
      *(volatile uint32_t *)(base + o) = 0;
  NxFpsSharedBlock *b = (NxFpsSharedBlock *)(base + off);
  memset(b, 0, sizeof *b);
  b->FPSmode = 1;
  b->currentRefreshRate = 60;
  b->expectedSetBuffers = -1;
  b->pluginActive = 1;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  b->MAGIC = NXFPS_MAGIC;
  g_block = b;
  debugPrintf("[saltynx] the frame rate goes to SaltyNX's shared memory at offset %u%s (Status Monitor's FPS)\n", off,
              g_hs_reused ? ", the block of an earlier start" : "");
}

/* Before every swap (port_gl_before_swap): one frame. */
void ssx_saltynx_frame(void) {
  volatile NxFpsSharedBlock *b = g_block;
  if (!b)
    return;
  uint64_t now = armGetSystemTick();
  if (g_last_tick) {
    uint64_t d = now - g_last_tick;
    b->FPSticks[g_tick_i] = d > 0xFFFFFFFFull ? 0xFFFFFFFFu : (uint32_t)d;
    g_tick_i = (g_tick_i + 1) % 10;
    uint64_t sum = 0;
    for (int i = 0; i < 10; i++)
      sum += b->FPSticks[i];
    if (sum)
      b->FPSavg = (float)(19200000.0 * 10.0 / (double)sum);
  }
  g_last_tick = now;
  g_frames_this_second++;
  if (!g_second_tick)
    g_second_tick = now;
  if (now - g_second_tick >= 19200000ull) {
    b->FPS = g_frames_this_second > 255 ? 255 : (uint8_t)g_frames_this_second;
    g_frames_this_second = 0;
    g_second_tick = now;
  }
  b->frameNumber = ++g_frame_number;
  b->pluginActive = 1;
}

/* The game leaves: our block says so (not the magic: the overlay would wait
 * on a dead game), and the next start finds it by this mark. */
void ssx_saltynx_stop(void) {
  volatile NxFpsSharedBlock *b = g_block;
  if (!b)
    return;
  g_block = NULL;
  b->pluginActive = 0;
  b->MAGIC = SSX_MARK;
  shmemClose(&g_shm);
}
