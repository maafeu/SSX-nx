#!/bin/sh
# make_package.sh -- the package for a console test, from a built tree:
#   tools/make_package.sh [out-dir]       (default: ../out/package-<build>)
#   SD/switch/ssx-android/ssx-android.nro            the launcher (carries ssx_nx.nsp)
#   SD/switch/ssx-android/PUT_YOUR_APK_HERE.txt where the player's APK goes
#   settings/                              config.ini examples (the race simulation at 30 Hz, the frame-rate counter,
#                                          the loading boost off ...)
#   debug/                                 both ELFs, the map, a source snapshot (git archive),
#                                          so a crash.log or a profile maps to source lines
#   README.txt                             for players: install, the first start, controls, settings, removing it
#   TESTING.txt                            what to test, how to install, what to send back
# No game data goes in it. MIT.
set -eu
HERE="$(cd "$(dirname "$0")/.." && pwd)"
BUILD="$(cat "$HERE/ssx_nx.build")"
OUT="${1:-$HERE/../out/package-$BUILD}"
[ -f "$HERE/ssx_nx.nsp" ] || { echo "make_package.sh: build ssx_nx.nsp first (./build.sh)" >&2; exit 1; }
# the launcher again: it must carry this ssx_nx.nsp
sh "$HERE/launcher/build.sh" >/dev/null
rm -rf "$OUT"
mkdir -p "$OUT/SD/switch/ssx-android" "$OUT/settings" "$OUT/debug"
cp "$HERE/launcher/ssx-android.nro" "$OUT/SD/switch/ssx-android/"
cat > "$OUT/SD/switch/ssx-android/PUT_YOUR_APK_HERE.txt" <<'EOF'
Copy the APK of your own SSX into this folder: ssx-v0_0_8430.apk
(com.ea.ssx 0.0.8430, about 1.3 GB). Any file name will do.
EOF
cp "$HERE/ssx_nx.elf" "$HERE/launcher/ssx-android.elf" "$OUT/debug/" 2>/dev/null || cp "$HERE/ssx_nx.elf" "$OUT/debug/"
# the linker map names libraries by the absolute paths it was given: here they
# read ./... (this tree) and ../... (the libraries built beside it)
if [ -f "$HERE/build/ssx_nx.map" ]; then
  W="$(cd "$HERE" && { pwd -W 2>/dev/null || pwd; })"
  sed -e "s|$W/\.\./|../|g" -e "s|$W/|./|g" -e "s|$HERE/\.\./|../|g" -e "s|$HERE/|./|g" \
    "$HERE/build/ssx_nx.map" > "$OUT/debug/ssx_nx.map"
fi
(cd "$HERE" && git archive --format=tar.gz --prefix="ssx-nx-$BUILD/" -o "$OUT/debug/ssx-nx-source-$BUILD.tar.gz" HEAD)
(cd "$HERE" && git log -1 --format='%H %s' > "$OUT/debug/COMMIT.txt")
cat > "$OUT/settings/config_play.ini" <<'EOF'
# SSX for Switch -- settings. (The defaults: the game writes this file itself
# at its first start; this copy only shows them.)
[display]
race_fps = 60
race_sim_hz = auto
menu_fps = 60
resolution = auto
render_resolution = 1080
detail_distance = 100
fps_counter = false
saltynx = true
follow_docking = false
[controls]
swap_a_b = false
board_presses = true
[game]
movies = true
skip_dlc_screen = true
[performance]
boost_cpu_when_loading = true
trim_heaps = true
handheld_gpu = 460
threads_first = off
glthread = false
native_atomics = true
gl_finish = frame
shader_cache = true
menu_full_gc = false
[debug]
gl_selftest = false
boot_log_on_screen = false
log_java_calls = false
fp_runfast = false
cpuinfo = switch
skip_ik = auto
live_log = false
profile = false
math_selftest = false
resize_test = 0
window_crop_top = false
[config]
# Settings file format; leave as it is.
version = 2
EOF
S="$OUT/settings"
sed -e 's/^fps_counter = false/fps_counter = true/' "$S/config_play.ini" > "$S/config_fps_counter.ini"
# the race simulation always at 30 Hz (the phones' rate; frames interpolated to 60)
sed -e 's/^race_sim_hz = auto/race_sim_hz = 30/' "$S/config_play.ini" > "$S/config_race_sim30.ini"
sed -e 's/^race_fps = 60/race_fps = 30/' -e 's/^race_sim_hz = auto/race_sim_hz = 30/' -e 's/^menu_fps = 60/menu_fps = 30/' "$S/config_play.ini" > "$S/config_original30.ini"
# only if the console hides the rider too (TESTING.txt, step 2): HumanIK skipped there as well
sed -e 's/^skip_ik = auto/skip_ik = all/' "$S/config_play.ini" > "$S/config_rider_fix.ini"
# the CPU boost at loads off (TESTING.txt, LOADING BOOST)
sed -e 's/^boost_cpu_when_loading = true/boost_cpu_when_loading = false/' "$S/config_play.ini" > "$S/config_boost_off.ini"
cp "$HERE/docs/README.txt" "$OUT/README.txt"
cp "$HERE/docs/TESTING.txt" "$OUT/TESTING.txt"
echo "package: $OUT"
ls -R "$OUT" | head -30
