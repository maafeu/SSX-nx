/* ssx_hook.h -- hooking libgame.so by address before it is sealed
 * (ssx_hook.c). Addresses are libgame.so 0.0.8430 vaddrs; `expect` is the
 * first word that build has there. MIT. */
#ifndef SSX_HOOK_H
#define SSX_HOOK_H

#include <stdint.h>

#include "so_util.h"

void ssx_hook_begin(so_module *m);
void ssx_hook_end(void);
/* vaddr jumps to dst; returns the original, callable, or NULL (no hook) */
void *ssx_hook(uint32_t vaddr, uint32_t expect, void *dst, const char *what);
/* vaddr jumps to dst, the original lost; 1 done */
int ssx_hook_jump(uint32_t vaddr, uint32_t expect, void *dst);
int ssx_patch_word(uint32_t vaddr, uint32_t expect, uint32_t value);
int ssx_hook_word_is(uint32_t vaddr, uint32_t expect);
void ssx_poke_word(uint32_t vaddr, uint32_t value);
uintptr_t ssx_hook_live(uint32_t vaddr);
uint32_t ssx_hook_alloc(uint32_t words); /* room in the trampolines' space: its vaddr, or 0 */

#endif
