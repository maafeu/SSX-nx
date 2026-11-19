# Third-party notices

SSX-nx's own code (`source/`, `launcher/`, `docs/`, the build files and the tools not named below) is MIT: see
[LICENSE](LICENSE). This page lists everything else: what this repository carries from elsewhere, and what the
Switch program links.

No part of SSX is in this repository or in the program: the game's libraries and data are read from the player's
own APK at run time. SSX is a trademark of Electronic Arts Inc. The HOME-menu icon (`launcher/icon.jpg`,
`installer/icon.jpg`) shows the SSX logo, Electronic Arts' trademark: the icon is not covered by this repository's
license.

## In this repository

| Part | Where | License |
|---|---|---|
| The android32 runtime (aks796): the so-loader, the bionic and JNI shims, the GL layer, the launcher | `runtime/` | MIT ([runtime/LICENSE](runtime/LICENSE)) |
| so_util, from the Vita so-loader (Andy Nguyen, fgsfds) | `runtime/source/so_util.c`, `so_util.h` | MIT (noted in [runtime/LICENSE](runtime/LICENSE)) |
| libnx's `kernel/virtmem.c` (libnx authors), changed for 32-bit processes | `runtime/source/nx32_virtmem.c` | ISC (libnx); the changes MIT |
| fdlibm's `e_pow.c` (Sun Microsystems, 1993), as found in SDL's `src/libm` | `source/ssx_vfpmath.c` | Sun's notice, kept in the file: free use, copying, change and distribution, provided the notice is preserved |
| The SHA-1 transform: the classic public-domain one (Steve Reid) | `runtime/source/bionic_sha1.c` | public domain |
| The web installer, cut down from renut_nx_ultimate_installer (ChanseyIsTheBest and contributors; StevensND's download worker) | `installer/` | GPL-3.0 ([installer/LICENSE](installer/LICENSE), [installer/NOTICES.md](installer/NOTICES.md)) |
| The RenderDoc capture scripts of the renut ultimate base (from banjo-nx and katamari-nx), unchanged | `tools/rdc/*.py` (17 files) | GPL-3.0 (the base's license) |
| The base's `rdc_run.ps1`, adapted | `tools/rdc_run.ps1` | GPL-3.0 |
| Rewritten from the base's scripts of the same names: their headers say MIT, but they follow the base's GPL-3.0 originals; until that is settled, treat them as GPL-3.0 | `tools/run_ryujinx.ps1`, `tools/run_ryujinx_rdc.ps1` | GPL-3.0 (see above) |
| Changes to Mesa and libdrm, kept as patches against their trees | `tools/mesa32/*.patch`, `tools/libdrm/*.patch` | MIT, as Mesa's and libdrm's files |
| Changes to ffmpeg32's build script (aks796) for the native build | `tools/ffmpeg32/native-build.patch` | MIT, as the script |
| miniz 1.15 (Rich Geldreich), unchanged | `tools/deps/miniz/miniz.c` | public domain (the Unlicense statement at the end of the file) |

The tools under GPL-3.0 are development aids (RenderDoc and Ryujinx runs on a PC). None of them is built into the
Switch program.

`source/ssx_saltynx.c` lets Status Monitor Overlay show SSX's frame rate. It talks to SaltyNX 2.1.4's
`saltysd_proc` with SaltyNX's IPC protocol, and writes the shared-memory block Status Monitor Overlay 1.4.1 reads.
Both were read in those projects' sources. The block's field layout and names follow Status Monitor Overlay's
`NxFpsSharedBlock`, so that the overlay can read it. The code is this port's own.

## In the Switch program

`ssx-android.nro` is the launcher (64-bit) carrying the game program `ssx_nx.nsp` (32-bit). Together they contain:

| Component | Version | License |
|---|---|---|
| SSX-nx and the android32 runtime | this repository | MIT (above) |
| libnx32: libnx built for AArch32 programs, with the android32 base's fixes (game program) | a fork of libnx 4.12 | ISC |
| libnx, devkitPro's (launcher) | as devkitA64 ships it | ISC |
| miniz (the runtime's zlib and zip reading) | 1.15 | public domain |
| mesa32: Mesa for the Switch, cross-built for AArch32 (EGL, OpenGL ES 2/3, glapi, the nouveau driver) | Mesa 20.1.0-rc3 | MIT, with some files under other permissive licenses (see Mesa's `docs/license.html`) |
| libdrm_nouveau, devkitPro's port | 1.0.1 | MIT |
| FFmpeg: libavformat, libavcodec, libavutil (the movies: the MOV demuxer, the MPEG-4 Part 2 and AAC decoders and their parsers; configured without `--enable-gpl`, `--enable-nonfree` or `--enable-version3`) | 7.1.1 | LGPL-2.1-or-later |
| newlib: the C library and libm (and, through `vfp_libm.o`, the fdlibm kernels of devkitARM's FPU build of it) | as devkitARM and devkitA64 ship it | BSD-style and other permissive licenses (newlib's `COPYING.NEWLIB`) |
| libgcc and libstdc++ | as devkitARM and devkitA64 ship them | GPL-3.0 with the GCC Runtime Library Exception |
| devkitPro's start-up files and tools output | | as devkitPro distributes them |

FFmpeg is linked statically. As the LGPL asks, the program can be rebuilt against a changed FFmpeg: this repository
has the port's complete source and build files, and FFmpeg's source is at [ffmpeg.org](https://ffmpeg.org/) (release
7.1.1). [`tools/deps/build_deps.sh`](tools/deps/README.md) fetches and builds it, with the other libraries, at
the commits the releases are built from.
