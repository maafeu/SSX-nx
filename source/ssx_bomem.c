/* ssx_bomem.c -- the memory behind Mesa's GPU buffer objects: big blocks are
 * kept for reuse instead of going back to the heap.
 *
 * libdrm_nouveau gives each buffer object page-aligned heap memory
 * (memalign(0x1000, size)) and frees it when the object goes. At every drop
 * load and Restart the game frees a 1280x720 texture (3,840 KB) and makes a
 * new one. A memalign of that size needs a free chunk a little bigger than
 * the hole the old one leaves, so each load took fresh memory from the end of
 * the heap and the hole went to small allocations: the arena grew ~4 MB a
 * load until the heap's end (console sessions bo1 and df1, 2026-11-17/18).
 *
 * libdrm (tools/libdrm/bo-mem-hooks.patch) calls these two instead of
 * memalign/free; blocks of 1 MB and more are kept on free (up to 24 MB, 16
 * blocks, the least recently freed going back to the heap first) and handed
 * out again for an object of exactly that size. A memalign that fails gives
 * every kept block back and tries once more. MIT.
 */
#include <malloc.h>
#include <stdint.h>
#include <stdlib.h>
#include <switch.h>

#include "util.h"

#define BIG (1u << 20)
#define SLOTS 16
#define CAP (24u << 20)

static Mutex g_mx;
static struct {
  void *mem;
  size_t size;
  uint32_t age;
} g_kept[SLOTS];
static size_t g_kept_bytes;
static uint32_t g_clock;
static uint32_t g_reused, g_new_big, g_returned, g_flushes;
static uint64_t g_reused_bytes;

/* under g_mx: the least recently freed kept block back to the heap */
static void give_back_oldest(void) {
  int o = -1;
  for (int i = 0; i < SLOTS; i++)
    if (g_kept[i].mem && (o < 0 || g_kept[i].age < g_kept[o].age))
      o = i;
  if (o < 0)
    return;
  free(g_kept[o].mem);
  g_kept_bytes -= g_kept[o].size;
  g_kept[o].mem = NULL;
  g_returned++;
}

void *nouveau_switch_bo_mem_alloc(size_t size) {
  if (size >= BIG) {
    mutexLock(&g_mx);
    int best = -1;
    for (int i = 0; i < SLOTS; i++) /* exactly this size, the most recently freed */
      if (g_kept[i].mem && g_kept[i].size == size && (best < 0 || g_kept[i].age > g_kept[best].age))
        best = i;
    if (best >= 0) {
      void *m = g_kept[best].mem;
      g_kept[best].mem = NULL;
      g_kept_bytes -= size;
      g_reused++;
      g_reused_bytes += size;
      mutexUnlock(&g_mx);
      return m;
    }
    g_new_big++;
    mutexUnlock(&g_mx);
  }
  void *m = memalign(0x1000, size);
  if (!m) {
    mutexLock(&g_mx);
    const int had = g_kept_bytes != 0;
    while (g_kept_bytes)
      give_back_oldest();
    g_flushes += had;
    mutexUnlock(&g_mx);
    if (had)
      m = memalign(0x1000, size);
  }
  return m;
}

void nouveau_switch_bo_mem_free(void *mem, size_t size) {
  if (!mem)
    return;
  if (size < BIG || size > CAP) {
    free(mem);
    return;
  }
  mutexLock(&g_mx);
  int slot = -1;
  for (int i = 0; i < SLOTS && slot < 0; i++)
    if (!g_kept[i].mem)
      slot = i;
  while (slot < 0 || g_kept_bytes + size > CAP) {
    give_back_oldest();
    for (int i = 0; i < SLOTS && slot < 0; i++)
      if (!g_kept[i].mem)
        slot = i;
  }
  g_kept[slot].mem = mem, g_kept[slot].size = size, g_kept[slot].age = ++g_clock;
  g_kept_bytes += size;
  mutexUnlock(&g_mx);
}

/* From the 10 s report: a line whenever something has changed. */
void ssx_bomem_report(void) {
  static uint32_t last;
  const uint32_t sum = g_reused + g_new_big + g_returned + g_flushes;
  if (sum == last)
    return;
  last = sum;
  debugPrintf("[bomem] GPU buffer blocks of 1 MB+: %u reused (%llu MB), %u new, %u given back to the heap, %u "
              "flushes; %u KB kept now\n",
              (unsigned)g_reused, (unsigned long long)(g_reused_bytes >> 20), (unsigned)g_new_big,
              (unsigned)g_returned, (unsigned)g_flushes, (unsigned)(g_kept_bytes >> 10));
}
