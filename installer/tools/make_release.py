"""ssx-android web installer: release/ from a package (SSX-nx's tools/make_package.sh) and an APK to check against.

  python tools/make_release.py --package <out/package-BUILD> --apk <ssx-v0_0_8430.apk> [--out release]

Copies the package's NRO, README.txt and settings/*.ini to release/, and writes release/manifest.json: their SHA-256s
(the page checks what it downloads) and the APK check, the CRC-32 and size of the game's library from the APK's own
central directory (nothing of the APK is copied: the page reads the player's). GPL-3.0 (see LICENSE)."""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import zipfile

LIBRARY = 'lib/armeabi/libgame.so'
CONFIG = 'assets/common/config/memory.cfg'


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--package', required=True)
    p.add_argument('--apk', required=True)
    p.add_argument('--out', default=os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'release'))
    p.add_argument('--version')
    a = p.parse_args()

    package = os.path.abspath(a.package)
    nro = os.path.join(package, 'SD', 'switch', 'ssx-android', 'ssx-android.nro')
    readme = os.path.join(package, 'README.txt')
    settings = os.path.join(package, 'settings')
    for path in (nro, readme, settings):
        if not os.path.exists(path):
            sys.exit(f'make_release.py: {path} is missing: make the package first (tools/make_package.sh)')
    version = a.version or re.sub(r'^package-', '', os.path.basename(package))

    with zipfile.ZipFile(a.apk) as z:
        try:
            lib = z.getinfo(LIBRARY)
            z.getinfo(CONFIG)
        except KeyError as e:
            sys.exit(f'make_release.py: {a.apk} is not SSX for Android (armeabi): {e}')

    out = os.path.abspath(a.out)
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'settings'))
    shutil.copyfile(nro, os.path.join(out, 'ssx-android.nro'))
    files = [('README.txt', readme)]
    for name in sorted(os.listdir(settings)):
        if name.endswith('.ini'):
            files.append((f'settings/{name}', os.path.join(settings, name)))
    for rel, src in files:
        shutil.copyfile(src, os.path.join(out, rel))

    manifest = {
        'version': version,
        'nro': 'ssx-android.nro',
        'nro_sha256': sha256(os.path.join(out, 'ssx-android.nro')),
        'files': [{'path': rel, 'sha256': sha256(os.path.join(out, rel))} for rel, _ in files],
        'apk': {
            'name': 'ssx-v0_0_8430.apk',
            'edition': 'SSX 0.0.8430 (com.ea.ssx, armeabi)',
            'check': [
                {'path': LIBRARY, 'crc32': f'{lib.CRC:08x}', 'size': lib.file_size},
                {'path': CONFIG},
            ],
        },
    }
    with open(os.path.join(out, 'manifest.json'), 'w', encoding='utf-8', newline='\n') as f:
        json.dump(manifest, f, indent=2)
        f.write('\n')
    print(f'release/ for build {version}: {len(files) + 1} files; the APK check: {LIBRARY} '
          f'CRC-32 {lib.CRC:08x}, {lib.file_size} bytes')


if __name__ == '__main__':
    main()
