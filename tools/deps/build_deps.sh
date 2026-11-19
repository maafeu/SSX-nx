#!/bin/bash
# build_deps.sh -- the AArch32 libraries SSX-nx links, fetched at the commits it is built and tested with,
# patched with this repository's changes, and built with devkitPro's devkitARM:
#
#   libnx32  -> ../libnx32/prefix        aks796/libnx32 (libnx 4.12 for AArch32 programs)
#   miniz    -> ../libnx32/prefix        miniz 1.15 (tools/deps/miniz/miniz.c, public domain)
#   drm      -> ../mesa32/prefix         devkitPro's libdrm_nouveau 1.0.1 + tools/libdrm/*.patch
#   mesa     -> ../mesa32/prefix         aks796/mesa32 (Mesa 20.1.0-rc3) + tools/mesa32/*.patch
#   ffmpeg   -> ../deps/ffmpeg32/prefix  aks796/ffmpeg32 + tools/ffmpeg32/*.patch: FFmpeg 7.1.1 (LGPL), downloaded
#                                        from ffmpeg.org and checked against its SHA-256
#
# "../" is the folder that holds this repository (or SSX_DEPS_DIR): build.sh and the Makefile look there.
#
#   tools/deps/build_deps.sh fetch     the sources at their commits, patched (once, into empty folders)
#   tools/deps/build_deps.sh all       build everything, in order
#   tools/deps/build_deps.sh <step>    libnx (with miniz) | miniz | drm | mesa-setup | mesa | install | ffmpeg
#
# On Windows: run it from Git Bash, with devkitPro in C:\devkitPro (devkitARM, switch-tools, and in devkitPro's
# MSYS2: make, bison, flex). On Linux or macOS: devkitPro in $DEVKITPRO (/opt/devkitpro). Both also need git,
# curl, ninja, and Python 3 with meson, mako and pkgconf (pip install meson mako pkgconf), on PATH or in a venv
# at ../tools/venv (or SSX_VENV). FFmpeg's configure needs a C compiler for the host: HOST_CC (on Windows,
# LLVM's clang in C:\Program Files\LLVM is found by itself). See tools/deps/README.md. MIT.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
TOP="${SSX_DEPS_DIR:-$(cd "$REPO/.." && pwd)}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 8)}"

# The pinned sources (the commits SSX-nx's releases are built from)
LIBNX32_URL=https://github.com/aks796/libnx32.git
LIBNX32_COMMIT=35c0b5aeb84a5456ea1419d98fd833631bda4279
MESA32_URL=https://github.com/aks796/mesa32.git
MESA32_COMMIT=b02c9c5f9cf0b08c6e49d4c0ee52a2d8bfd7fd42
DRM_URL=https://github.com/devkitPro/libdrm_nouveau.git
DRM_COMMIT=137265185c95cf58ce94c7afecba14f895dac2a7   # v1.0.1
FFMPEG32_URL=https://github.com/aks796/ffmpeg32.git
FFMPEG32_COMMIT=baef37dfd4526ff37f70dc68017ee847c553c725

# On Windows the compiler is a native program: it is given C:/... paths (Git Bash converts them), and the meson
# cross file names its programs that way.
if [ -d /c/devkitPro/devkitARM ]; then
  WIN=1; DKP=/c/devkitPro; EXE=.exe; MSYS_BASH=/c/devkitPro/msys2/usr/bin/bash.exe
else
  WIN=; DKP="${DEVKITPRO:-/opt/devkitpro}"; EXE=; MSYS_BASH=
fi
winpath() { if [ -n "$WIN" ] && command -v cygpath >/dev/null 2>&1; then cygpath -m "$1"; else printf '%s\n' "$1"; fi; }

TOPW=$(winpath "$TOP")
DKA=$DKP/devkitARM/bin
DKAW=$(winpath "$DKA")
NX=$TOP/libnx32/prefix
NXW=$(winpath "$NX")
MESA=$TOP/mesa32
PREFIX=$MESA/prefix
PREFIXW=$(winpath "$PREFIX")
MESA_BUILD="${MESA_BUILD:-$MESA/build}"
MESA_BUILDW=$(winpath "$MESA_BUILD")
DRM_SRC=$TOP/deps/libdrm_nouveau-1.0.1
FFMPEG32=$TOP/deps/ffmpeg32
CC=$DKA/arm-none-eabi-gcc
# archives made with "rcsD": no timestamps, so the same code gives the same bytes (and the game the same name
# for its shader cache, a hash of the Mesa libraries; meson's archives are like that already)
AR=$DKA/arm-none-eabi-gcc-ar
ARCH="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mfloat-abi=softfp -mfpu=neon-fp-armv8 -mtp=soft -fPIE -ftls-model=local-exec"
# devkitARM's bare-metal int32_t is 'long'; the android32 runtime, libnx32 and mesa32 were written and built
# where it is 'int' (the vita2hos toolchain image: their format strings and C++ overloads say so). Same size
# and C ABI either way.
I32=(-U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ "-D__UINT32_TYPE__=unsigned int"
     -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int -U__UINT_LEAST32_TYPE__ "-D__UINT_LEAST32_TYPE__=unsigned int")
VENV="${SSX_VENV:-$TOP/tools/venv}"
for v in "$VENV/Scripts" "$VENV/bin"; do if [ -d "$v" ]; then PATH="$v:$PATH"; fi; done
export PATH="$DKA:$DKP/tools/bin:$PATH${WIN:+:$DKP/msys2/usr/bin}"
step="${1:-all}"
# the compiler's working folder goes into the debug info: always the same one (mapped to ".")
cd "$TOP"

# The file names that asserts keep (__FILE__) and the debug info read <library>/..., not this machine's
# folders: $1 is the source folder, $2 its name. GCC uses the last map that matches.
pmap() { echo "-ffile-prefix-map=$TOPW=. -ffile-prefix-map=$(winpath "$1")=$2"; }

die() { echo "build_deps.sh: $*" >&2; exit 1; }

# fetch_git <url> <commit> <folder> [patch...]: one commit, without history; then this repository's patches
# (with LF line endings, as the sources are checked out).
fetch_git() {
  local url=$1 commit=$2 dir=$3; shift 3
  if [ -d "$dir/.git" ]; then
    [ "$(git -C "$dir" rev-parse HEAD)" = "$commit" ] || die "$dir is not at $commit"
    git -C "$dir" diff --quiet || { echo "$dir: fetched and patched already"; return 0; }
  else
    if [ -e "$dir" ]; then die "$dir exists and is not a checkout of $url: move it away, or skip fetch"; fi
    mkdir -p "$dir"
    git -C "$dir" init -q
    git -C "$dir" config core.autocrlf false
    git -C "$dir" remote add origin "$url"
    git -C "$dir" fetch -q --depth 1 origin "$commit"
    git -C "$dir" checkout -q FETCH_HEAD
  fi
  local p
  for p in "$@"; do
    echo "  $(basename "$dir"): $(basename "$p")"
    tr -d '\r' < "$p" | git -C "$dir" apply --whitespace=nowarn -
  done
  echo "$dir: $url at $commit"
}

fetch() {
  fetch_git "$LIBNX32_URL" "$LIBNX32_COMMIT" "$TOP/libnx32"
  fetch_git "$MESA32_URL" "$MESA32_COMMIT" "$MESA" "$REPO"/tools/mesa32/shader-cache-switch.patch \
    "$REPO"/tools/mesa32/fence-wait-stats.patch
  fetch_git "$DRM_URL" "$DRM_COMMIT" "$DRM_SRC" "$REPO"/tools/libdrm/bo-mem-hooks.patch
  fetch_git "$FFMPEG32_URL" "$FFMPEG32_COMMIT" "$FFMPEG32" "$REPO"/tools/ffmpeg32/native-build.patch
}

# libnx32 (its Makefile.32, in devkitPro's MSYS2 on Windows), with the same types, installed as its prefix/
# (headers, libnx.a, libnxd.a, switch32.specs/.ld/_rules)
build_libnx() {
  local S=$TOP/libnx32/nx
  local arch="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIC -ftls-model=local-exec $(pmap "$S" libnx32)"
  arch+=' -U__INT32_TYPE__ -D__INT32_TYPE__=int -U__UINT32_TYPE__ -D__UINT32_TYPE__=unsigned\ int'
  arch+=' -U__INT_LEAST32_TYPE__ -D__INT_LEAST32_TYPE__=int -U__UINT_LEAST32_TYPE__ -D__UINT_LEAST32_TYPE__=unsigned\ int'
  local script='cd "$0" && make -f Makefile.32 clean >/dev/null && make -f Makefile.32 -j"$2" dist-bin ARCH="$1" &&
    rm -rf ../prefix && mkdir ../prefix && tar -xjf libnx-*.tar.bz2 -C ../prefix && rm -f libnx-*.tar.bz2'
  if [ -n "$WIN" ]; then
    "$MSYS_BASH" -lc "export PATH=/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:\$PATH && $script" "$S" "$arch" "$JOBS"
  else
    DEVKITPRO=$DKP DEVKITARM=$DKP/devkitARM bash -c "$script" "$S" "$arch" "$JOBS"
  fi
  # switch32.specs looks for its linker script in $DEVKITPRO/libnx32 (where the Docker image installs libnx32):
  # here it is beside it. Mesa's and FFmpeg's configure tests link with these specs.
  sed -i "s|%:getenv(DEVKITPRO /libnx32/switch32.ld)|$NXW/switch32.ld|" "$NX/switch32.specs"
  echo "libnx32 -> $NX"
}

build_miniz() {
  # miniz v1.15, the amalgamated miniz.c: the header half is installed as <miniz/miniz.h> (what bionic_zlib.c
  # and dcr_apkcache.c include).
  local SRC=$HERE/miniz B=$TOP/deps/miniz/build
  mkdir -p "$B" "$NX/include/miniz"
  # the header: everything up to the implementation marker (MINIZ_NO_TIME first: the library is built with it,
  # and a reader without it sees mz_zip_archive_file_stat with an extra time_t, every field after it misplaced)
  { printf '#ifndef MINIZ_NO_TIME\n#define MINIZ_NO_TIME\n#endif\n'
    awk '/^\/\/ ------------------- End of Header: Implementation follows/ {exit} {print}' "$SRC/miniz.c"
  } > "$NX/include/miniz/miniz.h"
  $CC -g -O2 -Wall -ffunction-sections -fdata-sections $ARCH $(pmap "$SRC" miniz) -D__SWITCH__ \
    -DMINIZ_NO_TIME -I"$NX/include" -c "$SRC/miniz.c" -o "$B/miniz.o"
  rm -f "$NX/lib/libminiz.a"
  $AR rcsD "$NX/lib/libminiz.a" "$B/miniz.o"
  echo "miniz -> $NX/lib/libminiz.a"
}

build_drm() {
  local S=$DRM_SRC B=$MESA_BUILD/drm f
  rm -rf "$B" && mkdir -p "$B" "$PREFIX/lib/pkgconfig" "$PREFIX/include"
  for f in "$S"/source/*.c; do
    echo "  drm: $(basename "$f")"
    $CC -g -O2 -Wall -Werror -ffunction-sections -fdata-sections $ARCH $(pmap "$S" libdrm_nouveau-1.0.1) -D__SWITCH__ \
      -I"$S/include" -I"$NX/include" -c "$f" -o "$B/$(basename "${f%.c}").o"
  done
  rm -f "$PREFIX/lib/libdrm_nouveau.a"
  $AR rcsD "$PREFIX/lib/libdrm_nouveau.a" "$B"/*.o
  cp -r "$S"/include/* "$PREFIX/include/"
  sed -e "s,^prefix=.*,prefix=$PREFIXW," "$S/libdrm_nouveau.pc" > "$PREFIX/lib/pkgconfig/libdrm_nouveau.pc"
  echo "libdrm_nouveau -> $PREFIX/lib/libdrm_nouveau.a"
}

flags() { local out="" a; for a in "$@"; do out+="'$a',"; done; echo "${out%,}"; }

mesa_setup() {
  mkdir -p "$MESA_BUILD"
  local X=$MESA_BUILD/switch32_cross.txt
  local PKGCONF; PKGCONF=$(command -v pkgconf) || die "pkgconf not found (pip install pkgconf)"
  PKGCONF=$(winpath "$PKGCONF"); case "$PKGCONF" in *.exe) ;; *) PKGCONF+=$EXE ;; esac
  # devkitARM 16 (GCC 16) makes incompatible pointer types an error; newlib's uint32_t is 'unsigned long' where
  # Mesa says 'unsigned' (both 32-bit here). The vita2hos image's older GCC only warned.
  # Bare-metal arm-none-eabi makes int32_t 'long'; Mesa is written for the ILP32 Linux convention (int32_t
  # 'int': its C++ overloads on uint32_t and its format strings depend on it). Same size and C ABI either way.
  # (Sources are named relative to the build folder, ../../src/...; the maps cover a build folder elsewhere.)
  local CARGS=($ARCH -O2 -ffunction-sections -fdata-sections -D__SWITCH__ -DHAVE_TIMESPEC_GET "${I32[@]}"
    -Wno-error=incompatible-pointer-types -Wno-incompatible-pointer-types $(pmap "$MESA" mesa32)
    -I$PREFIXW/include -isystem $NXW/include)
  local LARGS=($ARCH -Wl,-z,notext -specs=$NXW/switch32.specs -L$PREFIXW/lib -L$NXW/lib -lnx)
  cat > "$X" <<EOC
[binaries]
c = '$DKAW/arm-none-eabi-gcc$EXE'
cpp = '$DKAW/arm-none-eabi-g++$EXE'
ar = '$DKAW/arm-none-eabi-gcc-ar$EXE'
strip = '$DKAW/arm-none-eabi-strip$EXE'
nm = '$DKAW/arm-none-eabi-nm$EXE'
pkg-config = '$PKGCONF'

[properties]
pkg_config_libdir = '$PREFIXW/lib/pkgconfig'

[built-in options]
c_args = [$(flags "${CARGS[@]}")]
cpp_args = [$(flags "${CARGS[@]}")]
c_link_args = [$(flags "${LARGS[@]}")]
cpp_link_args = [$(flags "${LARGS[@]}")]

[host_machine]
system = 'horizon'
cpu_family = 'arm'
cpu = 'cortex-a57'
endian = 'little'
EOC
  rm -rf "$MESA_BUILD/mesa"
  # MSYS_NO_PATHCONV: Git Bash would turn the target prefix /prefix into a Windows path; every other path here
  # is already a Windows one.
  MSYS_NO_PATHCONV=1 PKG_CONFIG_LIBDIR=$PREFIXW/lib/pkgconfig meson setup \
    --buildtype=plain --cross-file="$MESA_BUILDW/switch32_cross.txt" \
    --default-library=static --prefix=/prefix \
    --libdir=lib -Db_ndebug=true "$MESA_BUILDW/mesa" "$(winpath "$MESA")"
}

# SOURCE_DATE_EPOCH: nouveau_screen.c puts __DATE__ and __TIME__ in its name string; with the date of the mesa32
# commit there, the same sources give the same libraries (and the game the same shader cache name).
mesa_build() { SOURCE_DATE_EPOCH=1790770902 ninja -C "$MESA_BUILD/mesa" -j"$JOBS" "$@"; }
# meson takes the prefix as a path on the target (horizon: POSIX), so install into a stage folder and copy its
# prefix/ over ours
mesa_install() {
  rm -rf "$MESA_BUILD/stage"
  DESTDIR=$MESA_BUILDW/stage ninja -C "$MESA_BUILD/mesa" install
  cp -r "$MESA_BUILD/stage/prefix/." "$PREFIX/"
}

# ffmpeg32's own script (patched for this build: libnx32 from $LIBNX32, a host compiler, the file names), run
# in devkitPro's MSYS2 on Windows. It downloads FFmpeg 7.1.1 once and checks it.
build_ffmpeg() {
  local hostcc="${HOST_CC:-}"
  if [ -n "$WIN" ]; then
    if [ -z "$hostcc" ] && [ -x "/c/Program Files/LLVM/bin/clang.exe" ]; then
      hostcc=$(cygpath -u "$(cygpath -d "/c/Program Files/LLVM/bin/clang.exe")")   # no spaces in configure
    fi
    "$MSYS_BASH" -lc 'cd "$0" && export DEVKITPRO=/opt/devkitpro LIBNX32="$1" HOST_CC="${2:-cc}" &&
      ./build_ffmpeg32.sh reconfigure' "$FFMPEG32" "$NX" "$hostcc"
  else
    (cd "$FFMPEG32" && DEVKITPRO=$DKP LIBNX32=$NX HOST_CC="${hostcc:-cc}" ./build_ffmpeg32.sh reconfigure)
  fi
}

case "$step" in
  fetch) fetch ;;
  libnx) build_libnx; build_miniz ;;   # a new prefix/: miniz goes back in
  miniz) build_miniz ;;
  drm) build_drm ;;
  mesa-setup) mesa_setup ;;
  mesa) mesa_build "${@:2}" ;;
  install) mesa_install ;;
  ffmpeg) build_ffmpeg ;;
  all) build_libnx; build_miniz; build_drm; mesa_setup; mesa_build; mesa_install; build_ffmpeg ;;
  *) die "unknown step $step (fetch, all, libnx, miniz, drm, mesa-setup, mesa, install, ffmpeg)" ;;
esac
