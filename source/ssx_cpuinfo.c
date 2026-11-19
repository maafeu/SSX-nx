/* ssx_cpuinfo.c -- what the game reads in /proc/cpuinfo ([debug] cpuinfo).
 *
 * libgame.so reads /proc/cpuinfo. The runtime answers with the Switch's CPU
 * (ARMv8, NEON, VFPv4, integer divide). A 2012 phone said ARMv7; the PS Vita
 * port has no /proc/cpuinfo at all (the open fails). If the game picks code
 * paths from it (NEON or not, the number of job threads), the console and
 * the Vita run different code. This setting exists to find out, under the
 * emulator first (the riders' NaN bones, docs/SSX-notes.md "Open"):
 *   switch  the runtime's text (default)
 *   armv7   a 2012 phone's Cortex-A9 with NEON
 *   no_neon an ARMv7 CPU without NEON (VFPv3 only)
 *   empty   an empty file (no features), the nearest to the Vita's failed open
 * MIT.
 */
#include <stddef.h>

#include "dcr_config.h"

static const char k_armv7[] =
    "Processor\t: ARMv7 Processor rev 0 (v7l)\nprocessor\t: 0\nBogoMIPS\t: 1592.52\n\nprocessor\t: 1\n"
    "BogoMIPS\t: 1592.52\n\nFeatures\t: swp half thumb fastmult vfp edsp neon vfpv3 tls\n"
    "CPU implementer\t: 0x41\nCPU architecture: 7\nCPU variant\t: 0x2\nCPU part\t: 0xc09\nCPU revision\t: 9\n\n"
    "Hardware\t: SSX\nRevision\t: 0000\n";
static const char k_no_neon[] =
    "Processor\t: ARMv7 Processor rev 0 (v7l)\nprocessor\t: 0\nBogoMIPS\t: 1592.52\n\nprocessor\t: 1\n"
    "BogoMIPS\t: 1592.52\n\nFeatures\t: swp half thumb fastmult vfp edsp vfpv3 vfpv3d16 tls\n"
    "CPU implementer\t: 0x41\nCPU architecture: 7\nCPU variant\t: 0x2\nCPU part\t: 0xc09\nCPU revision\t: 9\n\n"
    "Hardware\t: SSX\nRevision\t: 0000\n";

/* bionic_io.c (weak there) */
const char *port_cpuinfo_text(void) {
  switch (dcr_config()->cpuinfo) {
  case 1: return k_armv7;
  case 2: return k_no_neon;
  case 3: return "";
  }
  return NULL;
}
