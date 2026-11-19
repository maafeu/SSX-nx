/* ssx_abort.c -- what a libnx or Mesa abort leaves behind.
 *
 * fatalThrow() and diagAbortWithResult() end the program with an error code on
 * the screen (2345-0008, 2349-0004 ...) and no crash.log: neither is an
 * exception, so exc_handler.c never sees it, and debug.log, kept open for
 * writing, loses what it had not flushed. Mesa's Switch EGL driver calls
 * fatalThrow() when the window refuses a buffer; libnx aborts when a thread or
 * a service is used in a state it does not expect. Both are wrapped here
 * (--wrap in the Makefile): the result, the caller, the thread and the code
 * addresses on its stack go to the log, and the newest log lines to abort.log
 * (ssx_livelog.c), then the real call ends the program as before. MIT.
 */
#include <stdint.h>
#include <stdio.h>
#include <switch.h>

#include "bionic_pthread.h"
#include "exc_handler.h"
#include "util.h"

void ssx_livelog_dump(const char *name); /* ssx_livelog.c */
const char *dcr_addr_name(uint32_t a, char *buf, size_t cap); /* exc_handler.c */
int dcr_is_code_addr(uint32_t a);

void NORETURN __real_fatalThrow(Result rc);
void NORETURN __real_diagAbortWithResult(Result rc);

static void note(const char *what, Result rc, uint32_t from, uint32_t sp) {
  static int once;
  if (__atomic_exchange_n(&once, 1, __ATOMIC_SEQ_CST))
    return; /* a second thread on its way down: the first one's report stands */
  BThread *t = b_thread_self();
  u64 tid = 0;
  svcGetThreadId(&tid, CUR_THREAD_HANDLE);
  char b[128];
  debugPrintf("[abort] %s(0x%x: error %04u-%04u) from %s, thread %llu (%s)\n", what, (unsigned)rc,
              2000 + R_MODULE(rc), R_DESCRIPTION(rc), dcr_addr_name(from, b, sizeof b), (unsigned long long)tid,
              t && t->name[0] ? t->name : "not the game's");
  /* the code addresses on its stack, nearest first: the calls that led here */
  size_t avail = dcr_readable(sp, 4096) / 4;
  const uint32_t *p = (const uint32_t *)(uintptr_t)sp;
  for (size_t i = 0, n = 0; i < avail && n < 24; i++)
    if (dcr_is_code_addr(p[i])) {
      debugPrintf("[abort]   sp+0x%03x: %s\n", (unsigned)(i * 4), dcr_addr_name(p[i], b, sizeof b));
      n++;
    }
  ssx_livelog_dump("abort.log");
  log_flush_ring();
  fflush(NULL);
}

static inline uint32_t cur_sp(void) {
  uint32_t sp;
  __asm__ volatile("mov %0, sp" : "=r"(sp));
  return sp;
}

void NORETURN __wrap_fatalThrow(Result rc) {
  note("fatalThrow", rc, (uint32_t)(uintptr_t)__builtin_return_address(0), cur_sp());
  __real_fatalThrow(rc);
}

void NORETURN __wrap_diagAbortWithResult(Result rc) {
  note("diagAbortWithResult", rc, (uint32_t)(uintptr_t)__builtin_return_address(0), cur_sp());
  __real_diagAbortWithResult(rc);
}
