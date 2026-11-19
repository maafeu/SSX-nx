#!/bin/sh
# Build ssx_nx.nsp with devkitPro's devkitARM, natively (Windows: devkitPro's
# MSYS2; Linux/macOS: a devkitPro install). Arguments go to make:
#   ./build.sh             ssx_nx.nsp and ssx_nx.build
#   ./build.sh clean
#   ./build.sh rt-files    what is built from where (runtime.mk)
# libnx32 (built, its prefix/) and mesa32 (built, its prefix/) are looked for
# next to this folder, or where SSX_LIBNX32 and SSX_PORTLIBS point.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
NX="${SSX_LIBNX32:-$HERE/../libnx32/prefix}"
PL="${SSX_PORTLIBS:-$HERE/../mesa32/prefix}"
[ -f "$NX/lib/libnx.a" ] || { echo "build.sh: no libnx32 at $NX (set SSX_LIBNX32)" >&2; exit 1; }
[ -f "$PL/lib/libEGL.a" ] || echo "build.sh: no mesa32 at $PL: the null renderer" >&2
JOBS="${JOBS:-16}"
# a Python 3 for tools/elf2nso32.py, as a path the build shell can run
PY="${PYTHON:-}"
if [ -z "$PY" ]; then
  for c in python3 python; do
    if command -v "$c" >/dev/null 2>&1 && "$c" -c "import sys" >/dev/null 2>&1; then
      PY="$("$c" -c "import sys; print(sys.executable.replace(chr(92), chr(47)))")"; break
    fi
  done
fi
if [ -n "${MSYSTEM:-}" ] && [ -d /opt/devkitpro ] && [ "$(uname -o 2>/dev/null)" = "Msys" ] && command -v make >/dev/null 2>&1 \
   && [ "${DEVKITPRO:-}" = "/opt/devkitpro" ]; then
  # inside devkitPro's own MSYS2 shell
  export PATH="/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH"
  cd "$HERE" && exec make -j"$JOBS" NX="$NX" PORTLIBS="$PL" PYTHON="$PY" "$@"
fi
if [ -x /c/devkitPro/msys2/usr/bin/bash.exe ]; then
  # from Git Bash or another shell on Windows: run this again in devkitPro's MSYS2
  # (C:/x -> /c/x: the form MSYS2's shell runs)
  PY="$(printf '%s' "$PY" | sed -E 's|^([A-Za-z]):/|/\L\1/|')"
  exec /c/devkitPro/msys2/usr/bin/bash.exe -lc 'cd "$0" && export PATH="/opt/devkitpro/devkitARM/bin:/opt/devkitpro/tools/bin:$PATH" && exec make -j"$1" NX="$2" PORTLIBS="$3" PYTHON="$4" "${@:5}"' \
    "$HERE" "$JOBS" "$NX" "$PL" "$PY" "$@"
fi
export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export PATH="$DEVKITPRO/devkitARM/bin:$DEVKITPRO/tools/bin:$PATH"
cd "$HERE" && exec make -j"$JOBS" NX="$NX" PORTLIBS="$PL" PYTHON="$PY" "$@"
