// ssx-android web installer: the page. The player chooses the APK of their own SSX; lib/apk.js checks it from its
// central directory (the game library's CRC-32 and size against release/manifest.json); worker.js makes the zip and
// sw.js serves it as a download while it is written.
// From renut_nx_ultimate_installer's app.js (rr6-nx-installer's and kameo-nx-installer's; bejeweled3-nx-installer's,
// ChanseyIsTheBest; nfsmw-nx-installer's download flow, StevensND), cut down to one file and no shaders. GPL-3.0.
import { checkApk, listZip, reader } from './lib/apk.js';

const $ = (id) => document.getElementById(id);
const apkInput = $('apk'), chosen = $('chosen'), detected = $('detected');
const create = $('create'), bar = $('bar'), logBox = $('log');
const manifest = fetch('./release/manifest.json', { cache: 'no-store' }).then((r) => r.json());
const game = fetch('./game.json', { cache: 'no-store' }).then((r) => r.json());
let apk = null;  // { name, size, file } once checked
let busy = false;
let downloadFrame = null;

// the texts of game.json (trusted: the port's own file, as the page's HTML is)
game.then((g) => {
  document.title = `${g.title} for Switch: web installer`;
  for (const el of document.querySelectorAll('[data-game]')) el.textContent = g[el.dataset.game];
  for (const el of document.querySelectorAll('[data-text]')) el.innerHTML = g.texts?.[el.dataset.text] || '';
  create.textContent = `Create ${g.package}.zip`;
  manifest.then((m) => { $('version').textContent = `build ${m.version}`; });
});

function log(text, error = false) {
  const line = document.createElement('div');
  line.textContent = text;
  if (error) line.className = 'error';
  logBox.appendChild(line);
  logBox.scrollTop = logBox.scrollHeight;
}

function formatSize(bytes) {
  return bytes >= 1 << 30 ? `${(bytes / (1 << 30)).toFixed(2)} GB` : `${(bytes / (1 << 20)).toFixed(0)} MB`;
}

function show(state, text) {
  detected.hidden = false;
  detected.className = `detected ${state}`;
  detected.textContent = text;
}

function updateCreate() {
  create.disabled = busy || !apk;
}

apkInput.addEventListener('change', async (event) => {
  const [file] = event.target.files;
  event.target.value = '';
  if (!file) return;
  apk = null;
  updateCreate();
  chosen.textContent = `${file.name}, ${formatSize(file.size)}`;
  show('', 'Checking the APK...');
  try {
    const m = await manifest;
    const why = checkApk(await listZip(reader({ file }), file.size), m.apk);
    if (why) {
      show('bad', why);
      return;
    }
    show('good', `${m.apk.edition}: supported.`);
    apk = { name: file.name, size: file.size, file };
    updateCreate();
  } catch (error) {
    show('bad', `Could not read this file: ${error.message}`);
  }
});

// The zip is served by sw.js as a normal browser download, streamed while it is being made.
async function downloadHelper() {
  if (!('serviceWorker' in navigator)) throw new Error('this browser cannot stream a download (no service workers)');
  await navigator.serviceWorker.register('./sw.js', { updateViaCache: 'none' });
  const registration = await navigator.serviceWorker.ready;
  if (!navigator.serviceWorker.controller) {
    const claimed = new Promise((resolve) => navigator.serviceWorker.addEventListener('controllerchange', resolve, { once: true }));
    registration.active.postMessage({ type: 'claim' });
    await claimed;
  }
  return navigator.serviceWorker.controller;
}
const helper = downloadHelper();
helper.catch(() => {});

create.addEventListener('click', async () => {
  if (!apk || busy) return;
  let controller;
  try {
    controller = await helper;
  } catch (error) {
    log(`Error: ${error.message}`, true);
    return;
  }
  const zipName = `${(await game).package}.zip`;
  busy = true;
  updateCreate();
  bar.hidden = false;
  bar.value = 0;
  logBox.textContent = '';
  const channel = new MessageChannel();
  const keepAlive = setInterval(() => controller.postMessage({ type: 'ping' }), 10000);
  const worker = new Worker('./worker.js', { type: 'module' });
  const finish = () => {
    clearInterval(keepAlive);
    worker.terminate();
    busy = false;
    updateCreate();
  };
  worker.onmessage = async ({ data }) => {
    if (data.type === 'log') {
      log(data.text);
    } else if (data.type === 'progress') {
      bar.value = data.fraction;
    } else if (data.type === 'start') {
      const id = `${Date.now()}-${Math.random().toString(36).slice(2)}`;
      const ack = new MessageChannel();
      const registered = new Promise((resolve) => (ack.port1.onmessage = resolve));
      controller.postMessage({ type: 'download', id, filename: zipName, size: null }, [channel.port2, ack.port2]);
      await registered;
      // a hidden frame opens the download: its request is a navigation, which always reaches the service worker
      downloadFrame?.remove();
      downloadFrame = document.createElement('iframe');
      downloadFrame.hidden = true;
      downloadFrame.src = `./download/${id}/${zipName}`;
      document.body.appendChild(downloadFrame);
      log(`Download started: ${zipName}, about ${formatSize(data.estimate)}. Keep this page open until it finishes.`);
    } else if (data.type === 'done') {
      bar.value = 1;
      log(`Finished in ${data.seconds} s: ${zipName} is ${formatSize(data.size)}.`);
      finish();
    } else if (data.type === 'error') {
      log(`Error: ${data.message}`, true);
      finish();
    }
  };
  worker.onerror = (event) => {
    log(`Error: ${event.message}`, true);
    finish();
  };
  worker.postMessage({ apk, port: channel.port1 }, [channel.port1]);
});
