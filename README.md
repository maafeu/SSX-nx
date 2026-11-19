# ssx-android web installer

A web page that makes the Switch package of the SSX port from the player's own APK, in their browser: it checks the
APK (`com.ea.ssx` 0.0.8430) from its central directory, without reading the rest, and streams a zip to a download:

```
switch/ssx-android/ssx-android.nro      the launcher (from release/, checked by its SHA-256)
switch/ssx-android/README.txt           the player README, and settings/*.ini (from release/, checked the same way)
switch/ssx-android/ssx-v0_0_8430.apk    the player's APK, copied whole (the port reads it in place)
```

Cut down from renut_nx_ultimate_installer (the web installer template of the renut ultimate base): the
same page, worker, zip writer and download service worker, without the shader chain or the LZ4 packing (the port
reads the APK as it is and compiles its GL shaders on the console). GPL-3.0, apart from the rest of this repository
(MIT); see [NOTICES.md](NOTICES.md).

## The check

The APK is SSX 0.0.8430 when its central directory has `lib/armeabi/libgame.so` with the CRC-32 and size in
`release/manifest.json` (`ab89e5ff`, 40,585,964 bytes) and `assets/common/config/memory.cfg`. That reads ~1.3 MB of
the 1.3 GB file: no hashing of the whole APK. The page checks when the player chooses the file, and `worker.js`
again before it writes the zip.

## Making a release

1. Build the port and its package: `./build.sh`, then `tools/make_package.sh` (in the repository's root).
2. Fill `release/` from that package (the APK only gives the check's CRC-32 and size; nothing of it is copied):

   ```bash
   python tools/make_release.py --package ../../out/package-<build> --apk <ssx-v0_0_8430.apk>
   ```

`release/` holds the NRO, so it is not committed in this repository (`*.nro` is ignored). Whatever publishes the page
(GitHub Pages, for example) must serve `release/` with it.

## Testing locally

```bash
python serve.py 8642 . --test-files <folder with the APK> --test-out <zip>
```

Open http://127.0.0.1:8642/ for the page. The browser test, http://127.0.0.1:8642/test/browser.html, runs the page's
check and the worker on the served APK with no file dialog, and writes the zip to `--test-out`. Then:

```bash
python test/check_zip.py <zip> <the APK>
```

reads every entry's CRC-32 back and compares the release files and the APK with their sources.

Tested on 2026-11-18 (build 202611182110), Chromium (the Claude desktop app's browser pane): the page's check on the
real APK (supported, 11,240 entries, ~0.1 s) and on a non-zip, a non-SSX APK and an SSX APK with another library
(each refused, Create stays disabled); the browser test made a 1,328,446,009-byte zip in 85 s, and `check_zip.py`
passed (the APK's CRC-32 in the zip equal to the source's). Not tested here: the final download through `sw.js`
(the template's, unchanged).

The zips these tests write hold the APK: game data. Never commit or share them.
