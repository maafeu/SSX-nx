# SSX-nx: progress log

Newest last. Emulator runs are `out/emu_runs/<name>` (not in git).

## 2026-11-15

- Inputs surveyed: the base is android32 (the 32-bit Android-port runtime),
  not a 360 game base; the game files folder `SSX` was empty: the player's
  APK `ssx-v0_0_8430.apk` was found in the user's Downloads and used (read in
  place; extracted to `game/` outside the repo for analysis only).
- Toolchain without Docker: libnx32 built with devkitARM 16.1; miniz v1.15;
  libdrm_nouveau 1.0.1 (pinned commit) and Mesa 20.1 (mesa32) cross-built
  with meson/ninja from a venv (bison/flex from devkitPro's MSYS2 pacman).
- Port skeleton: assets, loader, JNI tables, EGL10, AudioTrack, input,
  lifecycle, hooks. `ssx_nx.nsp` builds (elf2nso32.py).
- Ryujinx runs: emu1 crash (accelerometer), emu2 crash (touch screen),
  emu3 runs 150 s at 30 fps; emu4 PPTC crash (PPTC turned off); emu6 window
  shots show the front end rendering correctly; emu10 reaches SSX HOME and
  the World Tour tutorial by the input script.
- 60 fps: emu11 measures the front end at 59.5-60.0 fps, the simulation at
  100% speed.

## 2026-11-16

- Races in Ryujinx: the game's code now runs below 2 GiB under the emulator
  (runtime so_util.c, RT_EMU_LOW_CODE): Ryujinx sign-extended a Thumb CBZ
  target in HumanIK (emu13). Races run 200-300 s without a fault since.
- The instant end of skipped movies crashed the render thread (emu8, emu12):
  held >= 1 s and 30 frames.
- GPU skinning (the PS Vita port's): on, ~500 skinned meshes a second.
- The rider is invisible in races under Ryujinx: RenderDoc (rdc1) shows its
  meshes drawn with every post-VS vertex NaN; the bone palettes are NaN
  before upload (29007 of 31644); no asset differs from the APK (every
  inflated asset CRC-checked), FPSCR is IEEE default. Suspected: the
  emulator's FP/NEON emulation (HumanIK, Thumb-2 + NEON). Console to tell.
- Terrain culling (the Vita port's) in; not exercised by the trick tutorial
  (a pre-rendered backdrop, no terrain).
- [display] race_sim_hz 30/60 (60 experimental) for the user's "locked 60"
  question.
- Launcher NRO, console package (tools/make_package.sh), docs/TESTING.txt,
  docs/CONSOLE_RUN.md.
- The console's sdmc:/switch/ssx-nx is the Xbox 360 SSX port's (the other
  session): this port's folder is now sdmc:/switch/ssx-android. Nothing was
  written to the console; the run waits for the owner's word on the folder,
  the APK copy and the forwarder.
- Movies: ffmpeg32 built natively (FFmpeg 7.1.1, LGPL), ssx_movie.c plays the
  APK's MP4s over the game (emu23: the EA logo and the intro).
- Handoffs for the android32 base in ../out/: the emulator low-code fix, and
  the native (no Docker) build notes.

## 2026-11-16 (night)

- The owner's ssx.zip (the PS Vita data tool's output, v1.1.1) reviewed
  against the APK: same libgame.so; shrunk media; three text edits. Two taken,
  made in memory as the files are read (ssx_assetpatch.c): memory.cfg heaps
  (heap in use 817 -> 636 MB at boot, 909 -> 769 MB in a race; the menu heaps
  later put back to Android's sizes after two crashes at the first movie) and
  nav.lua's full Lua collection per menu move.
- [display] fps_counter (ssx_overlay.c): fps top left, race speed in races.
- The rider NaN, emulator: not /proc/cpuinfo (emu29), not the math library
  (ssx_mathguard.c, emu31: acos never out of range but 22% of its calls get
  NaN; powf/logf/fmodf/expf clean). Next: sqrt watch, [debug] skip_ik.
- Console: back online after a drop at 41 MB of the APK upload; the upload
  restarted and completed (1,319,941,127 bytes), the new launcher too.
  sphaira's Install Forwarder is disabled in its settings: left to the owner.
- ssx_spinwait.c (the Vita's busy-wait sleeps): the emulator's race went
  from 17-55 fps / 28-56% speed to 60 fps / 100% (emu34).
- ssx_emufp.c: Ryujinx's VRECPS/VRSQRTS 0 x inf bug corrected for the
  emulator (1,688 sites); the rider NaN stays (emu33). skip_ik = solvers:
  same (emu32).
