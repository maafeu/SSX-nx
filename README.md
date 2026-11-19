# SSX-nx

**SSX (EA Sports, 2012) on the Nintendo Switch, from its Android build.**

The game's own ARM code (`libgame.so` and `libgnustl_shared.so` from the Android release, `com.ea.ssx` 0.0.8430,
armeabi) runs unmodified in a 32-bit Switch process: nothing is recompiled. This repository is the Switch side: a
loader and Android stand-ins (the android32 runtime, in `runtime/`) and SSX's own fixes and features (`source/`).
It holds no part of the game: players bring their own APK.

- World Tour, Explore, all four riders, the movies and the music.
- 60 frames a second with the race simulated at 60 Hz. The 3D scene is drawn at 1920×1080 in handheld (scaled to the
  screen) and docked.
- The HOME button and sleep work at any time, in handheld and docked.
- Compiled shaders are kept on the SD card, so later starts and loads skip the compiler.
- Status Monitor Overlay's FPS counter works (with SaltyNX installed).

## Status

Tested on a Switch running Atmosphere, through 2026-11-18. The full log is
[docs/CONSOLE_RUN.md](docs/CONSOLE_RUN.md):
- Long sessions: memory in use stays flat across loads (37 loads, ~95 minutes, in one game process).
- No console crash in about 180 loads and many hours of riding since the memory fixes of 2026-11-18. The one game
  exit since then came from the experimental docking setting, which is off by default.
- 59.95–60.00 fps averages over whole sessions on the drops tested, handheld and docked.
- HOME and back (from menus, during a load, mid-race), sleep in handheld and in the dock, and undocking while asleep.

Not yet tested or known limits:
- The Deadly Descents and Mount Robson's drops, which this save hasn't unlocked yet.
- The game's online features (EA's servers) do nothing.
- Docking during play keeps the start-up resolution; following it live is an experimental setting, off by default.
- Four whole-console crashes during development, all on 2026-11-16/17 builds before the memory fixes. One had a
  known cause (an early profiler build, fixed). Three are unexplained, and none has happened since.

## Playing it

You need a Switch running Atmosphere with sphaira, and the APK of your own SSX for Android (0.0.8430).
- [docs/README.txt](docs/README.txt): installing it, the first start, controls, settings, updating, removing it.
  Every package carries it.
- The web installer, at **https://maafeu.github.io/SSX-nx/**: it makes the SD package from your APK, in your
  browser, with nothing uploaded ([installer/](installer/README.md)).
- The [Releases](https://github.com/maafeu/SSX-nx/releases): the package to copy by hand.

## How it works

1. `ssx-android.nro`, the launcher, is started from a sphaira forwarder (a HOME-menu icon). It installs the 32-bit game
   program for that icon as an Atmosphere ExeFS override, then restarts it (`runtime/launcher/`).
2. The game program finds the player's APK by what is in it. It unpacks the two libraries once, and reads the assets in
   place (`source/ssx_main.c`, `source/ssx_assets.c`).
3. It loads the libraries and resolves their imports against the runtime's bionic (Android's C library), JNI, OpenSL ES
   and EGL stand-ins (`source/imports.c`, made by `runtime/tools/gen_imports.py`).
4. It renders through Mesa's nouveau OpenGL ES driver, cross-built for AArch32. The movies are decoded with FFmpeg.
5. SSX's own fixes are in `source/`, each explained in [docs/SSX-notes.md](docs/SSX-notes.md):
   - patches to the game's code;
   - faster replacements for hot library functions (64-bit atomics, `pow`, `sinf`, `cosf`, `acosf`);
   - the shader cache, the input mapping, the settings.

## Building

Requirements:
- devkitPro, with devkitARM (the game program) and devkitA64 with libnx (the launcher).
- Python 3.
- The libraries the game program links, built beside this repository. [`tools/deps/`](tools/deps/README.md)
  fetches them at the commits the releases are built from, applies this repository's patches and builds them
  (what else that needs is in its README):

| Dependency | Where `build.sh` looks | What it is |
|---|---|---|
| libnx32 | `../libnx32/prefix` (or `SSX_LIBNX32`) | libnx for AArch32 programs, with miniz 1.15 |
| mesa32 | `../mesa32/prefix` (or `SSX_PORTLIBS`) | Mesa 20.1.0-rc3 for the Switch with libdrm_nouveau 1.0.1; this port's changes are in `tools/mesa32/` and `tools/libdrm/` |
| ffmpeg32 | `../deps/ffmpeg32/prefix` (or `SSX_FFMPEG`) | FFmpeg 7.1.1, LGPL-only. Optional: without it the movies are skipped |

Then:

```sh
tools/deps/build_deps.sh fetch   # the dependencies' sources, beside this repository (once)
tools/deps/build_deps.sh all     # the dependencies
./build.sh                       # the game program: ssx_nx.nsp, ssx_nx.elf
sh launcher/build.sh             # the launcher carrying it: launcher/ssx-android.nro
sh tools/make_package.sh         # the package: ../out/package-<build>/ (SD folder, README, settings, debug files)
```

On Windows these run in devkitPro's MSYS2; the scripts re-run themselves there from Git Bash. `tools/run_ryujinx.ps1`
runs `ssx_nx.nsp` in the Ryujinx emulator, with an emulator data folder kept outside the repository.

## Documents

| File | What it is |
|---|---|
| [docs/README.txt](docs/README.txt) | for players |
| [docs/TESTING.txt](docs/TESTING.txt) | for testers: what to check and what to send back |
| [docs/CONSOLE_RUN.md](docs/CONSOLE_RUN.md) | every console session: what was measured, found and fixed |
| [docs/SSX-notes.md](docs/SSX-notes.md) | the port's decisions and each game-specific fix |
| [docs/PROGRESS.md](docs/PROGRESS.md) | the progress log |
| [runtime/README.md](runtime/README.md), [runtime/docs/](runtime/docs/) | the android32 runtime shared by the 32-bit Android ports |
| [installer/README.md](installer/README.md) | the web installer |

## Thanks

Special thanks to lukebrl for [ssx-vita](https://github.com/lukebrl/ssx-vita), the PS Vita port of SSX: this port
was made using it. Its reverse engineering of the game found most of what a port of this build needs, and its data
tool's fixes to the game's files are made here as the game reads them (`source/ssx_assetpatch.c`). The android32
runtime, libnx32, mesa32 and ffmpeg32 are aks796's; the other credits are in
[docs/SSX-notes.md](docs/SSX-notes.md#credits) and [NOTICES.md](NOTICES.md).

## Legal

SSX is a trademark of Electronic Arts Inc. This is an unofficial fan project, not affiliated with or endorsed by
Electronic Arts or Nintendo.

No game code, music or other data of SSX is in this repository or in what it builds. The program runs the game from
the player's own copy of the Android release. The one piece of SSX's art here is its logo, on the HOME-menu icon
(`launcher/icon.jpg`, `installer/icon.jpg`): a trademark of Electronic Arts Inc., not covered by this repository's
license.

SSX-nx's own code is MIT ([LICENSE](LICENSE)). The runtime is MIT (`runtime/LICENSE`), the web installer GPL-3.0
(`installer/LICENSE`), and some development tools carry their origins' licenses. The program links libnx, Mesa,
libdrm, FFmpeg (LGPL) and newlib. Everything is listed in [NOTICES.md](NOTICES.md).
