// Runs the engine (tilefish.js / tilefish.wasm) off the page's main thread, so the
// board stays responsive while Tilefish thinks.  Messages: {id, load: LEXICON} and
// {id, run: "commands"}; each is answered with {id, out} or {id, error}, one at a time
// and in the order they came.
// The page starts this worker as worker.js?v=CODE&d=DATA (web/build.sh), and every file
// loaded here carries the same versions, so a browser never mixes files of two releases.
const Q = new URLSearchParams(self.location.search);
const CODE = Q.get("v") ? "?v=" + Q.get("v") : "";
const DATA = Q.get("d") ? "?d=" + Q.get("d") : "";
importScripts("tilefish.js" + CODE);

// Word lists.  Collins and NWL are copyrighted and not hosted here: like
// get-lexicon.sh, the page fetches them from the MAGPIE project's public data,
// pinned to one commit.  ENABLE and OXENDICT are free and come with the page, compiled
// to .kwg by web/build.sh: that loads several times faster than building from the text.
const MAGPIE = "https://raw.githubusercontent.com/jvc56/MAGPIE-DATA/adf29316fcb2d7bd78a832e198c1bdfc112efa89/data/lexica/";
const LISTS = {
  CSW24: { main: "CSW24.kwg", files: [["CSW24.kwg", MAGPIE + "CSW24.kwg"], ["CSW24.klv2", MAGPIE + "CSW24.klv2"], ["CSW24.win", "data/CSW24.win" + DATA]] },
  NWL23: { main: "NWL23.kwg", files: [["NWL23.kwg", MAGPIE + "NWL23.kwg"], ["NWL23.klv2", MAGPIE + "NWL23.klv2"], ["NWL23.win", "data/NWL23.win" + DATA]] },
  ENABLE: { main: "ENABLE.kwg", files: [["ENABLE.kwg", "data/ENABLE.kwg" + DATA], ["ENABLE.klv2", "data/ENABLE.klv2" + DATA], ["ENABLE.win", "data/ENABLE.win" + DATA]] },
  OXENDICT: { main: "OXENDICT.kwg", files: [["OXENDICT.kwg", "data/OXENDICT.kwg" + DATA], ["OXENDICT.klv2", "data/OXENDICT.klv2" + DATA], ["OXENDICT.win", "data/OXENDICT.win" + DATA]] },
};

let engine = null;
let loaded = "";

async function cachedBytes(url, progress) {
  let cache = null;
  try {
    cache = await caches.open("tilefish-data-1");
    const hit = await cache.match(url);
    if (hit) return new Uint8Array(await hit.arrayBuffer());
  } catch (e) {
    cache = null;  // no Cache Storage (private window, file://): just download
  }
  const res = await fetch(url);
  if (!res.ok) throw new Error("could not download " + url.split("/").pop() + " (" + res.status + ")");
  const total = +res.headers.get("content-length") || 0;
  const reader = res.body.getReader();
  const parts = [];
  let got = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    parts.push(value);
    got += value.length;
    progress(value.length, total);
  }
  const bytes = new Uint8Array(got);
  let at = 0;
  for (const p of parts) { bytes.set(p, at); at += p.length; }
  if (cache) {
    try { await cache.put(url, new Response(bytes)); } catch (e) { /* quota: fine */ }
  }
  return bytes;
}

async function load(name, id) {
  const list = LISTS[name];
  if (!list) throw new Error("unknown word list " + name);
  if (!engine) engine = await Tilefish({ locateFile: (file, dir) => dir + file + CODE });
  if (loaded === name) return "ready";
  // Rough sizes, for a progress bar that moves before the first byte arrives.
  let done = 0;
  const sizes = { CSW24: 9.7e6, NWL23: 8.4e6, ENABLE: 6.9e6, OXENDICT: 7.4e6 };
  const report = (n, total) => {
    done += n;
    postMessage({ id, progress: Math.min(0.97, done / sizes[name]) });
  };
  try { engine.FS.mkdir("/data"); } catch (e) { /* exists */ }
  const blobs = await Promise.all(list.files.map(([file, url]) => cachedBytes(url, report).then((b) => [file, b])));
  for (const [file, bytes] of blobs) engine.FS.writeFile("/data/" + file, bytes);
  const out = engine.ccall("tf_run", "string", ["string"], ["lexicon /data/" + list.main]);
  if (!/words/.test(out)) throw new Error(out.trim() || "the word list did not load");
  loaded = name;
  return out;
}

async function handle(data) {
  const { id } = data;
  try {
    if (data.load) {
      postMessage({ id, out: await load(data.load, id) });
    } else if (data.write) {
      // A file for the engine to read (an imported game record), in its memory file system.
      engine.FS.writeFile(data.write.path, data.write.text);
      postMessage({ id, out: "written" });
    } else {
      postMessage({ id, out: engine.ccall("tf_run", "string", ["string"], [data.run]) });
    }
  } catch (err) {
    postMessage({ id, error: String(err && err.message ? err.message : err) });
  }
}
// A load waits for its downloads, so without a queue a second message could start while
// the first is still under way, and two loads could finish in either order.
let queue = Promise.resolve();
onmessage = (e) => {
  queue = queue.then(() => handle(e.data));
};
