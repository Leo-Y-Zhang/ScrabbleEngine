// Runs the engine (tilefish.js / tilefish.wasm) off the page's main thread, so the
// board stays responsive while Tilefish thinks.  Messages: {id, load: LEXICON} and
// {id, run: "commands"}; each is answered with {id, out} or {id, error}, one at a time
// and in the order they came.  {prefetch: LEXICON} downloads a list's files into the
// browser's cache in the background, without touching the engine; {id, onDevice: true}
// answers with the lists already in that cache; {id, own: {lex, name, bytes}} keeps the
// player's own copy of a tournament list on this device.
// The page starts this worker as worker.js?v=CODE&d=DATA (web/build.sh), and every file
// loaded here carries the same versions, so a browser never mixes files of two releases.
const Q = new URLSearchParams(self.location.search);
const CODE = Q.get("v") ? "?v=" + Q.get("v") : "";
const DATA = Q.get("d") ? "?d=" + Q.get("d") : "";
// The multi-core engine starts its search threads as workers with this file's own URL
// (Emscripten uses the URL of the script that loaded it); such a worker is handed straight
// to tilefish-mt.js, which runs the thread.  Everything below is the engine's own worker.
if ((self.name || "").startsWith("em-pthread")) {
  importScripts("tilefish-mt.js" + CODE);
} else {
  // The engine thinks on several cores where the page is cross-origin isolated (coi-sw.js
  // adds the headers) and SharedArrayBuffer exists: tilefish-mt.js, one search thread per
  // core up to 8.  Otherwise, or if that build fails to start, the one-core tilefish.js.
  const CORES = Math.max(1, Math.min(8, (self.navigator && navigator.hardwareConcurrency) || 1));
  const MT = self.crossOriginIsolated === true && typeof SharedArrayBuffer === "function" && CORES > 1;
  let threads = 1;
  function boot(mt) {
    importScripts((mt ? "tilefish-mt.js" : "tilefish.js") + CODE);
    return (mt ? self.TilefishMT : self.Tilefish)({ locateFile: (file, dir) => dir + file + CODE }).then((m) => {
      threads = mt ? CORES : 1;
      m.ccall("tf_run", "string", ["string"], ["threads " + threads]);
      return m;
    });
  }
  // The engine starts compiling as soon as the worker exists, while the files download.
  const enginePromise = MT ? boot(true).catch(() => boot(false)) : boot(false);
  enginePromise.catch(() => {});  // reported by the load that waits for it
  let engine = null;
  let loaded = "";

  // Word lists.  CEL, ENABLE and OXENDICT are free and come with the page, compiled to .kwg
  // by web/build.sh (several times faster to load than building from the text) and gzipped,
  // which this worker unpacks where the browser can (DecompressionStream).  Collins and NWL
  // are their publishers' copyright, so Tilefish neither hosts nor fetches them: a player who
  // has a copy loads it from their own device ({own}), and it is kept in this browser's
  // cache under an address of this site that serves nothing (own/...).  Their leave values
  // and win models are Tilefish's own, learned by self-play, and come with the page.
  // Each file: [name in the engine's file system, URL, rough size on the wire].
  const GZ = typeof DecompressionStream === "function";
  const site = (file, raw, gz) => [file, "data/" + file + (GZ ? ".gz" : "") + DATA, GZ ? gz : raw];
  const ownUrl = (lex, ext) => new URL("own/" + lex + ext, self.location.href).href.split("?")[0];
  const LISTS = {
    CSW24: { own: true, main: "CSW24.kwg", files: [["CSW24.kwg", ownUrl("CSW24", ".kwg"), 6e6], site("CSW24.klv2", 3.67e6, 2.75e6), ["CSW24.win", "data/CSW24.win" + DATA, 2e3]] },
    NWL23: { own: true, main: "NWL23.kwg", files: [["NWL23.kwg", ownUrl("NWL23", ".kwg"), 5e6], site("NWL23.klv2", 3.67e6, 2.75e6), ["NWL23.win", "data/NWL23.win" + DATA, 2e3]] },
    CEL: { main: "CEL.kwg", files: [site("CEL.kwg", 1.45e6, 1.06e6), site("CEL.klv2", 3.67e6, 2.71e6), ["CEL.win", "data/CEL.win" + DATA, 2e3]] },
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
      const file = url.split("/").pop().split("?")[0];
      if (url.includes("/own/")) throw new Error("your copy of this word list is not on this device: load it on the start card");
      let res;
      try {
        res = await fetch(url);
      } catch (e) {
        throw new Error(self.navigator && navigator.onLine === false ? "no connection, and this word list is not kept on this device yet"
          : "could not download " + file + "; check the connection");
      }
      if (!res.ok) throw new Error("could not download " + file + " (" + res.status + ")");
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
        try {
          await cache.put(url, new Response(bytes));
          // A file of this site carries the version of the data (?d=): older versions of
          // the same file are no longer needed.
          const u = new URL(url, self.location.href);
          if (u.search) {
            for (const k of await cache.keys()) {
              const o = new URL(k.url);
              if (o.origin === u.origin && o.pathname === u.pathname && o.search !== u.search) await cache.delete(k);
            }
          }
        } catch (e) { /* quota: fine */ }
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

  // The player's own copy of a tournament list: a text file (one word per line) or a .kwg.
  // A text list is compiled to .kwg here once, so it loads fast next time; only the .kwg is
  // kept.  Nothing leaves this device.
  async function keepOwn(lex, name, bytes) {
    if (!LISTS[lex] || !LISTS[lex].own) throw new Error("not a list you load yourself");
    engine = engine || (await enginePromise);
    try { engine.FS.mkdir("/own"); } catch (e) { /* exists */ }
    const kwg = /\.kwg$/i.test(name);
    const src = "/own/" + lex + (kwg ? ".kwg" : ".txt"), out = "/own/" + lex + ".kwg";
    engine.FS.writeFile(src, bytes);
    // Loading it for a check replaces the engine's list: the next load must load again.
    loaded = "";
    const said = run("lexicon " + src);
    const m = said.match(/: (\d+) words/);
    if (!m || +m[1] < 1000) {
      rm(src);
      throw new Error("this file is not a word list Tilefish can read (" + (said.trim().split("\n").pop() || "no words") + ")");
    }
    if (!kwg) {
      const sv = run("savekwg " + out);
      if (!/saved/.test(sv)) throw new Error(sv.trim() || "the list could not be compiled");
    }
    const data = engine.FS.readFile(out);
    rm(src);
    rm(out);
    const cache = await caches.open("tilefish-data-1");
    await cache.put(ownUrl(lex, ".kwg"), new Response(data));
    for (const [, url, size] of LISTS[lex].files) if (!url.includes("/own/")) download(url, size).bytes.catch(() => {});
    return +m[1];
  }
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
    tell({ progress: 0.72, stage: "words", ms: 150 + main[1].length / 9000, next: 0.9 });
    await new Promise((r) => setTimeout(r, 0));  // let that message go out first
    const out = run("lexicon " + at(main[0]));
    if (!/words/.test(out)) throw new Error(out.trim() || "the word list did not load");
    engine.FS.writeFile(at(leaves[0]), leaves[1]);
    tell({ progress: 0.9, stage: "leaves", ms: 60 + leaves[1].length / 40000, next: 0.98 });
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
        const out = await load(data.load, id);
        postMessage({ id, out, threads });
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
  // Earlier versions of the page downloaded Collins and NWL from a third party's copy into
  // this cache; those copies are removed (a player's own copy is kept under own/).
  (async () => {
    try {
      const cache = await caches.open("tilefish-data-1");
      for (const k of await cache.keys()) if (/^https:\/\/raw\.githubusercontent\.com\//.test(k.url)) await cache.delete(k);
    } catch (e) { /* no Cache Storage */ }
  })();
  // {id, onDevice: true}: the word lists whose files are all kept by this browser, so they
  // load without a connection.  Answered at once, beside the queue.
  async function onDevice() {
    const out = [];
    try {
      const cache = await caches.open("tilefish-data-1");
      for (const name of Object.keys(LISTS)) {
        // A list the player loads is on this device once their copy is (Tilefish's own data
        // for it comes from this site, and is fetched with it).
        const files = LISTS[name].own ? LISTS[name].files.filter(([, url]) => url.includes("/own/")) : LISTS[name].files;
        const hits = await Promise.all(files.map(([, url]) => cache.match(url)));
        if (hits.every(Boolean)) out.push(name);
      }
    } catch (e) { /* no Cache Storage: nothing is kept */ }
    return out;
  }
  onmessage = (e) => {
    const data = e.data;
    if (data.onDevice) {
      onDevice().then((out) => postMessage({ id: data.id, out }));
      return;
    }
    if (data.prefetch) {
      const list = LISTS[data.prefetch];
      if (list) for (const [, url, size] of list.files) if (!url.includes("/own/")) download(url, size).bytes.catch(() => {});
      return;
    }
    if (data.own) {
      queue = queue.then(() => keepOwn(data.own.lex, data.own.name, new Uint8Array(data.own.bytes))
        .then((words) => postMessage({ id: data.id, out: words }), (err) => postMessage({ id: data.id, error: String(err && err.message ? err.message : err) })));
      return;
    }
    queue = queue.then(() => handle(data));
  };
}
