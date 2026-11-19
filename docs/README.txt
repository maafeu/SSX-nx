SSX for Nintendo Switch
=======================

SSX (EA Sports, 2012), the Android version, running on the Switch: World
Tour, Explore, all four riders, the movies and the music, at 60 frames a
second. In handheld the 3D scene is drawn at 1920x1080 and scaled to the
screen; docked, the picture is 1920x1080.

This package holds no part of the game. You need your own copy of the
Android game's APK file; the port reads it in place.


WHAT YOU NEED
- A Switch running Atmosphere (custom firmware), with sphaira (the
  homebrew menu that makes HOME-menu icons, "forwarders").
- The APK of your own SSX for Android: com.ea.ssx version 0.0.8430,
  about 1.3 GB (usually named ssx-v0_0_8430.apk; any name works).
- About 2.2 GB free on the SD card (the APK, ~750 MB of music and movies
  the game copies out of it, ~40 MB of game libraries, ~5 MB of shaders).


INSTALL
1. Copy the SD folder's contents to the root of your SD card, so you have
   sdmc:/switch/ssx-android/ssx-android.nro
   (Made your package with the web installer? Extract its zip to the root
   of the SD card instead: it holds your APK too, so skip step 2.)
2. Copy your APK into the same folder: sdmc:/switch/ssx-android/
3. On the Switch, open sphaira. It installs nothing until installing is
   turned on: Menu (Y) > Advanced Options > Install options > "Enable
   emummc" (or "Enable sysmmc" on a console without emuMMC). sphaira
   warns: "Installing apps will lead to a ban!". An icon installed on
   emuMMC, kept offline, is the safe way.
   Then, in the Homebrew list, select SSX ("SSX-nx port"), press X >
   Install Forwarder > Create Forwarder (the defaults are fine).
   This puts an "SSX" icon on the HOME menu.
4. Start the game from that SSX icon. Always use the icon: started from
   inside sphaira or the homebrew menu, ssx-android.nro only shows these
   steps.


THE FIRST START (once)
It takes a few minutes, then never again:
- The launcher installs the game for the SSX icon and restarts by itself.
- A progress bar while it unpacks the game's libraries from the APK
  (~10 s).
- About 20 s of black screen before the EA logo, then about 2 minutes on
  the "Loading" screen after the title: the game copies its music and
  movies out of the APK into sdmc:/switch/ssx-android/data/files.
- The first start and the first visit to each course also prepare the
  graphics shaders, so they load a little slower. The shaders are kept on
  the SD card (the shadercache folder); later starts and loads are faster.
At the title screen press A. The first time, the game asks you to accept
its online agreements: D-pad Down to ACCEPT, then A.


CONTROLS
The game's own layouts (the STANDARD controls), on the Switch's buttons:

  Button        On the ground              In the air
  A             Jump (also starts a run)   Grab
  Y, B          Nose press, tail press *   Grab
  X             -                          Grab
  ZR            Boost (hold)               Signature move (ZL + ZR)
  ZL            -                          Signature move (ZL + ZR)
  R             -                          Deploy the wing suit (hold)
  L             Rewind (hold)              Rewind (hold)
  Left stick    Carve                      Spin / flip, steer the wing suit
  Right stick   Push up: jump              Grabs (by gestures)
  +             Pause                      Pause

In the menus: A confirm, B back, the D-pad or the left stick to move,
ZL / ZR to switch pages.
* The port's own addition, as in the console versions of SSX: hold Y for a
nose press, B for a tail press; board_presses = false turns them off.
The game's pause menu, Settings > Controls, shows these layouts with
PlayStation buttons (Cross = A, Circle = B, Square = Y, Triangle = X,
L1 / R1 = L / R, L2 / R2 = ZL / ZR, Start = +) and switches between its
STANDARD and CLASSIC controls.
Prefer the bottom button (B) to confirm, as on a PlayStation pad? Set
swap_a_b = true in config.ini.


SETTINGS
The game writes sdmc:/switch/ssx-android/config.ini at its first start;
every setting in it has a comment. Change it with the game closed. The
settings folder in this package has ready-made copies: rename one to
config.ini and put it in sdmc:/switch/ssx-android/.
- config_play.ini: the defaults.
- config_fps_counter.ini: a frame-rate counter in the top-left corner.
- config_boost_off.ini: no CPU boost while loading (see below).
- config_race_sim30.ini: races simulated at 30 Hz, as on phones.
- config_original30.ini: 30 frames a second everywhere, as on phones.
- config_rider_fix.ini: only if your rider ever shows up invisible.
Settings worth knowing:
- resolution (auto, 720 or 1080) is the screen picture: auto means 1080
  when the Switch is docked at start-up, 720 in handheld. It is chosen at
  start-up. If you dock or undock while playing, the picture is scaled to
  fit; to keep full 1080 when you dock mid-game, set resolution = 1080.
- render_resolution (1080, 900, 720 or auto) is the 3D scene. 720 or auto
  saves battery in handheld.
- detail_distance (100 = as the game shipped): 150 or 200 keep trees and
  objects detailed farther away, at some cost.
- movies = false skips the movies.
- boost_cpu_when_loading (on): runs the CPU faster during loads, so a
  Restart freezes for a shorter moment. Set it to false if you prefer.
- shader_cache (on): keeps the compiled shaders on the SD card.


GOOD TO KNOW
- The HOME button and sleep mode work at any time, also mid-race and while
  loading; the race clock does not count the time away. Waking up in the
  dock can show one short pause while the TV picture comes back.
- The game's online features (EA's servers) do nothing.
- With SaltyNX and Status Monitor Overlay installed, Status Monitor's FPS
  counter and graph work for SSX.
- If sdmc:/switch/ssx-android/shadercache/off ever appears by itself, a
  start with the shader cache ended badly and the cache switched itself
  off. Delete that file to turn it back on.


UPDATING
Replace sdmc:/switch/ssx-android/ssx-android.nro with the new one and start
the SSX icon: the first start updates the game and restarts once. You do
not need a new forwarder. Your progress and settings are kept.


YOUR PROGRESS
The game saves in sdmc:/switch/ssx-android/data/files/ssxv/ (your profile
and your ghosts). Copy that folder somewhere to back up your progress.


IF SOMETHING GOES WRONG
- "APK: MISSING" on the launcher's screen: no SSX APK in
  sdmc:/switch/ssx-android/ (the launcher looks inside the files for the
  game's armeabi library). Only version 0.0.8430 is supported: another
  version may be found, but the port is made for that one and it may not
  run.
- "This was opened from inside another icon": start the game from its own
  SSX icon on the HOME menu, not from sphaira or the homebrew menu.
- Anything else: send sdmc:/switch/ssx-android/debug.log (and crash.log
  or abort.log, if there is one) with a note of what happened. debug.log
  is rewritten at every start; the start before is kept as
  debug.prev.log.


REMOVING IT
1. Delete the SSX icon: HOME menu, select SSX, + (Options) > Manage
   Software > Delete Software.
2. Delete the folder sdmc:/switch/ssx-android/ (back up data/files/ssxv/
   first if you want to keep your progress).
3. Delete the game program the launcher installed for the icon: the
   folder sdmc:/atmosphere/contents/<the icon's title ID>/ (the launcher
   showed its path on the first start: "Installed the game program for
   this icon").


ABOUT
SSX is a trademark of Electronic Arts Inc. This is an unofficial fan port,
not affiliated with or endorsed by Electronic Arts or Nintendo. No game
code, music or other data of SSX is included; the port runs the game from
your own copy. The icon shows the SSX logo, a trademark of Electronic Arts.
Source code and new versions: https://github.com/maafeu/SSX-nx
The port's own code is MIT-licensed. It includes FFmpeg (LGPL 2.1 or
later) to play the movies, and Mesa, libdrm and libnx; their licences come
with their sources.
