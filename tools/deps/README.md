# The dependencies

The game program links libraries built for 32-bit (AArch32) Switch programs. They are built beside this
repository, where `build.sh` and the Makefile look for them, by [`build_deps.sh`](build_deps.sh):

```text
<a folder>/
  SSX-nx/                      this repository
  libnx32/                     libnx32; prefix/ after the build (with miniz)
  mesa32/                      mesa32; prefix/ after the build (with libdrm_nouveau)
  deps/libdrm_nouveau-1.0.1/
  deps/ffmpeg32/               prefix/ after the build
  tools/venv/                  optional: a Python venv with meson, mako and pkgconf
```

| Library | Source | Commit | This repository's changes | License |
|---|---|---|---|---|
| libnx32 (libnx 4.12 for AArch32 programs) | [aks796/libnx32](https://github.com/aks796/libnx32) | `35c0b5aeb84a` | none | ISC |
| mesa32 (Mesa 20.1.0-rc3 for the Switch, AArch32) | [aks796/mesa32](https://github.com/aks796/mesa32) | `b02c9c5f9cf0` | [`tools/mesa32/`](../mesa32/README.md): the shader disk cache, fence-wait statistics | MIT and others |
| libdrm_nouveau 1.0.1 | [devkitPro/libdrm_nouveau](https://github.com/devkitPro/libdrm_nouveau) | `137265185c95` (v1.0.1) | [`tools/libdrm/`](../libdrm/README.md): hooks for the buffer memory | MIT |
| ffmpeg32: FFmpeg 7.1.1, LGPL-only (MOV, MPEG-4 Part 2, AAC) | [aks796/ffmpeg32](https://github.com/aks796/ffmpeg32), which downloads FFmpeg from ffmpeg.org and checks its SHA-256 | `baef37dfd452` | [`tools/ffmpeg32/`](../ffmpeg32/README.md): the native build | MIT (the scripts), LGPL-2.1-or-later (FFmpeg) |
| miniz 1.15 | [`miniz/miniz.c`](miniz/miniz.c), in this repository | | none | public domain |

The full commits are at the top of `build_deps.sh`. They are what the releases are built from: `fetch` checks
each one out and applies the patches.

## Building them

Written for and tested on Windows, from Git Bash. The Linux and macOS branches of the script follow the same
steps but have not been tried.

1. Install devkitPro (in `C:\devkitPro` on Windows) with devkitARM, devkitA64, libnx and switch-tools; in
   devkitPro's MSYS2, also `make`, `bison` and `flex`.
2. Install git, curl, ninja and Python 3 with `pip install meson mako pkgconf` (on PATH, or in a venv at
   `../tools/venv`, or where `SSX_VENV` points). On Windows, FFmpeg's configure also needs a C compiler for the
   host: LLVM's clang in `C:\Program Files\LLVM` is found by itself, or set `HOST_CC`.
3. Then, from this repository:

```sh
tools/deps/build_deps.sh fetch   # the sources at their commits, patched (into folders that do not exist yet)
tools/deps/build_deps.sh all     # libnx32, miniz, libdrm_nouveau, Mesa (the longest), FFmpeg
./build.sh                       # the game program
sh launcher/build.sh             # the launcher
```

`build_deps.sh <step>` runs one step: `libnx` (it remakes `libnx32/prefix`, so miniz is built again with it),
`miniz`, `drm`, `mesa-setup`, `mesa`, `install` (Mesa into `mesa32/prefix`), `ffmpeg`.

The releases were built with devkitARM r68 (GCC 16.1.0), devkitA64 r30 with libnx 4.12.0, switch-tools 1.13.1,
meson 1.12.1, Mako 1.4.3, pkgconf 3.0.7, ninja 1.13.2, bison 3.8.2 and flex 2.6.4.

## How they are built

- **int32_t is `int`.** devkitARM's bare-metal `int32_t` is `long`. The runtime, libnx32 and mesa32 were written
  where it is `int` (their format strings and C++ overloads depend on it), so they are built with
  `__INT32_TYPE__` and its kin redefined: the same size and C ABI either way.
- **GCC 16** makes incompatible pointer types an error: Mesa and FFmpeg are built with that warning off.
- **File names.** `-ffile-prefix-map` makes the debug info and the file names asserts keep read `libnx32/...`,
  `ffmpeg-7.1.1/...` and so on, not the folders of the machine that built them. The game program's own build
  does the same (`runtime/prefixmap.mk`).
- **The same bytes every time.** The game names its shader cache after a hash of the Mesa libraries it links,
  libdrm's included (`SSX_MESA_ID` in the Makefile): new bytes start a new cache on the console, whose first
  start compiles the shaders again. So the builds are made repeatable: archives without timestamps (`ar D`; meson
  makes them so already), and Mesa's `__DATE__`/`__TIME__` set to the mesa32 commit's date (`SOURCE_DATE_EPOCH`).
  Built from the same sources, the libraries come out byte for byte the same.
