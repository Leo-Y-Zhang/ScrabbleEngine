// Cross-origin isolation for GitHub Pages, which cannot send the headers itself: this
// service worker adds them to every response, so the page may use SharedArrayBuffer and
// the engine can think on several cores (tilefish-mt.js).  Where it does not take
// effect, the page runs the one-core engine as before (worker.js decides).
//
// It also lets the site open without a connection.  Each file of the page is kept in
// Cache Storage as it is fetched: the page itself comes from the network whenever it
// answers within a few seconds (so a new release is seen at once) and from the copy
// otherwise; files with a version in their address (web/build.sh adds ?v= and ?d=) never
// change, so the copy is used, and only the latest version of each is kept; the rest
// (fonts, icons) is answered from the copy and refreshed in the background.  The word
// lists are not kept here: worker.js keeps those itself.
const APP = "tilefish-app-1";
const SCOPE = new URL("./", self.location).href;
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (e) => e.waitUntil(self.clients.claim()));

function isolate(res) {
  if (res.status === 0 || res.type === "opaque") return res;
  const h = new Headers(res.headers);
  h.set("Cross-Origin-Embedder-Policy", "require-corp");
  h.set("Cross-Origin-Opener-Policy", "same-origin");
  if (!h.has("Cross-Origin-Resource-Policy")) h.set("Cross-Origin-Resource-Policy", "cross-origin");
  return new Response(res.body, { status: res.status, statusText: res.statusText, headers: h });
}
// Cache Storage may be missing or refuse (a private window, a full disk): then the page
// simply goes to the network, as it did before.
async function cache() {
  try { return await caches.open(APP); } catch (e) { return null; }
}
async function keep(c, key, res) {
  if (!c || !res.ok) return;
  try { await c.put(key, res); } catch (e) { /* quota: fine */ }
}

// The page: from the network, or the copy if the network fails or is slow.
function page(e) {
  const c = cache();
  const net = fetch(e.request).then(isolate);
  // (registered first, so the copy is taken before the page starts reading the answer)
  e.waitUntil(net.then((out) => {
    if (!out.ok || !/text\/html/.test(out.headers.get("content-type") || "")) return;
    const copy = out.clone();
    return c.then((ch) => keep(ch, SCOPE, copy));
  }).catch(() => {}));
  const saved = c.then((ch) => (ch ? ch.match(SCOPE) : undefined)).catch(() => undefined);
  return new Promise((resolve, reject) => {
    let done = false;
    const use = (r) => { if (!done && r) { done = true; resolve(r); } };
    const timer = setTimeout(() => saved.then(use), 4000);
    net.then((r) => { clearTimeout(timer); use(r); }, (err) => {
      clearTimeout(timer);
      saved.then((r) => { if (r) use(r); else if (!done) { done = true; reject(err); } });
    });
  });
}

// A versioned file: the copy if there is one, else the network (keeping the answer and
// dropping older versions of the same file).
async function versioned(e, url) {
  const c = await cache();
  const hit = c && (await c.match(e.request).catch(() => undefined));
  if (hit) return hit;
  const res = await fetch(e.request);
  const out = isolate(res);
  if (c && res.ok) {
    const copy = out.clone();
    e.waitUntil((async () => {
      await keep(c, e.request, copy);
      for (const k of await c.keys()) {
        const u = new URL(k.url);
        if (u.pathname === url.pathname && u.search !== url.search) await c.delete(k);
      }
    })().catch(() => {}));
  }
  return out;
}

// Anything else of the site's own: the copy at once if there is one, refreshed behind it.
async function shared(e) {
  const c = await cache();
  const hit = c && (await c.match(e.request).catch(() => undefined));
  const net = fetch(e.request).then((res) => {
    const out = isolate(res);
    if (c && res.ok) { const copy = out.clone(); e.waitUntil(keep(c, e.request, copy)); }
    return out;
  });
  if (hit) { e.waitUntil(net.catch(() => {})); return hit; }
  return net;
}

self.addEventListener("fetch", (e) => {
  const req = e.request;
  if (req.cache === "only-if-cached" && req.mode !== "same-origin") return;
  const url = new URL(req.url);
  const own = req.method === "GET" && url.origin === self.location.origin && url.href.startsWith(SCOPE) && !url.pathname.includes("/data/");
  if (!own) e.respondWith(fetch(req).then(isolate));
  else if (req.mode === "navigate" && /\/(index\.html)?$/.test(url.pathname)) e.respondWith(page(e));
  else if (/[?&][vd]=/.test(url.search)) e.respondWith(versioned(e, url));
  else e.respondWith(shared(e));
});
