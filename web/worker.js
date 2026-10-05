// Runs the engine (tilefish.js / tilefish.wasm) off the page's main thread, so the
// board stays responsive while Tilefish thinks.  Messages: {id, load: LEXICON} and
// {id, run: "commands"}; each is answered with {id, out} or {id, error}, one at a time
// and in the order they came.  {prefetch: LEXICON} downloads a list's files into the
// browser's cache in the background, without touching the engine.
// The page starts this worker as worker.js?v=CODE&d=DATA (web/build.sh), and every file
// loaded here carries the same versions, so a browser never mixes files of two releases.
const Q = new URLSearchParams(self.location.search);
const CODE = Q.get("v") ? "?v=" + Q.get("v") : "";
const DATA = Q.get("d") ? "?d=" + Q.get("d") : "";
importScripts("tilefish.js" + CODE);

// The engine starts compiling as soon as the worker exists, while the files download.
const enginePromise = Tilefish({ locateFile: (file, dir) => dir + file + CODE });
enginePromise.catch(() => {});  // reported by the load that waits for it
let engine = null;
let loaded = "";

// Word lists.  Collins and NWL are copyrighted and not hosted here: like
// get-lexicon.sh, the page fetches them from the MAGPIE project's public data,
// pinned to one commit.  ENABLE and OXENDICT are free and come with the page, compiled
// to .kwg by web/build.sh (several times faster to load than building from the text)
// and gzipped, which this worker unpacks where the browser can (DecompressionStream).
// Each file: [name in the engine's file system, URL, rough size on the wire].
const MAGPIE = "https://raw.githubusercontent.com/jvc56/MAGPIE-DATA/adf29316fcb2d7bd78a832e198c1bdfc112efa89/data/lexica/";
const GZ = typeof DecompressionStream === "function";
const site = (file, raw, gz) => [file, "data/" + file + (GZ ? ".gz" : "") + DATA, GZ ? gz : raw];
const LISTS = {
  CSW24: { main: "CSW24.kwg", files: [["CSW24.kwg", MAGPIE + "CSW24.kwg", 5.97e6], ["CSW24.klv2", MAGPIE + "CSW24.klv2", 3.67e6], ["CSW24.win", "data/CSW24.win" + DATA, 2e3]] },
  NWL23: { main: "NWL23.kwg", files: [["NWL23.kwg", MAGPIE + "NWL23.kwg", 4.72e6], ["NWL23.klv2", MAGPIE + "NWL23.klv2", 3.67e6], ["NWL23.win", "data/NWL23.win" + DATA, 2e3]] },
  ENABLE: { main: "ENABLE.kwg", files: [site("ENABLE.kwg", 3.19e6, 2.36e6), site("ENABLE.klv2", 3.67e6, 2.76e6), ["ENABLE.win", "data/ENABLE.win" + DATA, 2e3]] },
  OXENDICT: { main: "OXENDICT.kwg", files: [site("OXENDICT.kwg", 3.68e6, 2.73e6), site("OXENDICT.klv2", 3.67e6, 2.75e6), ["OXENDICT.win", "data/OXENDICT.win" + DATA, 2e3]] },
};

// One download per URL at a time, shared by a background prefetch and a load that
// asks for the same file.  The browser's Cache Storage keeps every file once fetched.
const downloads = new Map();
function download(url, size) {
  let d = downloads.get(url);
  if (d) return d;
  d = { got: 0, size, listeners: new Set() };
  const tell = () => d.listeners.forEach((f) => f());
  d.bytes = (async () => {
    let cache = null;
    try {
      cache = await caches.open("tilefish-data-1");
      const hit = await cache.match(url);
      if (hit) {
        const b = new Uint8Array(await hit.arrayBuffer());
        d.got = d.size = b.length;
        tell();
        return b;
      }
    } catch (e) {
      cache = null;  // no Cache Storage (private window, file://): just download
    }
    const res = await fetch(url);
    if (!res.ok) throw new Error("could not download " + url.split("/").pop().split("?")[0] + " (" + res.status + ")");
    // Content-Length counts the bytes on the wire, which is what arrives here unless
    // the server compressed the response itself.
    const len = +res.headers.get("content-length") || 0;
    if (len && !res.headers.get("content-encoding")) d.size = len;
    const reader = res.body.getReader();
    const parts = [];
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      parts.push(value);
      d.got += value.length;
      if (d.got > d.size) d.size = d.got;
      tell();
    }
    const bytes = new Uint8Array(d.got);
    let at = 0;
    for (const p of parts) { bytes.set(p, at); at += p.length; }
    d.size = d.got;
    if (cache) {
      try { await cache.put(url, new Response(bytes)); } catch (e) { /* quota: fine */ }
    }
    return bytes;
  })();
  d.bytes.finally(() => downloads.delete(url)).catch(() => {});
  downloads.set(url, d);
  return d;
}

// Gzipped files (a .gz from this site) are unpacked here; anything else, including a
// .gz the server already unpacked on the way, is used as it came.
async function unpack(bytes) {
  if (bytes.length < 2 || bytes[0] !== 0x1f || bytes[1] !== 0x8b) return bytes;
  const stream = new Blob([bytes]).stream().pipeThrough(new DecompressionStream("gzip"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

const run = (cmd) => engine.ccall("tf_run", "string", ["string"], [cmd]);
const rm = (path) => { try { engine.FS.unlink(path); } catch (e) { /* not there */ } };

// Progress for the page: the share of the bar done, the stage, and for downloads the
// bytes so far.  Stages that run inside the engine say how long they usually take, so
// the page can keep the bar moving while the engine works.
async function load(name, id) {
  const list = LISTS[name];
  if (!list) throw new Error("unknown word list " + name);
  const tell = (msg) => postMessage(Object.assign({ id }, msg));
  if (loaded === name) {
    engine = engine || (await enginePromise);
    return "ready";
  }
  const ds = list.files.map(([, url, size]) => download(url, size));
  const report = () => {
    let got = 0, size = 0;
    for (const d of ds) { got += d.got; size += d.size; }
    tell({ progress: 0.02 + 0.68 * Math.min(1, got / size), stage: "download", got, size });
  };
  report();
  for (const d of ds) d.listeners.add(report);
  let blobs;
  try {
    blobs = await Promise.all(ds.map((d) => d.bytes));
  } finally {
    for (const d of ds) d.listeners.delete(report);
  }
  if (!engine) {
    tell({ progress: 0.7, stage: "engine" });
    engine = await enginePromise;
  }
  const files = await Promise.all(list.files.map(([file], i) => unpack(blobs[i]).then((b) => [file, b])));
  try { engine.FS.mkdir("/data"); } catch (e) { /* exists */ }
  const at = (file) => "/data/" + file;
  // The word list first, on its own (leave values and win model of an earlier load of
  // this list must not be picked up with it), then the leave values, then the win model.
  // From here the engine no longer holds the old list, so if a step below fails, asking
  // for the old list again must load it, not answer "ready".
  loaded = "";
  for (const [file] of files) rm(at(file));
  const [main, leaves, win] = files;
  engine.FS.writeFile(at(main[0]), main[1]);
  tell({ progress: 0.72, stage: "words", ms: 250 + main[1].length / 6000, next: 0.88 });
  await new Promise((r) => setTimeout(r, 0));  // let that message go out first
  const out = run("lexicon " + at(main[0]));
  if (!/words/.test(out)) throw new Error(out.trim() || "the word list did not load");
  engine.FS.writeFile(at(leaves[0]), leaves[1]);
  tell({ progress: 0.88, stage: "leaves", ms: 150 + leaves[1].length / 9000, next: 0.98 });
  await new Promise((r) => setTimeout(r, 0));
  const lv = run("leaves " + at(leaves[0]));
  if (!/leave values/.test(lv)) throw new Error(lv.trim() || "the leave values did not load");
  engine.FS.writeFile(at(win[0]), win[1]);
  const wm = run("win " + at(win[0]));
  if (!/win model/.test(wm)) throw new Error(wm.trim() || "the win model did not load");
  tell({ progress: 0.99, stage: "ready" });
  loaded = name;
  return out + lv + wm;
}

async function handle(data) {
  const { id } = data;
  try {
    if (!data.load) engine = engine || (await enginePromise);
    if (data.load) {
      postMessage({ id, out: await load(data.load, id) });
    } else if (data.write) {
      // A file for the engine to read (an imported game record), in its memory file system.
      engine.FS.writeFile(data.write.path, data.write.text);
      postMessage({ id, out: "written" });
    } else {
      postMessage({ id, out: run(data.run) });
    }
  } catch (err) {
    postMessage({ id, error: String(err && err.message ? err.message : err) });
  }
}
// A load waits for its downloads, so without a queue a second message could start while
// the first is still under way, and two loads could finish in either order.  A prefetch
// only fills the cache, so it runs at once, beside the queue.
let queue = Promise.resolve();
onmessage = (e) => {
  const data = e.data;
  if (data.prefetch) {
    const list = LISTS[data.prefetch];
    if (list) for (const [, url, size] of list.files) download(url, size).bytes.catch(() => {});
    return;
  }
  queue = queue.then(() => handle(data));
};
