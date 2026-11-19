# A console run (only with the owner's word, arranged with the other sessions)

The console runs sys-botbase (button presses, screenshots, the running title id) and sys-ftpd-light (the SD
card). It is driven only through the owner's `switch_remote.py` (renut-ultimate base, `tools/`), always with
`--root /switch/ssx-android`: it refuses any other path on the card and has no delete command. In Git Bash set
`MSYS_NO_PATHCONV=1` for commands with card paths.

    python <base>/tools/switch_remote.py --host <console> --root /switch/ssx-android <command>

## Before the first run (once)

1. `put out/package-<build>/SD/switch/ssx-android/ssx-android.nro ssx-android.nro`
2. The player's APK in `/switch/ssx-android/` (1.3 GB): `put` it, or the owner copies it. Game data never leaves the
   owner's own machines.
3. A sphaira forwarder (HOME-menu icon) for `ssx-android.nro`: the launcher installs the game program only when it runs
   as a forwarder made for it (never from hbmenu). Made by the owner, or with presses, each screen read first.
   Done on 2026-11-16 with the owner's word (title id 0x05A9F60353E17000, the owner's icon from `launcher/icon.jpg`):
   - hbmenu -> sphaira (D-pad Right x43 from the first tile; read "sphaira" on the tile) -> A.
   - sphaira installs were off: Menu (Y) -> Advanced Options (Down x4, A) -> Install options (Down x9, A) ->
     "Enable emummc" (Down, A) -> its warning "Installing apps will lead to a ban!" -> Enable. Only emuMMC was
     turned on; "Enable sysmmc" stays off.
   - B x3 to the Homebrew list -> Down x14, Right: "SSX / Maafeu" (until 2026-11-19 "SSX-nx port (EA Sports game)"),
     path `/switch/ssx-android/ssx-android.nro` (NOT "SSX / ssx-nx", the Xbox 360 port's) -> X -> Install Forwarder
     (Down x5, A) -> Right, Down x6 -> Create Forwarder (defaults: 36-bit, 3 cores).
   - sphaira caches NRO icons: after a new icon, leave sphaira (Plus) and start it again before making the forwarder.

## A run

1. `shot` -- the HOME menu; read the selected tile's name on the screenshot. Only on the SSX forwarder's tile: `click A`.
2. `title` -- the running title id must be the forwarder's (0x05...). Any other title: stop pressing.
3. First start: the launcher installs the program and restarts (`shot` to follow); then the libraries are unpacked;
   then the game's own first start copies its music and movies (~1-2 min).
4. The test: `input_script.txt` (put into the folder) or `click`s, each step checked with `shot`:
   title -> A; agreements -> DDOWN x3, A; SSX HOME -> A (World Tour) -> the first drop. The question of this run:
   does the rider show on the board (under Ryujinx its bone matrices are NaN)?
   If presses stop reaching the game (seen after closing it and starting it again: the HOME menu took them, the
   game did not), `raw "detachController"` (it times out, harmlessly) makes the next press attach a fresh virtual
   controller. The game reads any controller (`padInitializeAny`); this is sys-botbase's.
   With `[debug] live_log = true`, `get live.log` reads the newest log lines while the game runs (debug.log is
   locked until it closes).
5. `get debug.log`, `get crash.log` (if any), `get config.ini`, and the `shot`s, into `out/console_runs/<name>/`.
6. Leave the console as it was: `hold HOME 200` (a `click HOME` is often not taken), close the title from the HOME
   menu (X, then A on "Close"); this console's hbmenu is Arkanoid's tile started with R held: read the tile name,
   then `chord R A 2500`, and `title` reads 010091D01597C000.

## What to read in debug.log

- `[fps] simulation 30 Hz: N steps a second (game at P% speed) ... render 60 Hz: F a second` every 10 s: in races
  P must be 100 and F 60 for a locked 60.
- `[hwskin] N palettes uploaded, M with NaN`: M = 0 on the console means the NaN was the emulator's.
- `[terrain] chunk LOD picks: K kept, C culled`: the culling works in races.
- `[frame] frame N took M ms; CPU since: <thread> <ms> ...`: the long frames and who took the CPU.

## Run c2 (2026-11-16): the first console run

- Forwarder made (above); the game unpacked its libraries and copied its music and movies, then ran.
- **Rider shown with full HumanIK**: 38,748 bone palettes, 0 with NaN (the invisible rider was Ryujinx's).
- **60 fps**: title, menus and globe 60.0; intro movies 59.3-59.7 (after the upload fix; 47-54 before); the
  first drop (air tricks, one rider) and the Rockies ground drop at 60.0 with the race simulated at 60 Hz, 60 steps a
  second at 10-11 ms each, render at 7-10 ms each, for over ten minutes in all.
- Loading screens stutter (12-60 fps): EA's asset-stream thread holds the frame up for ~300 ms at a time while a drop
  loads (one GL context handed between threads). Loading only.
- HOME and back, the pause menu, Quit to the World Tour map, closing and starting again: fine.
- Found and fixed in the run: EA's 2 KB-per-write File::Copy (first start ~25 min, the ghosts save 75 s at every
  start), the miniz mismatch (libraries unpacked again at every start), the movie frame uploads.

## Run c3 (2026-11-16, evening): World Tour under load

The console was in handheld mode throughout (applet mode 0). From hbmenu a tap of HOME did nothing; `hold HOME 200`
opens the HOME menu. Gameplay tips pause a drop until A is pressed.

- **Ground Tutorial** (Whitehorn, one rider, 3 min, 2,507 m): completed, 60 fps, 60 Hz simulation at 100% throughout
  (steps 10-12 ms, render 8-13 ms).
- **JT2: Race It** against three AI riders (Griff, Elise, Mac; 3:26): completed, **60 fps with the 60 Hz simulation at
  100%** all race; steps 13-15 ms (10-11 alone), render 12-17 ms. No fallback to 30 Hz. 598,397 bone palettes, 0 NaN.
- **Curnoe's Moustache: Trick It** (Griff's ghost, dense trees): the first drop seen **below 60: 55-59 fps** in its
  busy stretches, the simulation still at 100%. Render 16.6-19 ms a frame. CPU report: FGGameRender 78-90% of a core
  when frames drop, FGGameSim ~45%, three General Job Threads ~33% each: 250-275% of the three cores the game has.
- The system's clocks, read on the console: handheld configuration 0x00020003 (CPU 1020, GPU 307.2, memory 1331.2
  MHz), docked 0x00010001; a loading boost runs at 1785 MHz CPU, GPU 76.8.
- `handheld_gpu = 460` is accepted (0x92220007: GPU 460.8, memory 1600 MHz). The first session with it also had the
  first profiler build, whose svcGetThreadContext3 buffer was libnx32's 32-bit ThreadContext (~0x198 bytes) while the
  kernel writes 0x320: ~400 bytes past it on every sample. That session ended with the console off the network (its
  debug.log cut off mid-write); the owner brought it back. With the profiler fixed, six sessions at 460.8 MHz ran
  without trouble, so the overflow is the likely cause.
- The "GL error 0x502 after a rider mesh" lines were the frame counter's own error (client-memory vertex arrays with
  its own VAO bound), read by the next rider mesh: fixed (cc3ea0e); 0 GL errors in every session since.
- Seen once each, not explained: the standings showing "Rider" twice for a moment (Mac's slot) while the player passed
  Mac; one screenshot mid-flip with the rider's torso not drawn.

Curnoe's Moustache, the same inputs each time (`ride.sh`: A, steering, a trick button; 10 s windows with the race at
100% speed):

| Build / setting | Windows | Mean fps | Under 59 | Step | Render | CPU, all threads |
|---|---|---|---|---|---|---|
| system clocks (GPU 307.2) | 43 | 59.21 | 12 | 11.0 ms | 15.7 ms | 227% |
| `handheld_gpu = 460` | 47 | 59.74 | 1 | 10.7 ms | 14.3 ms | 223% |
| + hardware pow, native atomics | 48 | 59.76 | 2 | 7.0 ms | 14.1 ms | 196% |
| + `threads_first = render` | 45 | 59.74 | 3 | 6.9 ms | 14.0 ms | 193% |
| `detail_distance = 200` | 28 | 59.54 | 3 | 6.8 ms | 16.4 ms | 190% |
| `detail_distance = 50` | 27 | 59.64 | 1 | 7.4 ms | 13.7 ms | 195% |

- The profile (`[debug] profile`, `tools/prof_symbols.py`): the job threads' CPU was half soft-float pow (EA's audio
  compressor on newlib's soft-float libm), the simulation and job threads waited on EA's fake 64-bit atomics' mutexes
  (~292,000 calls a second in a race). Both fixed: steps 10.7 -> 7.0 ms, job threads ~25% -> ~14% of a core each.
- What is left under 60 is short runs of hitches when the course streams in its next part: the asset streamer takes
  the GL context (4.18 eglMakeCurrent a frame instead of 4.00) and compiles shaders on it (24 at a time, Mesa's GLSL
  compiler and nouveau's code generator in the profile of AssetStream::Tr). Mesa only offers program binaries
  (GL_OES_get_program_binary has 0 formats) when its disk shader cache exists, and on the Switch it is never created
  (`disk_cache_get_function_identifier` has nothing to hash, and the cache's index wants mmap).
- `detail_distance`: at 100 almost nothing is ever beyond the last LOD (0-442 of 17,000-37,000 picks per 10 s): the
  game's streaming already bounds what is loaded. 200 keeps the detailed models farther (+2.4 ms render a frame); 50
  saves 0.3 ms and leaves ~12% of objects undrawn. Default 100.
- SaltyNX (2026-11-17, after the owner installed it): the first builds reported "not running", because they asked
  after libnx had opened sm. An application process may hold only one session it opened from a named port (1 of 1),
  and sm keeps that one, so both SaltyNX ports answered LimitReached (0x10801). The handshake now runs first thing in
  `__appInit` (`port_app_init_early`, runtime/source/nx_init.c), before sm, and closes its session again. Status
  Monitor 1.4.1's FPS Graph then showed SSX's frame rate (60 in the menus, 59-60 in Race It against AI riders), its
  graph following the loads. Screenshots: `out/console_runs/c3/status-monitor-*.jpg`.
- End state: build 202611170607 on the card, config.ini = settings/config_fps_counter.ini; the console back in hbmenu.

## Shader cache (2026-11-17, early morning)

mesa32's new disk cache (`tools/mesa32/shader-cache-switch.patch`, `[performance] shader_cache`), build 202611170710:
- Ryujinx: the first start stored 668 programs (5 MB pack); the second read 839 from the cache and compiled 1, with the
  same pictures. Its start-up's two long frames: 4.2 s -> 2.9 s.
- Console, cold (empty cache): 694 programs stored, 5.2 MB. Curnoe's Moustache rode as before: 50 windows, mean 59.89
  fps, 1 under 59. 23 long frames, 12.4 s in all, from start-up to the end of the ride.
- Console, warm (the second start): the start-up compile frame took 1.9 s (3-6 s without the cache), then, a few
  frames later and before the first 10 s report, the whole console went down: sys-botbase and ftp gone, debug.log cut
  off mid-write with garbage at its end, the pack untouched. The console came back ~8 minutes later on its wake screen,
  no title running (restarted).
- This is the second time the console went down tonight; the first was the profiler session (no shader cache then).
  Both happened while the game was creating many GPU programs (a drop loading; the warm start-up). A user program
  cannot take the system down by itself; a GPU fault, through the system's GPU service, can. Not confirmed: the
  fatal or crash reports in `sdmc:/atmosphere/` would say which process failed (outside the folder this port may read).
- The cache is off by default (build 202611170747 on the card, config.ini = config_fps_counter.ini).
- Retest on the owner's word (build 202611170800/0814, `shader_cache = true`, counters logged every 10 frames for the
  first minute with the log written out): three more warm starts, none went down.
  - Warm start with the ride (Curnoe's Moustache, the same inputs): 608 programs from the cache by frame 10, 868 in
    all, 0 compiled. Start-up compile frame 1.9 s (cold: 6.4 s); 10 long frames, 4.7 s in all (cold: 23, 12.4 s);
    the ride 59.97 fps mean, lowest window 59.3, none under 59 (cold: 59.89, 1 under 59).
  - Two quick warm starts to the SSX HOME menu: all from the cache, 60 fps.
  - So: 1 crash in 4 warm starts, and 1 crash without any cache earlier (the profiler session). Not enough to blame
    the cache or clear it. The default stays off until the owner decides.
- Soak (on the owner's "use your best judgement"), build 202611170827: the cache on by default, with the safety net
  (shadercache/starting while a start is in its first minute; a start that finds it turns the cache off and keeps
  the pack as .suspect). Checked first under Ryujinx: a start killed at 30 s, then a start that turned the cache off;
  a 100 s start that cleared its marker. On the console five warm starts in a row, 80 s each, to the menus: all
  shaders from the cache, each cleared its marker after a minute, none went down. In all: 1 crash in 9 warm starts,
  1 crash without the cache. The default is now on.
- SaltyNX: a start that restarts to install a new build used to leave its 176-byte block unmarked, so the next start
  took a new one (offset 184 after offset 4). The block is now marked ours when reserved; checked: the next start
  reused 184.

## 1080 rendering and gl_finish (2026-11-17, morning)

`[display] render_resolution` (the scene's render target, "Scrape" in `mobile/scrape/main.scrape`) and the GPU-busy
reading (`[gpu] busy X% on average`), Curnoe's Moustache in handheld, the same inputs (ride.sh, 14 rounds):

| Build / setting | Windows | Mean fps | Lowest | Under 59 | Step | Render | GPU busy |
|---|---|---|---|---|---|---|---|
| 720 (warm cache, the game's glFinish) | 41 | 59.97 | 59.3 | 0 | 6.8 ms | 15.5 ms | - |
| 1080 scene, the game's glFinish (r1) | 50 | 58.47 | 50.5 | 18 | 7.3 ms | 15.0 ms | 40.5% |
| 1080 scene, the game's glFinish (r2) | 41 | 58.38 | 52.3 | 13 | 7.6 ms | 16.9 ms | ~45% |
| 1080 scene, `gl_finish = frame` (r4, cache off) | 49 | 59.76 | 54.5 | 2 | 7.4 ms | 13.8 ms | 41.8% |
| 720, `gl_finish = frame` (r5, cache off) | 45 | 59.81 | 54.4 | 1 | 7.0 ms | 13.0 ms | 24.0% |
| 1080 scene, frame, no mallinfo() in play (r8, build 202611171605, cache off, profiler on) | 42 | 59.99 | 59.9 | 0 | 7.3 ms | 15.5 ms | 40.5% |
| the same, the drop restarted in the same session (r9) | 47 | 59.98 | 59.7 | 0 | 7.3 ms | 15.7 ms | 41.9% |

- With the GPU less than half busy, 1080 still missed frames. The profile had the render thread 13% in a spin inside
  Mesa's fence wait. A diagnostic Mesa (`tools/mesa32/fence-wait-stats.patch`) counted the waits that block, per
  caller: ~600 every 10 s, all from `st_finish` -> `nouveau_screen_fence_finish`, i.e. glFinish. The game calls it
  at the start of every frame (`LoopRender::RenderFrame`, libgame+0x167a80; also `GameLoopProcessTransition`), so
  the CPU's part of a frame and the GPU's never overlapped.
- `[performance] gl_finish` (ssx_gl.c): frame (the default) waits only for the frame before the last; flush, no
  wait; keep, the game's own. Checked in Ryujinx first.
- Crash 3. The first start of the diagnostic Mesa build (202611170949; a new Mesa, so a cold cache: 591 programs
  compiled and stored) took the whole console down. Its debug.log and live.log both end at the 10 s report at frame
  1960: the drop had just started (simulation 83% speed), and the last new program had been stored by frame 1910.
  shadercache/starting was left behind, so the next start set the cache aside (.suspect) and turned it off.
  Checked and ruled out in the code: Mesa's code-area resize (the old area is freed only after its fence) and the
  pack writer (locked, CRC-checked, appended and flushed). Two of the three crashes so far had the cache on (one
  reading it, one writing it); the CPU was boosted around both.
- r4, build 202611171007 (`render_resolution = 1080`, `gl_finish = frame`; this start found shadercache/starting and
  turned the cache off, as designed, so every program was compiled again): ~600 glFinish calls every 10 s in the
  race, 0 of them had to wait for the frame before. The render thread's frame went from 15-17 ms to ~9 ms in steady
  stretches, and the race held 60.0. The two windows under 59 (54.5 and 58.9) had no long frame and no shader
  compile: the simulation step (5.4 -> 7.8 ms) and the render work rose together while the course streamed in its
  next part, with the GPU at 46% busy, the CPU-bound kind of dip the 720 runs also had. The drop start (where crash
  3 had happened) and 14 rounds went without trouble; closed cleanly from the HOME menu.
- r5, 720 with `gl_finish = frame`: the same one dip as r4 (54.4), at the same place (~30 s into the run, the
  step 5.1 -> 7.3 ms), so not the resolution and not the GPU (24% busy).
- The dip, found (build 202611171555: `[debug] profile` now writes a race window under 59 fps by itself,
  prof-dip<n>.txt, with the window before it, prof-dip<n>-before.txt; the cumulative prof.txt takes ~20 s to read
  over FTP while the game runs). In the dip window the render thread spent 15.6% blocked in mutexLock (8.7% the
  window before) and the three rwfilesys threads ~1.1 s in newlib's `__malloc_update_mallinfo` (none before):
  `mallinfo()`, which walks every free chunk of the heap with the malloc lock held, ~28 ms a call on this ~830 MB
  heap. The runtime's APK block cache (dcr_apkcache.c) called it on every miss to ask whether it may grow; past
  ~718 MB of heap the answer is always no, so once the course streamed in its next part (~5 MB, ~40 misses) the
  malloc lock was held over a second of those 10 s, and every thread that allocates (the render thread, each
  frame) waited. The 10 s log report also called it 2-3 times (~80 ms of the lock each time).
- The fix (build 202611171605): `rt_heap_taken()` (runtime util.c), newlib's own running count of the arena
  (`__malloc_current_mallinfo.arena`, kept by malloc itself), read without the lock, for the cache's limit and the
  log lines (now "heap N MB taken"). On the console it reads 749-751 MB at the menus, where mallinfo() said ~740 in
  use. r8: 14 rounds at 1080, every window 59.9-60.1, none under 59, no dip for the profiler to record.

## 1080 docked (2026-11-17, afternoon)

Build 202611171623, docked (TV), `resolution = auto`, `render_resolution = 1080`, `gl_finish = frame`, profiler on,
shader cache off (shadercache/off). debug.log: `[render] the 3D scene at 1920x1080, the window 1920x1080`;
`[clocks] running (docked): CPU 1020 MHz, GPU 768.0 MHz, memory 1600.0 MHz` (the loading boost: CPU 1785, GPU 76.8).
Curnoe's Moustache, the same inputs (ride.sh, 14 rounds; the run ended twice and Restart began it again):

| Build / setting | Windows | Mean fps | Lowest | Under 59 | Step | Render | GPU busy |
|---|---|---|---|---|---|---|---|
| docked, window and scene 1920x1080 (d1) | 45 | 59.98 | 59.5 | 0 | 7.5 ms | 15.2 ms | 42.8% (highest window 59.8%) |
| JT2: Race It against Griff, Elise and Mac, docked, 1920x1080 (j1, build 202611172244, the whole 3:04 race) | 22 | 60.00 | 59.9 | 0 | 8.9 ms (max 10.8) | 10.7 ms | 37.9% |

- No window under 59 at full speed, so the profiler recorded no dip. 594-605 glFinish calls every 10 s, one waited
  for the frame before (5.3 ms) in the whole run.
- The two race windows at ~53 fps are the Restart from "Where to Next?" (the game at 90% speed: the run reset,
  ~1 s without frames); the handheld runs r8 and r9 have the same windows at the same place.
- The GPU is about as busy docked (768 MHz, the 1080 window and HUD) as in handheld at 460.8 MHz with the 1080 scene
  and the 720 window (~41%); the memory clock is 1600 MHz in both.
- The heap's arena grew to 873 MB over three runs (handheld r8+r9: 867), out of 974.
- So `resolution` now defaults to auto (1080 docked, 720 in handheld), and config.ini's format is 2: a format-1 file
  still holding the old defaults moves `resolution` 720 -> auto and `render_resolution` auto -> 1080 (the runtime's
  CfgMigrate). Checked on the console with its own format-1 file (build 202611172244): "config.ini updated to format
  2 (your settings kept): resolution 720 -> auto;", then the window and the scene at 1920x1080.
- The window is chosen at start-up. Docking or undocking during play keeps it (vi scales it); not tried yet.
- Race It (j1): the race the CPU works hardest in (four riders simulated); the GPU less busy than on Curnoe's
  Moustache. After the race, A on the results went on to the next event (a trick lesson), so ride.sh's later rounds
  are not race windows and are left out.
- Left: one long frame at every drop start and every Restart, ~0.7-0.9 s (d1 frames 5127, 19721, 32732: 765, 731,
  735 ms; j1 frame 5347: 921 ms), with the render and simulation threads each busy ~670-860 ms of it. It falls in
  the fade before the run, the same in handheld.

## The drop-start / Restart frame (2026-11-17, evening)

Every drop start and every Restart had one frame of ~0.73 s (r5, r7, r9, d1, j1: 731-736 ms at a Restart, 760-921 ms
at a drop start), at 720 and 1080, handheld and docked alike, the render and simulation threads each busy ~670 ms
of it.

- `[debug] profile` now also keeps the samples taken while a frame has run past 100 ms (after the first 600 frames)
  in prof-long.txt (`dcr_boost_frame_running_ms`, runtime). L1 (build 202611172303, threshold 250 ms then): in the
  769 ms frame the render thread was in `StaticInstancesRenderObj::BakeRNAMeshToGPU` -> `qsort(indices, n, 6,
  BakeComparator)` ~80% of its busy time (ValueFromStream 20%, BakeComparator 16%, qsort 4.5%, HalfToFloat); the
  simulation thread 82% in `sched_yield`, waiting for it.
- BakeComparator (libgame+0xfdfea0) orders a baked mesh's triangles by height: it decodes both triangles' three
  vertex positions (ValueFromStream, from the stream, stride and position element the bake puts in three globals,
  libgame+0x186ef50), takes each one's highest y, (int16)(int), and returns 1 when the second is higher, -1
  otherwise (never 0). Six decodes per comparison.
- `b_qsort` (source/ssx_bakesort.c): for that comparator alone, each triangle's key once (the game's own
  ValueFromStream, the comparator's max and conversions), a stable radix sort in cached memory, highest first,
  written back once. B1 (build 202611172316): 735 sorts, 307,445 triangles, 27.8 ms in all (55.3 ms for two
  bakes). The drop start's frame 769 -> 252 ms; the Restart's 733 -> 228 ms. Riding: 35 windows, 60.00 fps, lowest
  59.8, none under 59.
- The picture: the end-of-run camera (the same view as d1-6, before the change) differs from it by 0.08 on average
  per pixel and nowhere by more than 40 of 255 (JPEG noise).
- What is left of the frame (B2, threshold 100 ms): the game's own bake, flat (BakeRNAMeshToGPU, ValueIntoStream,
  GetMatrix44, sqrtf, GPU buffer creation through nvIoctl), no one function worth replacing.
- The six ~340 ms frames on the loading screen before a drop are the asset streamer compiling shaders (Mesa's IR
  and nv50_ir passes on AssetStream::Tr): the shader cache is off on this console since crash 3.
- B2 soak (build 202611172326, docked, 1080, profiler on, ride.sh 30 rounds, ~18 min, four Restarts): 93 full-speed
  windows, 59.98 fps mean, lowest 59.5, none under 59. The Restarts' frames 231, 231, 224, 228 ms; each bake 735
  sorts in ~27.7 ms. The heap's arena 855-885 MB, not growing from one Restart to the next.
- HOME and back mid-run (H1): `hold HOME 200`, the HOME menu ("Playing"), A (Continue): the run resumed where it was,
  60 fps from the next 10 s window (the window with the HOME menu in it: 13 frames); debug.log: `[applet] focus state
  (focus 1, mode 1)` on the way back.
- End state: build 202611172326 on the card, config.ini = the console's own, format 2 (window auto, scene 1080,
  fps_counter on, profile and live_log off); the console on its HOME menu, docked.

## More events (2026-11-17, night)

What this save opens (World Tour: Deadly Descents 0/4; Explore: medals on 0 of 75 drops):
- Explore's globe: only the Rockies (Alaska and the rest locked).
- The Rockies: Whitehorn Mountain (Event Peak), Extinguisher Tower (Backcountry Peak, "heavily forested"), Mount
  Robson (Deadly Descent Peak, "nearly impenetrable gauntlet of trees", at dusk).
- Open: Whitehorn's four drops in Explore (JT2 Race It and Trick It, Curnoe's Moustache Trick It and Race It).
- Locked: every Extinguisher Tower drop (10,000 credits each) and every Mount Robson drop (5,000-10,000 credits, and
  armor required). Left locked: unlocking spends the player's credits and changes the save.

The console was undocked before the e1 start (debug.log: `[clocks] handheld`, the window 1280x720, the scene
1920x1080 supersampled, GPU 460.8 MHz), so these are handheld results at the defaults:

| Run | Mode | Windows | Mean fps | Lowest | Under 59 | Step | Render | GPU busy |
|---|---|---|---|---|---|---|---|---|
| Curnoe's Moustache: Race It, Explore (cr, build 202611172326; ghosts: Bronze, Silver, Gold, then Personal Best) | handheld | 33 | 59.96 | 59.7 | 0 | 8.5 ms | 16.5 ms | 44.1% |
| JT2: Race It, World Tour, Griff, Elise and Mac as AI riders (jh, build 202611180024, the whole 3:03 race) | handheld | 21 | 60.00 | 59.9 | 0 | 7.8 ms | 16.3 ms | 36.4% |
| Curnoe's Moustache: Race It, Explore, again (cr2, build 202611180035; with the Personal Best ghost) | handheld | 23 | 59.94 | 59.5 | 0 | 8.7 ms | 16.0 ms | 45.4% |

- Explore asks for the rider first (Zoe, Elise, Mac, Griff); Zoe as in World Tour.
- The menus and the Explore globe: 60 fps on the counter at every screenshot; the character screen showed 48 for
  one moment while it loaded the riders.
- The transition after the race (frames 40973-40998, the first run's end and the Restart with the new Personal Best
  ghost) had four long frames, 356, 168, 348 and 233 ms. The first had every thread nearly idle (10 ms of CPU in
  356 ms): a wait, not work. The long-frame report now counts file writes too (write, fwrite, fflush, fsync, close,
  fclose, rename: calls, KB, ms; runtime bionic_io.c `dcr_io_write_stats`), to see whether that is the save.
- cr2, with file writes counted (`[io] the game's file writes since the last report`, every 10 s when there were
  any): the save after the race, 14 calls, 965 KB, 137 ms, inside ordinary frames (no long frame); at start-up 9 and
  10 calls (0 KB), 350 KB when the drop began. The idle 356 ms frame of cr did not come back: it came when the first
  Personal Best ghost was made, so creating that file (open with O_CREAT on the card, not timed here) is the likely
  wait. Seen once.
- JT2 Race It in World Tour (jh): the results screen and back to the map, one 211 ms frame (a load), no writes in a
  long frame.
- End state: build 202611180035 on the card, config.ini the console's own (format 2, profile and live_log off); the
  console handheld, on its HOME menu.

## Extinguisher Tower: The Dagger, Race It (2026-11-17, night)

On the owner's word, one drop unlocked in Explore: The Dagger: Race It on Extinguisher Tower ("heavily forested"),
10,000 credits (216,800 -> 206,800; no confirmation screen; no armor needed, survival probability 83% with Zoe's
gear). The save now has it open; nothing else bought. The Wrecking Crew drops there need armor too.

| Run | Mode | Windows | Mean fps | Lowest | Under 59 | Step | Render | GPU busy |
|---|---|---|---|---|---|---|---|---|
| The Dagger: Race It, Explore (x1, build 202611180035; ghosts Bronze, Silver, Gold) | handheld (charging) | 32 | 59.99 | 59.7 | 0 | 7.9 ms | 13.2 ms | 37.3% (highest window 40.7%) |

- The terrain: 40,000-48,000 chunk LOD picks kept every 10 s (Whitehorn's Curnoe's Moustache: ~3,700), 17-56%
  culled outside the view; still 60 fps, the render thread 13.2 ms a frame.
- The bake (drop start 370 ms, Restarts 361 and 330 ms): more trees than Whitehorn (~230 ms), the same work.
- One frame of 225 ms on the loading screen with no thread busy (a wait during the load).
- No GL errors, no dip for the profiler to record. One screenshot (x1-3) shows two thin straight lines across a gully:
  most likely cables (the course has wires, x1-2 shows two), not seen in any other screenshot.
- End state: build 202611180035, config.ini the console's own (format 2, profile and live_log off); handheld, HOME menu.

## Handheld soak on The Dagger (2026-11-17, night)

xs (build 202611180035, handheld and charging, 1080 scene, profiler on; ride.sh 30 rounds, ~19 min, seven Restarts):
84 full-speed windows, 60.00 fps mean, lowest 59.8, none under 59; the GPU 37.0% busy on average (highest window
40.6%); the SoC 39-41 C the whole time (41 C in 94 of 114 reports). Every Restart's bake frame 327-331 ms (the drop
start 367 ms). The save after each run: 4 writes, 350 KB, 19-46 ms, inside ordinary frames.

The heap's arena (`rt_heap_taken`, newlib's sbrked total) grew at every Restart and never came back down: 857, 865,
873, 878, 883, 885, 890, 894, 900, 904 MB (of 974), ~5 MB a Restart. At that rate ~14 more Restarts would reach the
heap's end. The docked Whitehorn soak (B2) did give some back (881 -> 857). Whether this is memory in use (a leak)
or free space the arena cannot reuse (fragmentation) the arena alone cannot say: with `[debug] profile` on, the
first sort of each bake now logs mallinfo() (`[mem] a bake begins: in use .., free inside the arena ..`; the walk
takes ~28 ms with the malloc lock held, inside the bake's long frame).
- xm (build 202611180218, the same drop, 22 rounds): `[mem]` at each bake, in time order: in use 793 MB (the drop
  start), then 860, 867, 871, 877, 881 at the Restarts; free inside the arena 7, then 10 MB each time; ~139,000
  free blocks throughout. So the memory is in use, ~4-7 MB more at each Restart: a leak, not fragmentation. Riding
  meanwhile: 64 windows, 60.00 fps, lowest 59.8.
- Next (build 202611180238): `[debug] profile` also counts the game's live GL objects (glGen* minus glDelete* for
  buffers, textures, framebuffers and renderbuffers) and the bytes given to glBufferData and glTexImage2D, in a
  `[globj]` line beside each bake's `[mem]` line (source/ssx_globjs.c). The bake sort's four scratch arrays are now
  one block kept between sorts (no malloc/free per mesh).
- xg (build 202611180238, 18 rounds, four bakes): live GL objects flat after the first Restart (buffers 296,
  textures 155, framebuffers 2, renderbuffers 4 at every Restart; 302/136 at the drop start), while the heap in use
  still grew (862, 868, 872 MB; free inside the arena 10-14 MB). The game deletes what it creates; each Restart
  re-uploads ~46.8 MB of buffer data and 100-150 MB of textures. So either the driver keeps memory after a delete
  (Mesa's buffer caches, or a GPU buffer whose delete waits on a fence), or the game's own heap use grows outside
  GL. Next (build 202611180256): libnx's nvMapCreate/nvMapClose counted (`-Wl,--wrap`, the Makefile): the GPU
  memory the driver holds, in the same `[globj]` line.
- Riding meanwhile (xg): 54 windows, 60.00 fps, lowest 59.8.
- xv (build 202611180256): the driver's GPU memory at each bake: 198 MB / 283 buffers (the drop start), then 268 /
  557, 272 / 579, 278 / 597 at the Restarts: +4 and +6 MB, the same as the heap in use (864, 868, 874). The game's GL
  objects flat meanwhile: Mesa's nouveau driver keeps buffers after the game deletes what they held.
- xh (build 202611180312), live GPU buffers by size at each Restart: +14 of 16 KB, +3 of 8 KB, +1-2 in the 2-4 MB
  class, nothing else. In bytes nearly all of it is the 2-4 MB ones. For reference, Mesa's nouveau_mm sub-allocator
  (every buffer up to 2 MB) keeps slabs of 4 KB to 4 MB and never returns them: 16 KB slabs hold 1-2 KB chunks, 2 MB
  slabs hold 256-512 KB chunks.
- Build 202611180327 logged each 2 MB+ nvmap from inside the nvMapCreate wrapper and hung the game at "[egl] handing
  the screen from the boot log to the game" (a black screen; the console fine, closed from the HOME menu). The
  wrapper no longer logs: build 202611180332 keeps the live 2 MB+ ones in a table with the bake each was made after,
  printed at each bake.
- xt (build 202611180332), the table at each bake: what each Restart leaves behind is one 3,840 KB tiled buffer
  (1280x768x4: a 1280x720 surface of 4 bytes a pixel, its height padded to the tiling block: the handheld window's
  size) every time, and one 2,048 KB linear buffer once. The game makes no window-sized render target at a Restart
  (its own [render] lines: only the snow's 128x128 and 512x512 maps), so Mesa makes it on the game's behalf. Not
  found yet which call. On the docked Whitehorn soak (B2) the heap did come back down once (881 -> 857), so it may be
  a bounded cache rather than an endless leak.

### Crash 4 (2026-11-17, ~21:45 PC time)

During xt, round 14-15 (The Dagger Race It, handheld, build 202611180332, profiler on, shader cache OFF since crash
3): sys-botbase (TCP 6000) and the FTP server (TCP 5000) stopped answering within ~35 s of a normal 10 s report;
the console still answers ping. The last report: mid-race (01:43.8 on the clock), 60.0 fps, the simulation at 100%,
heap 878 MB taken, SoC 40-41 C, nothing unusual; the render thread at 84-85% of a core (60-67% in the other runs on
this drop). The same signs as crashes 1-3 (both services gone). This is the first with the shader cache off, so the
cache was not the whole story. The build had the nvmap counters (libnx's nvMapCreate/nvMapClose wrapped; counting
and, with the profiler, a table under a mutex); runs xv and xh with the counters did 16 rounds each without trouble.
The console needs a restart; the Atmosphere fatal report (sdmc:/atmosphere/) would name the failing component.
config.ini on the card is still the measuring one (profile and live_log on).
- After the restart (the owner: the screen was on the loading screen): debug.log ends at frame 39220, the start of a
  Restart's load, right after the game made the snow's render targets (VelDispMap, DispMap, BlurredDispMap; the
  AccumulatedDisplacementMap lines that always follow are missing), then a few bytes of garbage: the write cut short.
  The last 10 s report (frame 38642) was normal. That load's bake never logged its `[mem]` line. So crash 4 came in a
  Restart's loading screen, while GPU resources were being made.
- All four crashes were at loads: crash 1 a drop load (that profiler build's context overrun), crash 2 the start-up
  (first picture), crash 3 a drop start, crash 4 a Restart. During loads the runtime boosts the CPU for every frame
  over 50 ms and drops the boost at that frame's end (dcr_boost.c; FastLoad: CPU 1785 MHz, GPU 76.8 MHz), ~6
  long frames a load here, 52 in this session; retail games hold one boost for a whole load. Common to the crashes,
  but also to the hundreds of loads that did not crash: a candidate, not a cause.
- End state: build 202611180354 on the card, config.ini the console's everyday one (profile and live_log off,
  boost_cpu_when_loading true); the console on its HOME menu, handheld; not relaunched since the crash.

## Overnight: the loading boost off (2026-11-17/18, night)

The owner's call after crash 4: run on with `boost_cpu_when_loading = false` (config.ini otherwise the measuring one,
profile and live_log on) to see whether the per-frame loading boost is behind the console crashes. Handheld, The
Dagger Race It, Zoe; ride_night.sh (ride.sh's inputs, a screenshot and live.log a round, stops by itself).
- bo1 (build 202611180354, 22:07-23:19 PC time, 117 rounds): no console crash in 21 drop loads (the drop start and
  20 Restarts). 308 riding windows, 60.00 fps mean, lowest 59.8, none under 59; render thread 63%, simulation 63%
  of a core; SoC 35-41 C. With the boost off each Restart has two long frames of 513-689 ms (327-331 ms, one frame,
  with it on), and they grew through the session (513 -> 562 and 579 -> 689 ms) as the heap filled.
- Then the game ran out of memory: the heap in use at each bake 796 MB (drop start), 866, 870, ... 947 MB (+4.1 MB
  every Restart, all 20 of them; the arena 964 of the heap's 972 MB), one 3,840 KB tiled buffer left behind each
  time. At the 22nd load: `[assets] out of memory inflating mobile/android/audio/sfx/tricky.abk (4778486 ->
  5662704)`, the game took the missing file as "not in the APK", and a data abort followed (crash.log: libgame+0x503474
  reading near 0x21604878; the game's own code, a NULL-ish pointer from the failed load). The console was fine.
  So the Restart leak is a crash of its own: ~21 drop loads into a session (Restarts or new drops alike, if every
  load leaves its buffer), about 70 minutes of Restart-heavy play.
- A slip: ride_night.sh's A presses went on after the game closed and relaunched SSX from the HOME menu without
  the tile being read (the title check after confirmed SSX). ride_night.sh now checks the running title before every
  press and stops on a frame count that goes backwards or any "out of memory" line.
- The leak comes first now: build 202611180526 (the stack trace of each window-sized tiled buffer made after the
  first bake, 16 KB of stack scanned) with the boost still off, a short session (bt1).
- bt1 (build 202611180526, 20 rounds, four Restarts; 109 riding windows, 60.00 fps, lowest 59.9; heap in use at the
  bakes 794, 862, 868, 872, 878 MB): the trace of every leftover 3,840 KB tiled buffer is the same. The printer
  labelled this program's addresses G+ (it sits above libgame here; fixed since); shifted back (0x8966000: 26 of 28
  addresses then fall right after a call) and symbolized: nvMapCreate <- nouveau_bo_new <- nvc0_miptree_create <-
  nvc0_resource_create <- st_texture_create <- guess_and_alloc_texture <- st_AllocTextureImageBuffer <- st_TexImage
  <- _mesa_TexImage2D <- libgame. So it is a texture the game makes itself with glTexImage2D, 1280x720, at every
  load, while its glGenTextures/glDeleteTextures balance. The game has one GL context and passes it between threads
  with eglMakeCurrent (~5 times a frame); a glDelete* on a thread not holding it at that moment goes to Mesa's no-op
  dispatch and is lost. The likely story: this texture is deleted on such a thread.
- Build 202611180549 (source/ssx_gldefer.c, always on): a glDelete* (buffers, textures, framebuffers,
  renderbuffers, vertex arrays, programs, shaders) on a thread with no current context is queued (the names stay
  taken), and the thread that next makes the context current runs it (ssx_egl.c). A `[gl] deletes made with no
  context on the thread` line counts them, with the first name and thread. df1: that build, the boost still off.
- df1 (build 202611180549, 30 rounds, six Restarts; 123 riding windows, 60.00 fps, lowest 59.8; 0 GL errors): the
  story holds. The simulation thread (FGGameSim) deletes textures without the context: 145 deferred in six loads
  (~24 a load, not just the big one), every one of them lost before. The leftover 3,840 KB texture is gone: at each
  bake only the current run's is live. GPU memory 260 -> 266 MB over five Restarts; the heap in use at the bakes
  793, 857, 859, 859, 861, 861, 863 MB (+1 MB a Restart, was +4.1). But the arena still grew as before (869, 874,
  878, 880, 884, 890 MB; bo1 860 -> 894 MB over its first 30 rounds, df1 854 -> 886), the free space inside it
  11 -> 26 MB: fragmentation now. libdrm memaligns each buffer object page-aligned from the heap; the 3,840 KB
  texture remade at every load needs a chunk a little bigger than the hole its predecessor leaves, so it takes
  fresh memory from the arena's end each time and the hole goes to small allocations.
- Build 202611180617: libdrm_nouveau gets two weak hooks for the memory behind buffer objects
  (tools/libdrm/bo-mem-hooks.patch, rebuilt with deps/build_deps.sh drm); source/ssx_bomem.c keeps freed blocks
  of 1 MB and more (24 MB, 16 blocks at most, the least recently freed going back first) and hands one out again
  for an object of exactly its size; a failed memalign gives everything back and retries. A `[bomem]` line counts
  them. bm1: that build, the boost still off.
- bm1 (build 202611180617, 30 rounds, six Restarts; 96 riding windows, 60.00 fps, lowest 59.8): the arena at the
  bakes 825 (drop start), 889, 889, 891, 891, 893, 893 MB, the free space inside it 9 MB every time (df1: 869 ->
  890 MB, 11 -> 26 MB free). +0.8 MB a Restart, was ~4. The kept blocks are busy: 1,363 handed out again (1.9 GB)
  against 55 made, mostly during the races (Mesa frees and remakes 1 MB+ buffers all the time; each of those was a
  memalign and a free before), ~23 MB kept at any moment, 3 given back. At +0.8 MB a load the heap's end is ~100
  loads away instead of 21. The boost-off Restart frames: 508-527 and 585-637 ms. The same process rides on (bm1b,
  120 rounds) to check a session well past the old 21 loads.
- bm1b (the same process as bm1, 120 more rounds, 00:42-01:56 PC time; 150 rounds and 97 minutes of play in all):
  no console crash, no game exit. 36 loads in one process (the drop start and 35 Restarts; the old limit was 21).
  The heap in use at every bake from the 6th Restart on: 884 MB, flat; the arena 893 MB at the 6th Restart, 896 MB
  at the 35th (+0.1 MB a Restart), the free space inside it 9 -> 11 MB; GPU memory 264 -> 268 MB. The kept blocks:
  4,235 handed out again (4.9 GB) against the same 55 made, 3 given back, 0 flushes. 723 texture deletes deferred
  (~20 a load), all from FGGameSim. 328 riding windows, 60.00 fps mean, lowest 59.8, none under 59; render thread
  65%, simulation 60% of a core; GPU busy 36% on average; SoC 38-42 C (41 C most of the time). The two boost-off
  long frames of every Restart: 585-671 ms (no trend) and 513 -> 547 ms (bo1: 513 -> 562 and 579 -> 689 ms in 20
  Restarts, as the heap filled). Closed from the HOME menu; debug.log in console_runs/c3/bm1.
- With the boost off, about 70 loads over ~4 hours of riding tonight (bo1 22, bt1 5, df1 7, bm1 36) and no console
  crash; the four console crashes all came at loads with the boost on. Not proof (crash 4 came after ~13 minutes
  and 4 loads, crashes 1-3 had other suspects), but the boost stays off on the card for now. The cost: a Restart's
  two frames of ~0.5-0.6 s instead of one of ~0.33 s.
- Build 202611180624 (Mesa's no-op dispatch counted: `[gl] N other GL calls made with no context`) is on the card,
  not yet run: the relaunch after bm1b was refused by the session's permission check, so it waits for the owner.

### The boost series (2026-11-18, early morning, on the owner's word)

The owner's plan: two more long sessions with the boost off, then one with it on, to compare; unattended. Each a fresh
launch (tile read, title checked, The Dagger: Race It and 206,800 credits read on screenshots), 150 rounds of
ride_night.sh, then closed from the HOME menu. Build 202611180624, config-boostoff-measure.ini.
- nc1 (boost off, 02:18-03:50 PC time): no console crash, no game exit. 37 loads (the drop start and 36 Restarts)
  in 95 minutes; the heap in use 882 MB at every bake from the 6th, the arena 888 -> 894 MB, free inside 9 -> 11 MB;
  GPU memory 262 -> 266 MB; 4,193 kept blocks handed out again against 55 made; 742 texture deletes deferred
  (FGGameSim). No `[gl] ... other GL calls made with no context` line at all: nothing besides the deletes reached
  Mesa's no-op dispatch (the counter has not been seen firing, so this zero is only as good as the hook). 0 GL
  errors. 405 riding windows, 60.00 fps mean, lowest 59.9; GPU busy 36% on average; SoC 36-41 C. Restart frames
  583-699 ms (no trend) and 512 -> 554 ms.
- nc2 (boost off, 03:55-05:27 PC time): the same. No console crash, no game exit; 37 loads in 94 minutes; the heap in
  use 878 -> 884 MB from the 2nd bake on (882 by the 6th), the arena 888 -> 897 MB, free inside 9 -> 12 MB; GPU
  memory 260 -> 266 MB; 4,379 blocks handed out again, 55 made; 772 texture deletes deferred; no other GL call
  without a context; 0 GL errors. 376 riding windows, 60.00 fps mean, lowest 59.8; GPU busy 36%; SoC 39-41 C.
  Restart frames 579-837 ms (one 837, the rest under 615) and 509 -> 543 ms.
- So with the boost off: about 144 loads over ~7 hours of riding tonight (bo1 22, bt1 5, df1 7, bm1 36, nc1 37,
  nc2 37), and no console crash.
- nc3 (boost ON, config-booston-measure.ini, 05:31-07:04 PC time): no console crash, no game exit either. 33 loads
  in 95 minutes (the race outcomes vary, so fewer Restarts); the heap in use 882 MB from the 6th bake, the arena
  886 -> 895 MB, free inside 9 -> 12 MB; GPU memory 260 -> 266 MB; 4,055 blocks handed out again, 55 made; 622
  texture deletes deferred; no other GL call without a context; 0 GL errors. 374 riding windows, 60.00 fps mean,
  lowest 59.8; GPU busy 36%; SoC 39-41 C. The boost did its job: start-up 6.6 s at 1785 MHz; each Restart's two long
  frames 369-503 ms (one 503, the rest under 403) and 346-373 ms, against ~590 and ~530 ms with it off.
- What the series says: 177 loads tonight on the fixed builds, 144 with the boost off and 33 with it on, and no
  console crash at all. The boost-on session doesn't reproduce crash 4, so the boost isn't shown to be the cause:
  if it crashed the console once every 15 loads, 33 clean ones would happen ~1 time in 10; once every 30 loads,
  ~1 time in 3. Neither is ruled out. The card is left on the owner's everyday config.ini (format 2, profiler and
  live log off) with the boost off (config-everyday-v2-boostoff.ini): the setting with the longest clean record, at
  ~0.2 s more per Restart; the build's default stays on.
- The owner's call in the morning: the boost back on. The card now has the everyday config.ini as it was
  (config-everyday-v2.ini: format 2, profiler and live log off, `boost_cpu_when_loading = true`), read back after the
  upload; build 202611180624.

## Later on the 18th: the docking switch, the FPU float math, and "crash 5" (a false alarm)

- The Restart freeze is not a regression: build 202611180332 (before the leak fixes, boost on, crash 4's) had the
  same two frames as nc3 (~345 ms, the asset streamer compiling shaders, then ~370 ms, the bake). The earlier
  "one ~330 ms frame" was a different drop and setting.
- The no-op counter's zero is real: Mesa's own nop handler is compiled out off Windows (main/context.c,
  USE_GLAPI_NOP_FEATURES 0), and with shared glapi a thread with no context dispatches through table_noop_array
  -> noop_generic -> noop_warn -> the handler ssx_gldefer.c installs. So no GL call but the (deferred) deletes was
  ever made without the context.
- Where the shader compiles go (prof-long.txt of nc3, AssetStream::Tr in frames past 100 ms): ~45% of its busy
  time in newlib's malloc/free and their recursive lock (13% of all its samples in lock/unlock alone), the rest
  GLSL IR and nv50_ir passes. Not pursued: an allocator front end is a big, risky change for ~0.1 s a Restart.
- Docking during play (build 202611181553, then 1610): with [display] follow_docking (resolution auto) the
  window's buffers are made at 1080 in either mode, the crop shows the bottom-left part GL draws (rt_window.c),
  and a change of mode sets the size the game is told and sends MainThread.NativeOnSurfaceChanged again
  (ssx_window.c; the handler posts a Blast MessageCoord2i, type 5). [debug] resize_test = N switches every N s.
  Ryujinx (rz1, docked, 9 switches): menus, HUD and the 3D scene laid out again full-screen at both sizes. The
  console (rz1, handheld, 20 s): the same at every screenshot (SSX HOME at 720, the drop selection at 1080,
  six race shots); but at the 12th switch, mid-race, the game aborted ("pure virtual method called", "terminate
  called without an active exception", abort from its C++ runtime) -- a game exit, the console fine. So
  follow_docking is off by default (experimental, the crash written in its help text): auto keeps the start-up
  size, and to dock during play at full resolution, resolution = 1080.
- Soft float left in the game's math: the race profile (nc3) has ~20% of the General Job Threads' busy time in
  newlib's soft-float __kernel_cosf, __kernel_sinf and __ieee754_acosf (EA::Rigamate / EA::Ant animation).
  Build 202611181610 gives the game sinf, cosf, acosf and asinf on the FPU with newlib's own kernels: the
  Makefile extracts kf_sin, kf_cos, ef_rem_pio2, kf_rem_pio2, ef_acos, ef_asin and their sqrtf/fabsf/floorf/
  scalbnf from devkitARM's FPU multilib (armv6k/fpu, VFPv2: no fused multiply-add), renames them vfp_* and
  drops their ABI tag; ssx_vfpmathf.c calls them with pcs("aapcs-vfp") from newlib's own sinf/cosf wrapper
  logic (as libm_a-sf_sin.o disassembles). [debug] math_selftest, Ryujinx: 0 of 1,048,576 arguments differ
  from newlib in each of the four (bit for bit; one NaN input with another payload), 5x (sinf, cosf) to 18x
  (acosf, asinf) faster there. On the console: mt2 below.
- "Crash 5" (~10:15 PC time) was a false alarm. The first console start of build 202611181610
  (config-mathtest.ini: boost on, profiler and live log on, math_selftest on; handheld; shader cache off): the
  title check right after the launch read SSX; within ~50 s the console stopped answering at its usual address
  (ping "host unreachable") and was taken for a crash and not relaunched. It had only moved to a new address on
  the home network (the owner read it off System Settings > Internet after restarting the console). That start's
  debug.log, fetched afterwards: the self-test (below), then 555,256 frames at 60 fps, 2 h 34 min on the start
  screens with the boost on, until the owner's restart. So since the leak fixes there has been no console crash,
  boost on or off. Lesson: ping can't tell a crashed console from one with a new address; before calling a
  console crash, ask for the address System Settings > Internet shows.

### mt2: the FPU float math on the console (2026-11-18 afternoon)

The owner's word after "crash 5": the boost off, then the math check. The switch_remote wrapper now drives
the console's new address. The 1610 start's logs were saved first (console_runs/c3/mt1).
- That 1610 start's self-test, on the hardware: 0 of 1,048,576 arguments differ from newlib for each of sinf,
  cosf, acosf and asinf (no NaN payload differences either). It ran inside the start-up boost (1785 MHz), and its
  acosf/asinf arguments were the buggy [-1, 3) range (half of them NaN cases), so its times are not quoted.
- mt2: build 202611181619 (the self-test's [-1, 1] fix), config-boostoff-mathtest.ini (boost off, profiler and
  live log on, math_selftest on), handheld, shader cache off. Tile read, title checked, The Dagger: Race It and
  206,800 credits read on screenshots; 60 rounds of ride_night.sh (14:19-14:55 PC time), then closed from the
  HOME menu.
- The self-test at 1020 MHz: 0 of 1,048,576 differ for each of the four; ns a call, ours against newlib's soft
  float: sinf 522 / 2908 (5.6x), cosf 525 / 2916 (5.6x), acosf 68.5 / 915.5 (13.4x), asinf 72.0 / 990.8
  (13.8x).
- The race, against the same drop on build 0624 (soft-float sinf/cosf/acosf; nc1-nc3, 362-401 race windows each):

  | Session | Race windows | Job threads (3, summed) | FGGameSim | FGGameRender | Sim step | Render |
  |---|---|---|---|---|---|---|
  | nc1 (0624, boost off) | 401 | 60.5% | 60.0% | 67.5% | 8.18 ms | 13.70 ms |
  | nc2 (0624, boost off) | 363 | 60.8% | 59.5% | 70.0% | 8.32 ms | 13.41 ms |
  | nc3 (0624, boost on) | 362 | 59.0% | 60.6% | 62.9% | 8.10 ms | 14.54 ms |
  | mt2 (1619, FPU math, boost off) | 171 | 53.1% | 63.3% | 69.5% | 7.54 ms | 13.62 ms |

  Shares of a core from the per-thread CPU times in the 10 s reports, race windows at 100% speed only
  (scratchpad jobcpu.sh). So the job threads use ~7 points of a core less (~12%), and a simulation step is
  0.6-0.8 ms shorter (7-9%). The rest of mt2: 60.00 fps mean, lowest window 59.9, none under 59; 14 loads (the drop
  start and 13 Restarts), the heap in use flat at 880 MB from the 6th; 0 GL errors; GPU 36% busy; SoC 35-42 C.
  The boost-off Restart frames: 583-641 and 503-528 ms, as nc1/nc2.
- The profiler couldn't confirm it by function: its 8,192-slot table (ssx_prof.c, put()) keeps the first 8,192
  (thread, pc) pairs it sees and drops every new one (`lost` in prof.txt's header: 668,161 in mt2). mt2 sat ~4
  minutes on the intro and menus before the race, so race-only code never got a slot: no vfp_* kernel, no
  b_sinf, and also no HIK2013 skeleton code in the job threads' list, which nc3 had. Every c3 profile has this
  bias toward what ran first (nc3: 1,370,786 lost); its percentages hold for what is listed, but a function
  missing from the list may only have come late. Fixed in build 202611182110 (pr1 below).
- Left: build 202611181619 and the owner's everyday config, boost off (config-everyday-v2-boostoff.ini, read
  back after the upload); the console on the HOME menu.

### pr1: the profiler fixed, and a profile of the race alone (2026-11-18 afternoon)

The owner's word: fix the profiler's blind spot and profile a race again.
- Build 202611182110. The tables (source/ssx_proftab.c): the sampler fills a fresh table per 10 s window
  (16,384 slots), and each window is merged into the session's table (prof.txt) and, when the window was a race
  at full speed (ssx_patch2.c's [fps] flag), into a race table (prof-race.txt, new). Those two (32,768 slots
  each) stay at most half full: when one gets there it keeps its 8,192 most-hit addresses and counts the
  samples of the rest as pruned (a new header field). The old hash, pc * 2654435761 on a power-of-two table,
  could start at only 1 slot in 4 (ARM pcs are multiples of 4); it now mixes all 32 bits (MurmurHash3's
  finalizer), and the probe limit is 128 (at half full a few runs pass 32). The merge and the file writes moved
  from the main thread's 10 s report to the sampler thread, at priority 58 while it writes; prof.txt and
  prof-race.txt are rewritten every 30 s. The tables (~2.3 MB) are allocated only when profiling (the old ones
  were ~0.9 MB of static memory in every build). prof_symbols.py reads the files as before.
- Host check, tools/test_proftab.c (clang on the PC): a made-up session of 5 minutes of menus (38,336 distinct
  addresses) then 40 of race (12,000 new ones), 8.1 million samples: every sample in a slot or pruned, none
  lost, the session table at most half full, and every one of the race's 163 hot addresses kept 100% of its
  samples in both tables (2.4% of all samples pruned, the long tail). The old single table on the same
  samples: 15% lost, and 0% of the race's code kept. The profiler is off under emulators, so Ryujinx can't run
  it.
- pr1: config-boostoff-measure.ini (the everyday config with profile and live log on), handheld, shader cache
  off. Tile read, title checked, The Dagger: Race It and 206,800 credits read on screenshots; 60 rounds of
  ride_night.sh (15:14-15:50 PC time), then closed from the HOME menu. No console crash, no game exit; 152 race
  windows at full speed, 60.00 fps mean, lowest 59.9, none under 59; job threads 50.8% of a core, step 7.32 ms
  (mt2: 53.1%, 7.54 ms); 7 loads, the heap in use 880-886 MB; 0 GL errors; GPU 34% busy; SoC 37-42 C; Restart
  frames 584-627 and 511-541 ms.
- The profiler's own figures: prof-race.txt from 199 race windows, 0 samples lost, 2.2-2.3% pruned (the
  `[prof] tables at half full` lines); prof.txt the same (2.3% pruned, 0 lost).
- What the race costs now, prof-race.txt. The General Job Threads: 81.9% of their samples idle (waiting for
  work), 14.2% busy. Of the busy time:

  | Work | Share of busy |
  |---|---|
  | blocked on a mutex or waking others (svcArbitrateLock 3.8% of all samples, from mutexLock) | 32% |
  | animation (EA::Ant blend nodes and command VM, HumanIK skeleton solving) | 30% |
  | pow (on the FPU since c3; EA's audio compressor) | 10% |
  | audio (EA::Audio resampling, decoding, FFT filters) | 8% |
  | memcpy/memset | 4% |
  | EA::Jobs | 3.5% |
  | soft float left: mostly double acos (newlib __ieee754_acos's __aeabi_dadd, __muldf3, sqrt) | 3.5% |
  | integer division (libgcc) | 3% |
  | Lua, physics and ray queries | 2% |
  | other | 3% |
  | sinf/cosf/acosf, now on the FPU | 0.7% |

  In nc3 the soft-float sinf/cosf/acosf were ~20% of this busy time (and nc3's table had dropped part of the
  race's code, so likely more). FGGameSim: 32% of its samples in sched_yield's sleep, 32% waiting for work,
  14% blocked in svcArbitrateLock; of its samples in mutexLock, 41% were called from b_clock_gettime (the runtime's
  clock_gettime takes a lock). FGGameRender: 18% waiting on events, 16% sched_yield, 12% blocked on a mutex,
  5% in binder IPC (the presents).
- Left: build 202611182110 and the owner's everyday config, boost off (config-everyday-v2-boostoff.ini, read
  back after the upload); the console on the HOME menu.

### hs1: HOME and back, and sleep, mid-game (2026-11-18 evening)

The owner's word: test what players do that the runs hadn't, HOME and sleep. Build 202611182110,
config-homesleep.ini (the everyday config, boost off, with the live log on; profiler off), handheld, shader
cache off. The port's handling (runtime/source/rt_applet.c): the system freezes the whole process for the HOME
menu and for sleep; bionic_time.c sees the freeze and leaves the time away out of the game's clocks.
- Three HOME trips, each read on screenshots before and after (the SSX tile, "Playing", A = Continue):

  | Trip | From | Away | Back |
  |---|---|---|---|
  | 1 | The Dagger's drop selection | 41.7 s | the same screen at 60 fps, the menus answering |
  | 2 | 2 s into the drop's load (HOME just after Start Run) | 40.8 s | the loading screen at 60 fps; the load finished (its 581 ms bake frame) and the race started at 100% speed |
  | 3 | mid-race, 2:04.0 on the race clock | 73.4 s | the race at 60 fps, the clock at 2:05.9: the time away did not count |

- Sleep (the owner: power button, ~19 minutes, power again and unlock), from the "Try again" screen after a
  fall, the 3D scene drawing: frozen 1,124.6 s; afterwards 60 fps at 100% speed at once, audio writes on at
  ~94 a second as before; the console at the same address. Presses still reached the game (sys-botbase's virtual
  controller survived): "Try again" continued, a Restart loaded, and a new race ran (0:04.3, later 1:54.4). No
  new `[clocks]` line after the wake (the report prints only on a change): CPU 1020, GPU 460.8, memory 1600 MHz
  as before.
- The whole session (77,085 frames): 97 race windows at full speed, 60.00 fps mean, lowest 59.9, none under 59;
  0 GL errors; no watchdog report, no game exit, no console crash. In all four freezes the game got no focus
  message (`[applet]` lines: only the start-up one), so the game's own onPause/onResume (PauseAudio, ResumeAudio)
  never ran: the freeze itself stops everything, and the clocks' freeze handling is what makes it come back right.
- Not tried: a HOME trip or sleep while docked, or a dock/undock while asleep; a sleep of hours.
- Left: build 202611182110 and the owner's everyday config, boost off (config-everyday-v2-boostoff.ini, read
  back after the upload); the console on the HOME menu.

### hd1: HOME and sleep docked, and undocking while asleep (2026-11-18 evening)

The owner docked the console (TV on) for the docked half of hs1. Build 202611182110, config-homesleep.ini.
Started docked: `[clocks] running (docked): CPU 1020 MHz, GPU 768.0 MHz`, the window and the scene 1920x1080.
- Three HOME trips as in hs1, the tile read before each Continue:

  | Trip | From | Away | Back |
  |---|---|---|---|
  | 1 | the drop selection | 42.0 s | the same screen at 60 fps; no clock change |
  | 2 | 2 s into the drop's load | 42.0 s | the loading screen at 60 fps; the load finished (its 576 ms bake frame), the race at 100% speed |
  | 3 | mid-race, 1:47.3 on the race clock | 70.9 s | 60 fps, the clock at 1:57.7 (the ~10 s ridden around the trip) |

- Sleep in the dock (the owner: power, ~1.7 minutes, wake in the dock): frozen 99.4 s. On waking the system
  sent operation mode handheld (0), then docked (1) again, with their performance-mode messages; one 10 s
  report caught the GPU at 460.8 MHz (the port's handheld configuration, applied while the mode read handheld),
  the next 768 MHz again. One frame took 833 ms right after the wake: no CPU in the render thread (the
  simulation ran on at 60 steps a second), so it waited, most likely for the TV output to come back. That 10 s
  window drew 55.2 fps, then 60.0. Input worked: "Try again", a Restart, a new race.
- Undocking while asleep (the owner: sleep in the dock, lifted out, woken in handheld): frozen 4.6 s (a short
  sleep), then operation mode handheld; `[clocks] running (handheld): CPU 1020, GPU 460.8 MHz`. No long frame;
  60 fps at once, the 1920x1080 window scaled to the screen by the console (follow_docking off), the GPU ~40%
  busy at 460.8 MHz. Input worked: a Restart, a new race at 60 fps.
- The session (86,270 frames): 107 race windows at full speed, 59.95 fps mean, the one under 59 the docked wake's
  55.2; 0 GL errors; no watchdog report, no game exit, no console crash. As in hs1, no focus message in any
  freeze; the mode messages came only with the dock changes.
- At the end, closing it took three tries. Presses came that weren't the script's, with the console in the
  owner's hands after the undock: the game came back from the HOME menu, a Restart started between two HOME
  presses, and the first Close was followed within ~3 s by a new start of SSX from the HOME menu. Either a
  press from the console's own buttons, or the Close's A delivered twice and the second started the selected
  tile. That new start reached the title screen, in handheld (its log in hd1/relaunch-debug.log); closed again,
  the title empty at once. The session's log was then debug.prev.log (saved as hd1/hd1-debug.log). Lesson:
  while the owner holds the console, read the screen after every press, and expect presses that aren't ours.
- Left: build 202611182110 and the owner's everyday config, boost off (config-everyday-v2-boostoff.ini, read
  back after the relaunch too); SSX closed, the console on the HOME menu, in handheld.

### sc1: the shader cache back on (2026-11-18 evening)

The owner deleted shadercache/off (off since crash 3, 2026-11-17). Build 202611182110, config-homesleep.ini
(the everyday config with the live log; shader_cache = true as in every config), handheld. The old pack stays
aside as mesa_shader_cache.pack.suspect; this Mesa starts its own.
- Cold start (c): 575 programs compiled and stored during start-up, 708 (5.3 MB) once the drop had loaded;
  the start marker removed after a minute of frames; start-up's longest frame 7.1 s (compiling and writing at
  once; 3.6-3.7 s without the cache). From the second race on, the Restarts found their programs in the cache
  (169, then 210 found) and compiled none. Closed cleanly; the pack 5,473,023 bytes.
- Warm starts w1, w2, w3 (each launched from the HOME menu after a clean close): 608-610 programs found at
  start-up, 0 compiled, in every one; the start marker removed after a minute each time; start-up's longest
  frame 2.34, 2.33, 2.33 s. No console crash, no game exit (crash 2 had come at the first start that read the
  cache).
- Where the time went, without the cache (pr1, hs1) against warm (w1, w3), The Dagger, boost off:

  | | No cache | Warm cache |
  |---|---|---|
  | start-up's longest frame | 3.64-3.68 s | 2.33-2.34 s |
  | the drop start's loading screen | 6 frames of ~500-540 ms (AssetStream::Tr compiling), then the bake (581-627 ms) | the bake alone, 578-584 ms |
  | the menus before it | one ~400 ms compile frame | none |
  | each Restart | a compile frame (AssetStream::Tr, ~515 ms) and the bake (FGGameSim/Render, 584-627 ms) | the bake alone, 522-528 ms |

  So a Restart's freeze goes from ~1.1 s in two frames to ~0.52 s in one.
- w3, the long ride: 60 rounds (18:49-19:25 PC time), 172 race windows at full speed, 60.00 fps mean, lowest
  59.9, none under 59; 16 loads (the drop start and 15 Restarts), not one compile frame among them; 1,492
  programs found in the cache over the session, 0 compiled; 0 GL errors; GPU 36% busy; job threads 54.3% of a
  core, step 7.58 ms.
- The cache stays on: the owner's everyday config has shader_cache = true, the marker is gone, the pack is
  on the card. The safety net (shadercache/starting, then off on a young death) stays in the build.
- Left: build 202611182110, the owner's everyday config, boost off (config-everyday-v2-boostoff.ini, read
  back); shadercache/mesa_shader_cache.pack (5.3 MB) and the old .suspect; the console on the HOME menu.

## Release 0.1.0 check, rel1 (2026-11-18, night)

The build of the first release, 202611190444 (`out/release-v0.1.0`: dependencies rebuilt with `tools/deps`, file
names mapped, the drawn icon), on the owner's word, handheld, with the owner's everyday config unchanged (boost
off, shader cache on, frame counter on). The NRO put on the card was read back identical to the release's.
- Started from the SSX tile (title 0x05A9F60353E17000): the launcher found build 202611190444 newer than the
  installed 202611182110, installed it and restarted (debug.prev.log); then the game started on its own.
- The rebuilt Mesa gives a new shader cache name (2843f03515821425), so a cold cache: 575 programs compiled at
  start-up, all linked (0 failed), stored in the pack; no shadercache/off afterwards.
- The boot movie, the intro movie and the Rockies' movie played at 60 (FFmpeg rebuilt). After the self-update
  restart, presses reached the game only after `raw "detachController"` (as seen before).
- World Tour > Rockies > Whitehorn Mountain > Curnoe's Moustache: Trick It (crash 3's drop, also on a cold cache
  then): 54 of 59 race windows at full speed, 59.98 fps mean, lowest 59.3; then Restart Drop: the load's long
  frames 134-279 ms (FGGameSim, AssetStream::Tr), the drop back at 60. 130,528 bone palettes, 0 with NaN; no GL
  error logged; SoC 36-39 C; GPU 45-47% busy.
- Closed from the HOME menu (X, Close): the title empty, no new crash.log. No console crash.
- Left: build 202611190444 on the card (the release's NRO), the owner's config as it was, the console on the
  HOME menu. The SSX tile keeps the old icon until the forwarder is made again.

## B on the main menu: the game's own quit crashed the exit (2026-11-19)

The owner found that B on SSX HOME (and on what they took for course select) ended the game with an error:
2345-0008 once, 2349-0004 twice (Atmosphere's fatal reports: the result only, no registers or stack for a 32-bit
program). debug.log was empty: the program died before closing it.
- B is the game's Circle (key 100); on its top menu the game answers it with MainActivity.exitApp, the Android
  "quit" (back1, back2). The port then left its loop and returned from main, and libnx's exit closed the window
  first, while FGGameRender was still drawing at 60 fps: Mesa's Switch EGL driver calls fatalThrow() when the
  window refuses a buffer (2349-0004, LibnxBinderError_BadValue) or is already gone (2345-0008,
  LibnxError_NotInitialized). The HOME menu's Close never hit it: the game has stopped drawing by then.
- A diagnostic build (202611190701) wrapped fatalThrow() and diagAbortWithResult() (source/ssx_abort.c: the
  result, the caller, the thread and the stack's code addresses to the log, the newest log lines to abort.log).
  This abort came after the log and the card were closed, so it wrote nothing, but debug.log, closed in time,
  showed key 100 -> exitApp -> leaving -> exiting, and the crash.
- Fix (202611190712): on the game's quit, no frame is asked for any more, the render thread is held at its
  next eglSwapBuffers (ssx_egl.c), and the exit waits until it is held or no frame has come for 100 ms (at most
  2 s) before libnx closes anything (ssx_boot.c). back3: B on SSX HOME twice (two starts): "its render thread
  held at its next frame after 15 ms" / "10 ms", back to the HOME menu with no error and no new fatal report;
  B in World Tour went back to SSX HOME as before. The abort hook stays: a later abort leaves abort.log.
- Then, on the owner's word ("pressing B does nothing", no setting): exitApp is ignored (ssx_java.c), so B on
  SSX HOME does nothing and the game carries on; the clean quit stays for any later use. back4 (a test build with
  a setting) and back5 (202611190730, the final one): B on SSX HOME twice, the game stays on the menu at 60 fps,
  A then opens World Tour; closed from the HOME menu cleanly.

## The face-button icons with the Switch's letters (2026-11-19)

The owner asked for the game's Cross / Circle / Square / Triangle icons as the Joy-Con's letters. Each icon is a
36x36 RGBA4444 texture; ssx_buttonicons.c redraws its inside as the Switch letter that does the button's job,
following swap_a_b (checked first on the PC: the C code run over the APK's four icons, the ring untouched).
- icons1 (202611191504, swap_a_b off): Help > "On the Ground Control Layout" and "Freefalling/In-Air": Jump
  "Hold (A)", Classic boost "(Y)", Classic rewind "Hold (B)"; debug.log: x -> A, circle -> B, square -> Y (no
  page loaded the Triangle icon). A Whitehorn race at 60 fps (no face-button hint on that drop), Settings >
  Controls (still the PlayStation pad picture, by design), closed from the HOME menu cleanly.
- The owner wants Cross = B and Circle = A: on their word, swap_a_b = true in the card's config.ini (B confirms
  and jumps). Second start: B on the title and in the menus, Help shows Jump "Hold (B)", Classic boost "(Y)",
  rewind "Hold (A)"; debug.log: x -> B, circle -> A, square -> Y. Closed cleanly; no new crash.log.
