// Runs the engine (tilefish.js / tilefish.wasm) off the page's main thread, so the
// board stays responsive while Tilefish thinks.  Messages: {id, load: LEXICON} and
// {id, run: "commands"}; each is answered with {id, out} or {id, error}.
importScripts("tilefish.js");

// Word lists.  Collins and NWL are copyrighted and not hosted here: like
// get-lexicon.sh, the page fetches them from the MAGPIE project's public data,
// pinned to one commit.  ENABLE and OXENDICT are free and come with the page.
const MAGPIE = "https://raw.githubusercontent.com/jvc56/MAGPIE-DATA/adf29316fcb2d7bd78a832e198c1bdfc112efa89/data/lexica/";
const LISTS = {
  CSW24: { main: "CSW24.kwg", files: [["CSW24.kwg", MAGPIE + "CSW24.kwg"], ["CSW24.klv2", MAGPIE + "CSW24.klv2"], ["CSW24.win", "data/CSW24.win"]] },
  NWL23: { main: "NWL23.kwg", files: [["NWL23.kwg", MAGPIE + "NWL23.kwg"], ["NWL23.klv2", MAGPIE + "NWL23.klv2"], ["NWL23.win", "data/NWL23.win"]] },
  ENABLE: { main: "ENABLE.txt", files: [["ENABLE.txt", "data/ENABLE.txt"], ["ENABLE.klv2", "data/ENABLE.klv2"], ["ENABLE.win", "data/ENABLE.win"]] },
  OXENDICT: { main: "OXENDICT.txt", files: [["OXENDICT.txt", "data/OXENDICT.txt"], ["OXENDICT.klv2", "data/OXENDICT.klv2"], ["OXENDICT.win", "data/OXENDICT.win"]] },
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
  if (!engine) engine = await Tilefish();
  if (loaded === name) return "ready";
  // Rough sizes, for a progress bar that moves before the first byte arrives.
  let done = 0;
  const sizes = { CSW24: 9.7e6, NWL23: 8.4e6, ENABLE: 5.6e6, OXENDICT: 5.8e6 };
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

onmessage = async (e) => {
  const { id } = e.data;
  try {
    if (e.data.load) {
      postMessage({ id, out: await load(e.data.load, id) });
    } else if (e.data.write) {
      // A file for the engine to read (an imported game record), in its memory file system.
      engine.FS.writeFile(e.data.write.path, e.data.write.text);
      postMessage({ id, out: "written" });
    } else {
      postMessage({ id, out: engine.ccall("tf_run", "string", ["string"], [e.data.run]) });
    }
  } catch (err) {
    postMessage({ id, error: String(err && err.message ? err.message : err) });
  }
};
