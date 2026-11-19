/* ssx_emufp.c -- EMULATOR ONLY: NEON's Newton-Raphson steps, ARM's way.
 *
 * The riders were invisible under Ryujinx: some of their bones came out NaN
 * (docs/SSX-notes.md). The cause is in Ryujinx's "fast FP" on x86, which it
 * cannot turn off without a rebuild: it computes
 *   VRECPS  Vd, Vn, Vm   (the reciprocal's step,  2 - n*m)
 *   VRSQRTS Vd, Vn, Vm   (the 1/sqrt's step, (3 - n*m) / 2)
 * with a plain multiply (ARMeilleure, InstEmitSimdArithmetic32.cs: Vrecps /
 * Vrsqrts, the X86Mulps path). ARM defines both with one exception: when one
 * operand is zero and the other infinite, the result is exactly 2.0 (1.5),
 * not NaN (the ARM ARM's FPRecipStep / FPRSqrtStep). That is what makes the
 * usual estimate-and-refine sequences give a finite answer for an input of
 * zero -- a zero-length vector normalised, a zero weight inverted -- on the
 * console; under the emulator they give NaN, and the NaN spreads. libgame.so
 * has 585 VRECPS and 1,103 VRSQRTS (RenderWare's NEON vector library, rw::
 * math::vpu, in 383 ARM functions).
 *
 * Under the emulator each one becomes a branch to a stub of instructions
 * Ryujinx computes exactly as the hardware does:
 *
 *   vpush  T1..T4               four scratch Q registers no operand touches
 *   vmul.f32 T1, Vn, Vm
 *   vmov.f32 T2, #2.0           (VRSQRTS: #3.0, then the subtraction, then
 *   vsub.f32 T1, T2, T1          vmov #0.5 / vmul, and T2 = #1.5)
 *   vceq.f32 T3, T1, T1         lanes whose result is a number
 *   vceq.f32 T4, Vn, Vn         lanes whose n is
 *   vbic     T4, T4, T3         n a number, result NaN ...
 *   vceq.f32 T3, Vm, Vm
 *   vand     T4, T4, T3         ... m a number too: zero times infinity
 *   vbsl     T4, T2, T1         those lanes 2.0 (1.5), the others the result
 *   vorr     Vd, T4, T4
 *   vpop   T4..T1
 *   b      <next instruction>
 *
 * The stubs live in room added after libgame.so's image (port_so_extra_bytes,
 * so_util.c: mapped with it, within a branch's reach). On the console none of
 * this happens: the hardware computes the steps itself. MIT.
 */
#include <elf.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "so_util.h"
#include "util.h"

#define POOL_BYTES (320u << 10)

/* so_util.c (weak there): the room, for libgame.so under the emulator only */
size_t port_so_extra_bytes(const char *filename) {
  if (!dcr_is_emulator() || !filename)
    return 0;
  const size_t n = strlen(filename);
  return n >= 10 && !strcmp(filename + n - 10, "libgame.so") ? POOL_BYTES : 0;
}

static uint32_t branch(uint32_t cond, uint32_t from, uint32_t to) {
  const int32_t off = (int32_t)(to - (from + 8)) >> 2;
  return cond << 28 | 0x0A000000u | ((uint32_t)off & 0x00FFFFFFu);
}
static uint32_t dfield(uint32_t d) { return (d >> 4 & 1) << 22 | (d & 15) << 12; } /* D:Vd */
static uint32_t nfield(uint32_t n) { return (n >> 4 & 1) << 7 | (n & 15) << 16; }  /* N:Vn */
static uint32_t mfield(uint32_t m) { return (m >> 4 & 1) << 5 | (m & 15); }        /* M:Vm */
static uint32_t vpush_q(uint32_t q) { return 0xED2D0B04u | dfield(2 * q); }
static uint32_t vpop_q(uint32_t q) { return 0xECBD0B04u | dfield(2 * q); }
static uint64_t dmask(uint32_t d, int quad) { return (quad ? 3ull : 1ull) << (quad ? (d & ~1u) : d); }
static uint32_t pick_q(uint64_t used) {
  for (uint32_t q = 15; q >= 4; q--)
    if (!(used >> (2 * q) & 3))
      return q;
  return 0;
}
/* vmov.f32 Vd, #imm8 (VFPExpandImm): 2.0 0x00, 3.0 0x08, 0.5 0x60, 1.5 0x78 */
static uint32_t vmov_f32(uint32_t qb, uint32_t d, uint32_t imm8) {
  return 0xF2800F10u | qb | dfield(d) | (imm8 >> 7) << 24 | ((imm8 >> 4) & 7) << 16 | (imm8 & 15);
}

/* VRECPS / VRSQRTS (A1, .F32): 1111 0010 0 D op 0 Vn Vd 1111 N Q M 1 Vm */
static int step_kind(uint32_t w) {
  if ((w & 0xFFB00F10u) == 0xF2000F10u)
    return 1; /* vrecps */
  if ((w & 0xFFB00F10u) == 0xF2200F10u)
    return 2; /* vrsqrts */
  return 0;
}

/* Is the word at off read as data by a pc-relative ldr/vldr nearby (a
 * literal that only looks like the instruction)? */
static int is_literal(const uint8_t *img, uint32_t lo, uint32_t hi, uint32_t off) {
  for (int32_t k = -256; k <= 1024; k++) {
    const uint32_t at = off - 4u * (uint32_t)k;
    if (k == 0 || at < lo || at + 4 > hi)
      continue;
    uint32_t w;
    memcpy(&w, img + at, 4);
    if ((w >> 28) == 0xF)
      continue;
    const uint32_t pc = at + 8;
    if ((w & 0x0E5F0000u) == 0x041F0000u) {
      const uint32_t imm = w & 0xFFF;
      if (((w >> 23) & 1 ? pc + imm : pc - imm) == off)
        return 1;
    } else if ((w & 0x0F3F0E00u) == 0x0D1F0A00u) {
      const uint32_t imm = (w & 0xFF) * 4;
      const uint32_t t = (w >> 23) & 1 ? pc + imm : pc - imm;
      if (t == off || ((w & 0x100) && t + 4 == off))
        return 1;
    }
  }
  return 0;
}

typedef struct {
  uint32_t *at, *end;
  int fixed[3], skipped;
} Pool;

static void fix_step(uint32_t *site, uint32_t w, int kind, Pool *p) {
  uint32_t s[24], k = 0;
  const uint32_t qb = w & 0x40u;
  const int quad = qb != 0;
  const uint32_t d = (w >> 22 & 1) << 4 | (w >> 12 & 15), n = (w >> 7 & 1) << 4 | (w >> 16 & 15),
                 m = (w >> 5 & 1) << 4 | (w & 15);
  uint64_t used = dmask(d, quad) | dmask(n, quad) | dmask(m, quad);
  uint32_t t[4];
  for (int i = 0; i < 4; i++) {
    t[i] = pick_q(used);
    if (!t[i]) {
      p->skipped++;
      return;
    }
    used |= dmask(2 * t[i], 1);
  }
  const uint32_t T1 = 2 * t[0], T2 = 2 * t[1], T3 = 2 * t[2], T4 = 2 * t[3]; /* their D numbers */
  for (int i = 0; i < 4; i++)
    s[k++] = vpush_q(t[i]);
  s[k++] = 0xF3000D10u | qb | dfield(T1) | nfield(n) | mfield(m);     /* vmul.f32 T1, n, m */
  s[k++] = vmov_f32(qb, T2, kind == 1 ? 0x00 : 0x08);                 /* #2.0 / #3.0 */
  s[k++] = 0xF2200D00u | qb | dfield(T1) | nfield(T2) | mfield(T1);   /* vsub.f32 T1, T2, T1 */
  if (kind == 2) {
    s[k++] = vmov_f32(qb, T2, 0x60);                                  /* #0.5 */
    s[k++] = 0xF3000D10u | qb | dfield(T1) | nfield(T1) | mfield(T2); /* vmul.f32 T1, T1, T2 */
    s[k++] = vmov_f32(qb, T2, 0x78);                                  /* #1.5 */
  }
  s[k++] = 0xF2000E00u | qb | dfield(T3) | nfield(T1) | mfield(T1);   /* vceq.f32 T3, T1, T1 */
  s[k++] = 0xF2000E00u | qb | dfield(T4) | nfield(n) | mfield(n);     /* vceq.f32 T4, n, n */
  s[k++] = 0xF2100110u | qb | dfield(T4) | nfield(T4) | mfield(T3);   /* vbic T4, T4, T3 */
  s[k++] = 0xF2000E00u | qb | dfield(T3) | nfield(m) | mfield(m);     /* vceq.f32 T3, m, m */
  s[k++] = 0xF2000110u | qb | dfield(T4) | nfield(T4) | mfield(T3);   /* vand T4, T4, T3 */
  s[k++] = 0xF3100110u | qb | dfield(T4) | nfield(T2) | mfield(T1);   /* vbsl T4, T2, T1 */
  s[k++] = 0xF2200110u | qb | dfield(d) | nfield(T4) | mfield(T4);    /* vorr d, T4, T4 */
  for (int i = 3; i >= 0; i--)
    s[k++] = vpop_q(t[i]);
  if (p->at + k + 1 > p->end) {
    p->skipped++;
    return;
  }
  uint32_t *stub = p->at;
  memcpy(stub, s, 4 * k);
  stub[k] = branch(0xE, (uint32_t)(uintptr_t)(stub + k), (uint32_t)(uintptr_t)(site + 1));
  *site = branch(0xE, (uint32_t)(uintptr_t)site, (uint32_t)(uintptr_t)stub);
  p->at += k + 1;
  p->fixed[kind]++;
}

/* The file's .symtab (malloc'd), or NULL. */
static Elf32_Sym *read_symtab(const char *path, int *count) {
  FILE *f = path ? fopen(path, "rb") : NULL;
  if (!f)
    return NULL;
  Elf32_Ehdr eh;
  Elf32_Sym *syms = NULL;
  if (fread(&eh, sizeof eh, 1, f) != 1 || eh.e_shentsize != sizeof(Elf32_Shdr) || !eh.e_shnum)
    goto out;
  Elf32_Shdr *sh = malloc(eh.e_shnum * sizeof *sh);
  if (!sh)
    goto out;
  if (fseek(f, (long)eh.e_shoff, SEEK_SET) == 0 && fread(sh, sizeof *sh, eh.e_shnum, f) == eh.e_shnum)
    for (int i = 0; i < eh.e_shnum; i++)
      if (sh[i].sh_type == SHT_SYMTAB && sh[i].sh_size >= sizeof(Elf32_Sym)) {
        syms = malloc(sh[i].sh_size);
        if (syms && (fseek(f, (long)sh[i].sh_offset, SEEK_SET) != 0 || fread(syms, 1, sh[i].sh_size, f) != sh[i].sh_size)) {
          free(syms);
          syms = NULL;
        }
        if (syms)
          *count = (int)(sh[i].sh_size / sizeof(Elf32_Sym));
        break;
      }
  free(sh);
out:
  fclose(f);
  return syms;
}

/* ssx_patch.c, in the hook window (the image still in its staging copy) */
void ssx_emufp_patch(so_module *m) {
  if (!dcr_is_emulator())
    return;
  uint8_t *img = m->load_base;
  uint32_t xlo = 0, xhi = 0, end = 0;
  for (int i = 0; i < m->phnum; i++) {
    const Elf32_Phdr *ph = &m->phdr[i];
    if (ph->p_type != PT_LOAD)
      continue;
    if (ph->p_vaddr + ph->p_memsz > end)
      end = ph->p_vaddr + ph->p_memsz;
    if (ph->p_flags & PF_X)
      xlo = ph->p_vaddr, xhi = ph->p_vaddr + ph->p_filesz;
  }
  const uint32_t pool_lo = (end + 15) & ~15u;
  if (!xhi || pool_lo + 4096 > m->load_size) {
    debugPrintf("[emufp] no room after the image (end 0x%x, mapped 0x%x): NEON steps left as they are\n",
                (unsigned)end, (unsigned)m->load_size);
    return;
  }
  Pool p = {(uint32_t *)(img + pool_lo), (uint32_t *)(img + m->load_size), {0}, 0};
  int literals = 0;
  /* The ARM functions: from the file's .symtab (the vector library's are
   * local, not in .dynsym, which is all the loader keeps). Only inside them:
   * Thumb code holds word pairs that look like these instructions. */
  int nsyms = 0;
  Elf32_Sym *syms = read_symtab(m->name, &nsyms);
  if (!syms) {
    debugPrintf("[emufp] %s: no .symtab: NEON steps left as they are\n", m->name);
    return;
  }
  for (int i = 0; i < nsyms; i++) {
    const Elf32_Sym *sym = &syms[i];
    if (sym->st_shndx == SHN_UNDEF || ELF32_ST_TYPE(sym->st_info) != STT_FUNC || (sym->st_value & 1) ||
        !sym->st_size)
      continue;
    const uint32_t lo = sym->st_value, hi = sym->st_value + sym->st_size;
    if (lo < xlo || hi > xhi)
      continue;
    for (uint32_t off = lo; off + 4 <= hi; off += 4) {
      uint32_t *site = (uint32_t *)(img + off);
      const int kind = step_kind(*site);
      if (!kind)
        continue;
      if (is_literal(img, xlo, xhi, off)) {
        literals++;
        continue;
      }
      fix_step(site, *site, kind, &p);
    }
  }
  free(syms);
  debugPrintf("[emufp] emulator: %d VRECPS and %d VRSQRTS given ARM's 0 x inf rule (stubs %u KB of %u)%s; "
              "%d look-alike literal(s) left\n",
              p.fixed[1], p.fixed[2], (unsigned)((uint8_t *)p.at - (img + pool_lo)) >> 10,
              (unsigned)(m->load_size - pool_lo) >> 10, p.skipped ? ", SOME NOT FIXED (no room)" : "", literals);
}
