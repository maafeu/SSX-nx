// ssx-android web installer: the player's APK, read in place. An APK is a zip; its central directory (the last
// ~1.3 MB of SSX's 1.3 GB file) lists every entry with its CRC-32 and size, so the version can be checked without
// reading or unpacking anything else. The page and the worker both use this. GPL-3.0 (see LICENSE).

// read(offset, length) -> Promise<Uint8Array>; size: the file's size.
// Returns Map(name -> { name, method, crc, csize, usize }), or null when this is not a zip.
export async function listZip(read, size) {
  if (size < 22) return null;
  const tail = Math.min(size, 22 + 0xffff + 20);  // the end record, its longest comment, a zip64 locator before it
  const end = await read(size - tail, tail);
  const v = new DataView(end.buffer, end.byteOffset, end.byteLength);
  let eocd = -1;
  for (let i = end.length - 22; i >= 0; i--) {
    if (v.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  }
  if (eocd < 0) return null;
  let count = v.getUint16(eocd + 10, true);
  let cdSize = v.getUint32(eocd + 12, true);
  let cdOffset = v.getUint32(eocd + 16, true);
  if (count === 0xffff || cdSize === 0xffffffff || cdOffset === 0xffffffff) {  // zip64
    const loc = eocd - 20;
    if (loc < 0 || v.getUint32(loc, true) !== 0x07064b50) return null;
    const at = Number(v.getBigUint64(loc + 8, true));
    const z = await read(at, 56);
    const zv = new DataView(z.buffer, z.byteOffset, z.byteLength);
    if (z.length < 56 || zv.getUint32(0, true) !== 0x06064b50) return null;
    count = Number(zv.getBigUint64(32, true));
    cdSize = Number(zv.getBigUint64(40, true));
    cdOffset = Number(zv.getBigUint64(48, true));
  }
  if (cdOffset + cdSize > size || cdSize > 64 << 20) return null;
  const cd = await read(cdOffset, cdSize);
  const d = new DataView(cd.buffer, cd.byteOffset, cd.byteLength);
  const decoder = new TextDecoder();
  const entries = new Map();
  let at = 0;
  for (let i = 0; i < count; i++) {
    if (at + 46 > cd.length || d.getUint32(at, true) !== 0x02014b50) return null;
    const nameLength = d.getUint16(at + 28, true);
    const name = decoder.decode(cd.subarray(at + 46, at + 46 + nameLength));
    entries.set(name, {
      name,
      method: d.getUint16(at + 10, true),
      crc: d.getUint32(at + 16, true),
      csize: d.getUint32(at + 20, true),
      usize: d.getUint32(at + 24, true),
    });
    at += 46 + nameLength + d.getUint16(at + 30, true) + d.getUint16(at + 32, true);
  }
  return entries;
}

// want: release/manifest.json's "apk". Returns null when the APK is the supported one, else why not.
export function checkApk(entries, want) {
  if (!entries) return 'This file is not an APK (not a zip archive).';
  for (const c of want.check) {
    const e = entries.get(c.path);
    if (!e) {
      return entries.has('AndroidManifest.xml')
        ? `This APK has no ${c.path}: it is not SSX for Android (armeabi).`
        : 'This file is not an APK (no AndroidManifest.xml).';
    }
    if (c.crc32 && (e.crc !== parseInt(c.crc32, 16) || e.usize !== c.size)) {
      return `This is SSX, but not the version this release supports (${want.edition}): its ${c.path} has CRC-32 ` +
        `${e.crc.toString(16).padStart(8, '0')} and ${e.usize} bytes.`;
    }
  }
  return null;
}

// The whole file, 8 MB at a time.
export async function* chunks(read, size, step = 8 << 20) {
  for (let at = 0; at < size; at += step) yield await read(at, Math.min(step, size - at));
}

// A reader for a File (the page) or a URL served with HTTP range requests (test/browser.html).
export function reader(source) {
  if (source.file) return async (offset, length) => new Uint8Array(await source.file.slice(offset, offset + length).arrayBuffer());
  return async (offset, length) => {
    const r = await fetch(source.url, { headers: { Range: `bytes=${offset}-${offset + length - 1}` } });
    if (r.status !== 206) throw new Error(`${source.url}: HTTP ${r.status} for a range request`);
    const bytes = new Uint8Array(await r.arrayBuffer());
    if (bytes.length !== length) throw new Error(`${source.url}: ${bytes.length} bytes at ${offset}, wanted ${length}`);
    return bytes;
  };
}
