"""ssx-android web installer: checks a zip the page (or test/browser.html) made.

  python test/check_zip.py <the zip> <the APK it was made from>

Every entry's CRC-32 is read back (zipfile's testzip), the release files must match release/ byte for byte, and the
APK in the zip must be the source APK (size and CRC-32). The zip holds the APK: game data, never commit or share it.
GPL-3.0 (see LICENSE)."""
import json
import os
import sys
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
RELEASE = os.path.join(HERE, '..', 'release')


def crc_of(path):
    crc = 0
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(8 << 20), b''):
            crc = zlib.crc32(block, crc)
    return crc & 0xffffffff


def main():
    zip_path, apk_path = sys.argv[1:3]
    manifest = json.load(open(os.path.join(RELEASE, 'manifest.json'), encoding='utf-8'))
    game = json.load(open(os.path.join(HERE, '..', 'game.json'), encoding='utf-8'))
    root = f"switch/{game['package']}/"
    want = [root + manifest['nro']] + [root + f['path'] for f in manifest['files']] + [root + manifest['apk']['name']]
    problems = []
    with zipfile.ZipFile(zip_path) as z:
        names = z.namelist()
        if names != want:
            problems.append(f'entries {names} != {want}')
        bad = z.testzip()
        if bad:
            problems.append(f'{bad}: CRC-32 does not match its data')
        for rel in [manifest['nro']] + [f['path'] for f in manifest['files']]:
            with open(os.path.join(RELEASE, rel), 'rb') as f:
                if z.read(root + rel) != f.read():
                    problems.append(f'{rel} differs from release/{rel}')
        info = z.getinfo(root + manifest['apk']['name'])
        if info.file_size != os.path.getsize(apk_path) or info.CRC != crc_of(apk_path):
            problems.append('the APK in the zip is not the source APK')
        if any(i.compress_type != zipfile.ZIP_STORED for i in z.infolist()):
            problems.append('an entry is compressed (the writer stores everything)')
    print(f'{zip_path}: {len(names)} entries, {os.path.getsize(zip_path)} bytes')
    for p in problems:
        print('PROBLEM:', p)
    print('OK' if not problems else 'FAILED')
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
