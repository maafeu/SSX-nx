# ffmpeg32 patch

[aks796/ffmpeg32](https://github.com/aks796/ffmpeg32) builds FFmpeg 7.1.1 for 32-bit Switch programs in a
Docker image. `native-build.patch` changes its `build_ffmpeg32.sh` for the native build `tools/deps/build_deps.sh`
runs (devkitPro's MSYS2 on Windows):

- `LIBNX32` points at the libnx32 to build against (default: the image's `$DEVKITPRO/libnx32`).
- `HOST_CC`: the compiler for FFmpeg's build-time tools.
- `-Wno-incompatible-pointer-types`: GCC 16 makes those an error.
- `-ffile-prefix-map`: the file names FFmpeg's asserts keep read `ffmpeg-7.1.1/...`, not the build machine's
  folders.

The configuration is unchanged: LGPL-only (no `--enable-gpl`, `--enable-nonfree` or `--enable-version3`), the
MOV demuxer, the MPEG-4 Part 2 and AAC decoders and their parsers.
