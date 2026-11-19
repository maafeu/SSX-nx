#!/bin/sh
# Build ssx-android.nro (the launcher, 64-bit: devkitA64 + libnx) natively. Build
# the game program first (../build.sh): the NRO carries ../ssx_nx.nsp and
# ../ssx_nx.build. Windows: runs in devkitPro's MSYS2.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
[ -f "$HERE/../ssx_nx.nsp" ] || { echo "launcher/build.sh: build ../ssx_nx.nsp first (../build.sh)" >&2; exit 1; }
if [ ! -d /opt/devkitpro ] && [ -x /c/devkitPro/msys2/usr/bin/bash.exe ]; then
  # from Git Bash or another shell on Windows: again, in devkitPro's MSYS2
  exec /c/devkitPro/msys2/usr/bin/bash.exe -lc 'cd "$0" && exec ./build.sh "$@"' "$HERE" "$@"
fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitA64/bin:$DEVKITPRO/tools/bin:$PATH"
cd "$HERE" && exec make -j8 "$@"
