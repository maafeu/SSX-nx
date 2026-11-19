# Third-party notices

The ssx-android web installer is cut down from
renut_nx_ultimate_installer, the web installer template of the renut ultimate base (GPL-3.0), itself built
from kameo-nx-installer, rr6-nx-installer and katamari-nx-installer on
[renut-nx-webinstaller](https://github.com/ChanseyIsTheBest/renut-nx-webinstaller). So the whole installer is GPL-3.0
(see [LICENSE](LICENSE)), apart from the port it packages, whose own code is MIT and lives in the rest of this
repository.

| Component | Where | License |
|---|---|---|
| renut_nx_ultimate_installer and the installers it is built from, by ChanseyIsTheBest ([kameo-nx-installer](https://github.com/ChanseyIsTheBest/kameo-nx-installer), [rr6-nx-installer](https://github.com/ChanseyIsTheBest/rr6-nx-installer), [katamari-nx-installer](https://github.com/ChanseyIsTheBest/katamari-nx-installer), [bejeweled3-nx-installer](https://github.com/ChanseyIsTheBest/bejeweled3-nx-installer)): the page's layout and styles, its flow (`app.js`), the worker's message protocol, the zip writer, the test server and the browser test | `index.html`, `app.js`, `worker.js`, `lib/zip.js` (unchanged), `serve.py` (unchanged), `test/browser.html` | GPL-3.0 |
| [nfsmw-nx-installer](https://github.com/StevensND/nfsmw-nx-installer) by StevensND: the download service worker and the download stream, the zip writer | `sw.js` (unchanged), `lib/zip.js` | GPL-3.0 |
| New for SSX: the APK reader and check, the worker's packaging, the release tool, the zip check | `lib/apk.js`, `worker.js`, `tools/make_release.py`, `test/check_zip.py` | GPL-3.0 |

None of the template's shader chain, Pyodide, WebAssembly tools or LZ4 packing is used or included.

The software in the NRO (`release/ssx-android.nro`: the port, the android32 runtime, libnx, Mesa, libdrm, FFmpeg and
newlib) is listed with its licenses in the port's repository, with its source code. `icon.jpg` is the port's own
forwarder icon. SSX is a trademark of Electronic Arts Inc.; no part of the game is in this installer.
