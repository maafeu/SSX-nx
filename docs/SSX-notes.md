# SSX-nx: notes

SSX (EA Sports, 2012) for the Nintendo Switch, from its Android build
(`com.ea.ssx` 0.0.8430, `ssx-v0_0_8430.apk`, armeabi). The game's own ARM
code runs unmodified in a 32-bit (AArch32) Switch process: nothing is
recompiled. This file records what was decided, why, and what each
game-specific fix is. The progress log is `docs/PROGRESS.md`.

## Decisions

| Decision | Why |
| --- | --- |
| Port on the **android32** runtime (copied into `runtime/`, unmodified), not ReXGlue | ReXGlue recompiles Xbox 360 PowerPC code. The Android build is already ARM machine code the Switch's CPU runs as it is (AArch32), so it is loaded, not recompiled: android32 is the so-loader runtime built for exactly this (its ports: Asphalt 8, Sonic Racing, Dead Space...). |
| The "PC port" is the Switch build running in Ryujinx on the PC | A native x86 build would need an ARM CPU emulator inside it (the game is ARM code, and no recompiling was asked for). Ryujinx runs the same 32-bit NSP on the PC; it is the intermediary step and the test bench. |
| Built natively with devkitPro's devkitARM 16.1 in devkitPro's MSYS2, no Docker | Docker is not installed. The vita2hos container the base uses is devkitARM + switch-tools + libnx32 + miniz; the same pieces were built here (`tools/deps/build_deps.sh`). |
| `int32_t` is `int` in every build (`SSX_I32` in the Makefile, `I32` in build_deps.sh) | Bare-metal arm-none-eabi makes `int32_t` `long`. libnx32, mesa32 and the runtime were written where it is `int` (their format strings, Mesa's C++ overloads on `uint32_t`). Same size and C ABI either way; built the other way Mesa did not compile and the runtime warned on every `%x` of a `Result`. |
| `tools/elf2nso32.py` writes the NSO | devkitPro's `elf2nso` takes AArch64 ELFs only (the vita2hos image's took 32-bit ones). Same header layout as switch-tools' elf2nso; segments uncompressed, SHA-256 checked. |
| miniz v1.15 (public domain) for the runtime's zlib/zip | The image's libminiz is not in devkitARM. v1.15 (a copy already on this PC) plus `mz_inflateReset` (added; later miniz has it). |
| Assets read in place from the APK (`ssx_assets.c`) | No extraction step: the 643 stored entries (movies, music streams, UI movies, level .bin) are read at their offset, `AAsset_openFileDescriptor` hands back a descriptor on the APK as Android's does; the deflated ones are inflated (whole up to 8 MB, streamed above). The central directory is indexed once (11,227 assets in 17 ms). |
| libmercury.so and libnimble.so not loaded | EA's DLC downloader and telemetry: the APK holds all content and EA's servers are gone. libgame.so's 14 imports from them are stubs (`ssx_stubs.c`); MercuryManager::Init reports the package as downloaded (as the PS Vita port does). |
| The game's own first-start copy of its streams is kept | `GameCore::CopyBigFilesOutsideAppBundle` copies ~750 MB (music, movies) from the APK into `data/files` once, as on Android. The Vita port skipped it for its memory card; on the Switch's SD it is a one-time ~1-2 min cost. Revisit if it proves slow on hardware. |
| Controls by function: A = Cross (confirm), B = Circle (back), Y = Square, X = Triangle | The Android build accepts one controller, named "PLAYSTATION" (MainActivityGenerated.mTargetControllerName), with its PlayStation key codes. `[controls] swap_a_b` gives the positional layout. |
| Face-button icons with the Switch's letters (`ssx_buttonicons.c`) | The game's hints and Help pages show PlayStation icons (its one controller is "PLAYSTATION"). Each is a 36x36 RGBA4444 PVR, `common/fe/assets/button_icons/button_ps3_{x,circle,square,triangle}.etc1.pvr`, redrawn in memory as it is read (through `ssx_assetpatch.c`): the ring kept, the symbol replaced by the letter of the Switch button that does its job (Cross A, Circle B, Square Y, Triangle X; with swap_a_b Cross B, Circle A), drawn from strokes in the symbol's colour. The game's Xbox icons were not used: their letters sit in the Xbox's places. Unchanged: the shoulder and Start icons, and the PlayStation pad pictures (controller maps, loading screens). |
| 60 fps: menus 60/60, races 30 Hz simulation / 60 Hz render | The PS Vita port's analysis (its fpscap.c): the race simulation advances a fixed step per iteration and cannot catch up, so it stays 30 Hz; the renderer interpolates. `[display] race_fps` / `menu_fps` = 30 restores the original. |
| Title id 0x0100000000001055, SD folder `sdmc:/switch/ssx-android` (NRO `ssx-android.nro`) | One title id per android32 port. Not `/switch/ssx-nx`: the console's card already holds the Xbox 360 SSX port there (the other session's ReXGlue NRO, its toml, shaders and game files); a shared folder would overwrite its NRO and mix the two ports' files. No old-folder migration from it, ever. The 32-bit program keeps the name `ssx_nx`. |
| Two of the game's text files edited in memory as they are read (`ssx_assetpatch.c`), the APK untouched | The PS Vita port's data tool (v1.1.1; its output was reviewed file by file against the APK: same libgame.so, 3,502 files identical, 4,763 only moved into sub-folders) makes three text edits besides shrinking textures, movies and sound for the Vita's 512 MB. Taken: `memory.cfg` (EA's heaps asked for ~573 MB, all of it real memory here: 817 MB of the 992 MB heap in use at the first frame, 909 MB in a race; five heaps sized from the Vita's measured peaks with a margin: 680 MB at the first frame and 782 MB in a race after, emu31) and `nav.lua` (no full Lua collection after every menu move: 15-60 ms each on the Vita). Not taken: the 960x544 render target (the Switch draws 1280x720), and the shrunk media (the Switch has the memory; the APK's quality stays). `[performance] trim_heaps` / `menu_full_gc` undo them. |
| Ryujinx: PPTC off in this port's data folder | The game's code is loaded at run time; PPTC replayed old translations at addresses that no longer held code and crashed the emulator at start (emu4). |

## Game-specific fixes

| Where | What | Found by |
| --- | --- | --- |
| `ssx_java.c` GetAccelerometerCount = 1 | GameApplication::AddAccelerationListener dereferences the accelerometer module; with 0 the game crashed at boot (emu1, NULL at libgame+0x15e280). | emulator run 1; the Vita port answers 1 too |
| `ssx_java.c` GetTouchScreenCount = 1 | GameApplication::AddPointerListener needs the touch module (emu2, NULL at libgame+0x15e3bc). | emulator run 2 |
| `ssx_patch.c` MercuryManager::Init | reports the SSX package downloaded (data store 456/453), no DLC screen. | Vita port |
| `ssx_patch.c` Dac::SubmitSamplesThreadFunc | waits for Dac::sDacActive before running: the mixer thread lost a race with Dac::Start and the boot hung. | Vita port |
| `ssx_patch.c` OutOfMemory (x2) | logs the failing heap before the game's own handler. | Vita port |
| `ssx_patch.c` File::Copy | the first start's copy of the music and movies (~750 MB) out of the APK: EA's copy writes 2 KB at a time, ~0.43 MB/s on the console's SD card (~25 min of black screen); an `appbundle:` source is now read from its APK entry and written in 4 MB pieces (~5 MB/s: ~2.5 min). The same for file-to-file copies: the save copies its 25 MB ghosts file to a new name at every start (the "Saving" after the title: 75-78 s, now 1.5 s; title to menus 9 s instead of 50-90 s). A copy onto itself is refused (EA's truncated its own source). | hardware run c2 |
| `ssx_patch.c` GameRender::PlatInit | logged only (the 1280x720 render target, kept). | |
| `ssx_patch2.c` FpsLimiter, GameKernel::Loop | 60 fps (above). `[display] race_sim_hz = auto` (default): races simulated at 60 Hz, 30 Hz for the rest of a race the console cannot keep up with (never slow motion). On the console the first drop holds 60 steps a second at 10-11 ms each. | Vita port; hardware run c2 |
| `ssx_boot.c` input gate | no input before a picture is up and FEDataStore exists (input earlier crashed the Vita build). | Vita port |
| `ssx_egl.c` | EGL10 (Java) on Mesa's EGL: the game's context and window surface are real; eglDestroy* are ignored (the context stays across focus changes). | |
| `ssx_input.c` | input_script.txt for unattended runs (an emulator window gets no pad). | |
| `ssx_hwskin.c` | riders skinned on the GPU (GpuSkinningFeatureImpl, the _GpuSkinned techniques). | Vita port |
| `ssx_terraincull.c` | chunks the frustum test classes outside get no LOD (not drawn). | Vita port |
| `ssx_movie.c` / `ssx_video.c` | the movies (FFmpeg) drawn before the swap, their sound mixed in; completion reported to the game. | |
| `ssx_assetpatch.c` memory.cfg | 5 heaps resized (Android -> here): GAMEWORLD_GEN 20->4M, STL_GEN 10->4M, FE_SFGFX_ASCRIPT 12->4M, MEMORY_RESIDENT_GEN 5->1M, RENDER_GEN 250->128M. A first cut also shrank the menu (FE_*) heaps and GLOBAL_ASSETTMP to the Vita's sizes plus a margin; two of four emulator runs then crashed at the first movie with a NULL (emu27, emu30), so those keep Android's sizes until `[heaps]` (debug.log, each heap's peak) gives this port's own numbers. Kept: AUDIODATA_GEN 60M (the Vita's 36M fits its re-encoded ambience bank only), FE_SFGFX_GEN_A 35M, GAMEWORLD_SLOTALLOC 25M, GLOBAL_GEN 15M. | Vita data tool (patches.py) |
| `ssx_mathguard.c` | acos/asin (float and double) take their argument clamped to [-1, 1]; powf/logf/fmodf NaN results and expf overflows are counted (`[math]` in debug.log). | the rider NaN hunt |
| `ssx_spinwait.c` | the 7 busy-wait loops (EA::Ant scene ops and CacheLock, interaction scenes, audio buffers) sleep 100 us between checks through veneers in the hook trampoline space: no time-slicing among equal priorities froze the Vita's races, and Horizon schedules the same way. | Vita port |
| `ssx_emufp.c` (emulator only) | Ryujinx's fast FP computes VRECPS/VRSQRTS as plain `2 - n*m` / `(3 - n*m) / 2` (NaN for 0 x inf, where ARM gives 2.0/1.5); all 1,688 in libgame's ARM functions (found through the file's .symtab) branch to stubs that restore ARM's rule. Room for the stubs after the image: the runtime's weak `port_so_extra_bytes` (so_load), 0 on hardware. Did not cure the riders (emu33). | the rider NaN hunt |
| `ssx_ikdebug.c` | `[debug] skip_ik` auto/none/solvers/all: HumanIK's plugin functions return at once (bx lr). Default auto: all of HumanIK under an emulator (its pose conversion makes the riders' bones NaN there: emu35), none on the console. | the rider NaN hunt |
| `ssx_overlay.c` | `[display] fps_counter`: frames per second top left, and in races the simulation's speed (the Vita port v1.1's counter). | Vita port v1.1.0 |
| `ssx_assetpatch.c` nav.lua | `P:collect()` no longer runs `collectgarbage("collect")` (a hitch on every menu move). | Vita data tool |
| `ssx_livelog.c` | `[debug] live_log`: the newest 24 KB of log lines rewritten to live.log every 10 s (open, write, close), readable over FTP while the game runs; debug.log stays open for writing and Horizon keeps other processes out of it. | hardware run c2 |
| `ssx_movie.c` uploads | a movie frame is uploaded only when a new one is due, into one of two texture sets in turn: the whole 1080p frame went up at every swap into the texture the last draw used (47-54 fps during the intro movies on the console). | hardware run c2 |
| `runtime.mk` `-DMINIZ_NO_TIME` | libminiz.a is built with it; without it here `mz_zip_archive_file_stat` had an extra `time_t` for every reader, `.setup` recorded garbage CRCs and sizes, and both libraries were unpacked again at every start (~20 s). | hardware run c2 |
| `ssx_overlay.c` / `ssx_movie.c` buffers | the counter's and the movies' vertices go through a buffer of their own: with their own VAO bound, ES 3 refuses client-memory arrays (`glVertexAttribPointer(non-VBO array)`), the console run's only GL errors; the "[hwskin] GL error 0x502 after a rider mesh" lines were that error read late. 0 GL errors since. | hardware run c3 |
| `ssx_vfpmath.c` `b_pow` | devkitARM's newlib is the soft-float multilib: pow did every double operation in integer code. EA's audio compressor (`CompressorLimiter1::CalcGainsUnVectorized`) calls pow on the job threads, about half of their race CPU. fdlibm's e_pow compiled for the FPU (`-ffp-contract=off`: the same results; host check: 2,000,000 cases within 1 ulp). | hardware profile, run c3 |
| `ssx_atomic64.c` | EA::Thread's 64-bit atomics (`android_fake_atomic_read/swap/cmpxchg_64`, the ARMv5 fallback: one of 32 mutexes per call, ~35,000 calls a second) as LDAEXD/STLEXD loops. `[performance] native_atomics`. | hardware profile, run c3 |
| `ssx_detail.c` | `[display] detail_distance`: `GameObject::SelectLOD` (props, trees, instances) and the terrain's take their metric scaled; 100 = as shipped. | |
| `ssx_render.c` + `ssx_assetpatch.c` main.scrape | `[display] render_resolution` auto/720/900/1080: the 3D scene's render target ("Scrape": `mobile/scrape/main.scrape`'s `FrameBufferTexture 1280 720` and its D24S8 depth surface, fixed at 1280x720 whatever the window) is resized as the script is read; the post-effects pass scales it to the window, and the menus and HUD draw at the window's size. Found by logging the render targets the RNA layer makes (`RNA::TexturePlatC::Create` / `CreateAndSet`, `RNA::SurfacePlatC::Create`), which stays (once each). | Ryujinx, hardware run c3 |
| `ssx_gl.c` glFinish | `[performance] gl_finish` (flush): `LoopRender::RenderFrame` (and `GameLoopProcessTransition`) call glFinish every frame through `EA::Graphics::OpenGLES20`, so the render thread waited out the GPU each frame (Mesa: `st_finish`, a spin on the last fence) and a frame cost the CPU's time plus the GPU's. Found with `tools/mesa32/fence-wait-stats.patch` (`[fence]` in the log): every blocking GPU wait was that one. flush: glFlush instead. | hardware run c3 |
| `ssx_shadercache.c` + `tools/mesa32/shader-cache-switch.patch` | `[performance] shader_cache` (on): Mesa's shader disk cache on the Switch (mesa32 had none: no mmap, flock, nftw or zlib there); one pack file in `shadercache/`, read back by later starts. Console: start-up stall 6.4 -> 1.9 s, long frames 12.4 -> 4.7 s in a Curnoe's Moustache session, the ride with no window under 59. A start that dies in its first minute turns it off for the next (`shadercache/off`): 1 of 9 warm starts was followed by the console going down, not explained. | hardware run c3 |
| `ssx_saltynx.c` | `[display] saltynx`: Status Monitor Overlay's FPS through SaltyNX's shared memory, NX-FPS's part done by the port (SaltyNX skips homebrew titles). | |
| `ssx_clocks.c` / `ssx_prof.c` / `ssx_sched.c` | the clocks and each busy thread's share of a core every 10 s; `[performance] handheld_gpu` (460.8 MHz in handheld, off by default); `[debug] profile` (a sampling profiler into prof.txt, `tools/prof_symbols.py`); `[performance] threads_first`. | hardware run c3 |
| `runtime/source/so_util.c` | emulator only: the code runs below 2 GiB (Ryujinx's Thumb CBZ sign-extension). Handed off to the base. | emu13 |

## Under the emulator

- The game's own read-back captures (`capture-NNN.bmp`) come out partial
  (left quarter, or black) under Ryujinx; the real picture is correct
  (window shots, `-Shots` in `tools/run_ryujinx.ps1`). Known for android32
  ports (runtime/docs/MIGRATING.md).
- One crash in ten runs on the game's render thread right after the first
  movie was skipped (emu8, Ryujinx reported a stale PC in libgcc); not seen
  again in the next runs. Watch.

## Open

- **FOUND (emu35): the rider's NaN comes from HumanIK's pose conversion
  under Ryujinx.** With all of HumanIK skipped no palette has a NaN and the
  rider shows; `[debug] skip_ik = auto` (the default) skips it under an
  emulator only. Still open: the instruction in PoseToHikPlugin /
  HikToPosePlugin the emulator gets wrong (to keep HumanIK there too). The
  console needs nothing: hardware run c2 (2026-11-16) ran the first drop with
  full HumanIK, the rider on the board, 38,748 palettes and 0 with NaN. The
  history of the hunt, as it was written:
- **The rider is invisible in races under Ryujinx.** Its bone palettes are
  NaN before the GPU sees them (`[hwskin] N palettes uploaded, M with NaN`),
  and one emulator crash was in EA::Ant::Anim::CurveClipNode::Exec with a wild
  read. Asset bytes, FPSCR and shaders are ruled out; so is what the game
  reads in /proc/cpuinfo (`[debug] cpuinfo = empty`, the nearest to the
  Vita's missing file: emu29, same 92%). Only some bones of each palette are
  NaN (one, in some pieces), the board's never: a solver's work. Suspected:
  the emulator's "fast FP" (Ryujinx computes VRSQRTE/VRECPE and some float
  operations with x86 instructions that round differently; it cannot be
  turned off without rebuilding Ryujinx). Experiments in the build:
  `ssx_mathguard.c` (acos/asin clamped, counted: never out of range, 22%
  of calls already NaN; sqrt: 31% NaN arguments, some exact zeros),
  `[debug] skip_ik = solvers` (emu32: same 92%: not HumanIK's solvers),
  `ssx_emufp.c` (Ryujinx's VRECPS/VRSQRTS 0 x inf bug fixed for the
  emulator, emu33: same). The first NaN palettes are identical in every
  run (240, 12, 252, 84, 36, 24 NaN floats), from the race's first frame.
  HumanIK is ARM code (not Thumb), VFP. Not yet tried: `skip_ik = all`,
  and the animation decoder (CurveClipNode::Exec: VTBL byte shuffles,
  checked in Ryujinx's source and fine). The console run decides.

## Licences

FFmpeg 7.1.1 (LGPL 2.1+) is linked statically for the movies (ffmpeg32's
build: MOV demuxer, MPEG-4, AAC; no GPL parts). Its source is ffmpeg.org's
7.1.1 tarball and the build is `tools/deps/build_deps.sh ffmpeg` (aks796's
ffmpeg32 with `tools/ffmpeg32/native-build.patch`); a player who wants to relink can rebuild
the NRO from this source with another FFmpeg. Mesa, libdrm_nouveau, libnx32,
miniz (public domain) and the runtime are MIT-style.

## Credits

The android32 runtime (aks796 and its ports' authors; MIT), libnx32, mesa32
and ffmpeg32 (aks796, from xerpi's vita2hos), the hook trampolines of the
Sonic & SEGA All-Stars Racing port (aks796), and above all the PS Vita port
of SSX by lukebrl (ssx-vita, MIT), whose reverse engineering of libgame.so
found most of what a port of this build needs.
