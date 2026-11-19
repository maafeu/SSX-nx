// ssx-android web installer: makes the zip, off the page's thread. The page (app.js) or the browser test
// (test/browser.html) sends { apk: { name, size, file | url }, port }; the zip goes out through `port`, one chunk per
// "pull" (sw.js serves it as a download; the test writes it to the server), so memory stays bounded for a 1.3 GB APK.
//   switch/<package>/<nro>              the launcher (release/: checked by its SHA-256)
//   switch/<package>/README.txt         and settings/*.ini (release/, checked the same way)
//   switch/<package>/<apk name>         the player's APK, copied whole (the port reads it in place)
// The message protocol and the download flow are renut_nx_ultimate_installer's (GPL-3.0); this file is new. GPL-3.0.
import { checkApk, chunks, listZip, reader } from './lib/apk.js';
import { ZipWriter } from './lib/zip.js';

const post = (message) => self.postMessage(message);
const log = (text) => post({ type: 'log', text });

async function sha256(bytes) {
  const d = new Uint8Array(await crypto.subtle.digest('SHA-256', bytes));
  return Array.from(d, (b) => b.toString(16).padStart(2, '0')).join('');
}

async function fetchRelease(path, hash) {
  const r = await fetch(`./release/${path}`, { cache: 'no-store' });
  if (!r.ok) throw new Error(`release/${path}: HTTP ${r.status}`);
  const bytes = new Uint8Array(await r.arrayBuffer());
  if ((await sha256(bytes)) !== hash) throw new Error(`release/${path} is not the file this release names (SHA-256 differs)`);
  return bytes;
}

// The zip's bytes to the port: batched into ~1 MB chunks, each sent when the other side asks ("pull").
function portSink(port) {
  let credits = 0, wake = null, cancelled = false, parts = [], partBytes = 0;
  port.onmessage = ({ data }) => {
    if (data.type === 'pull') credits++;
    else if (data.type === 'cancel') cancelled = true;
    if (wake) { const w = wake; wake = null; w(); }
  };
  const send = async (bytes) => {
    while (!credits && !cancelled) await new Promise((resolve) => (wake = resolve));
    if (cancelled) throw new Error('the download was cancelled');
    credits--;
    port.postMessage({ type: 'chunk', buffer: bytes.buffer }, [bytes.buffer]);
  };
  const flush = async () => {
    if (!partBytes) return;
    const out = new Uint8Array(partBytes);
    let at = 0;
    for (const p of parts) { out.set(p, at); at += p.length; }
    parts = [];
    partBytes = 0;
    await send(out);
  };
  return {
    async write(bytes) {
      parts.push(bytes);
      partBytes += bytes.length;
      if (partBytes >= 1 << 20) await flush();
    },
    async close() {
      await flush();
      port.postMessage({ type: 'end' });
    },
    fail(message) {
      port.postMessage({ type: 'error', message });
    },
  };
}

self.onmessage = async ({ data }) => {
  const { apk, port } = data;
  const sink = portSink(port);
  const began = performance.now();
  try {
    const [game, manifest] = await Promise.all(
      ['./game.json', './release/manifest.json'].map(async (u) => (await fetch(u, { cache: 'no-store' })).json()));
    const read = reader(apk);
    const why = checkApk(await listZip(read, apk.size), manifest.apk);  // again: this is what writes the zip
    if (why) throw new Error(why);
    log('Downloading the Switch program and its files...');
    const nro = await fetchRelease(manifest.nro, manifest.nro_sha256);
    const files = [];
    for (const f of manifest.files) files.push({ path: f.path, bytes: await fetchRelease(f.path, f.sha256) });
    const root = `switch/${game.package}`;
    const total = nro.length + files.reduce((n, f) => n + f.bytes.length, 0) + apk.size;
    let done = 0, shown = -1;
    const counted = (n) => {
      done += n;
      const f = Math.floor((done / total) * 1000);
      if (f !== shown) { shown = f; post({ type: 'progress', fraction: done / total }); }
    };
    post({ type: 'start', estimate: total });
    const zip = new ZipWriter(sink);
    await zip.addBytes(`${root}/${manifest.nro}`, nro);
    counted(nro.length);
    for (const f of files) {
      await zip.addBytes(`${root}/${f.path}`, f.bytes);
      counted(f.bytes.length);
    }
    log(`Adding your APK (${(apk.size / (1 << 30)).toFixed(2)} GB, copied as it is)...`);
    await zip.addFile(`${root}/${manifest.apk.name}`, apk.size, chunks(read, apk.size), (c) => counted(c.length));
    const size = await zip.finish();
    post({ type: 'done', size, seconds: Math.round((performance.now() - began) / 1000) });
  } catch (error) {
    sink.fail(error.message);
    post({ type: 'error', message: error.message });
  }
};
