"use strict";
// Tilefish in the browser.  The engine (tilefish.cpp, compiled to WebAssembly) runs in
// worker.js and owns the game: rules, scoring, the bag.  This file keeps the record of
// committed moves with the clocks, which is enough to rebuild the engine's game exactly
// (the seed fixes every draw), so a reload, a crashed worker, a cancelled search or a
// take-back all go through the same replay.

const N = 15;
const COLS = "ABCDEFGHIJKLMNO";
const VALUES = { A: 1, B: 3, C: 3, D: 2, E: 1, F: 4, G: 2, H: 4, I: 1, J: 8, K: 5, L: 1, M: 3, N: 1, O: 1, P: 3, Q: 10, R: 1, S: 1, T: 1, U: 1, V: 4, W: 4, X: 8, Y: 4, Z: 10 };
const LEVEL_TIME = { casual: 0, strong: 3, champion: 10 };
const LEX_NAME = { CSW24: "Collins 2024", NWL23: "NWL 2023", ENABLE: "ENABLE", OXENDICT: "Oxford spelling" };
const BUNDLED = new Set(["ENABLE", "OXENDICT"]);  // free lists that come with the page
const NOTES = {
  CSW24: "Collins Scrabble Words, the World Championship list. About 10 MB, downloaded once and kept by this browser.",
  NWL23: "The North American tournament list. About 8 MB, downloaded once and kept by this browser.",
  ENABLE: "A free public-domain list that comes with Tilefish.",
  OXENDICT: "British English in Oxford spelling (realize, colour), a free list that comes with Tilefish. Built from the English Speller Database: not the Oxford English Dictionary's own list.",
  casual: "Plays at once, from its evaluation of each move, with exact endgames.",
  strong: "Simulates its best candidates for up to 3 seconds a move.",
  champion: "Its full search, up to 10 seconds a move.",
  play: "A game against Tilefish with no help. Your clock keeps running if you switch tabs.",
  practice: "Against Tilefish with hints, take-backs and a pause button. Games where you use them are marked as assisted.",
  duo: "Two people at one screen. The rack is hidden between turns.",
  analysis: "Paste a position or open a game record (.gcg or a Tilefish file) to review it or play on from it.",
  naspa: "As in the NASPA rules of 1 December 2016: 10 points off for each started minute past zero; more than 10 minutes over loses the game.",
  flag: "The game ends, lost, when a clock reaches zero.",
};
const PREMIUM = (() => {
  const m = {};
  const put = (kind, list) => list.forEach(([r, c]) => {
    for (const [rr, cc] of [[r, c], [c, r], [N - 1 - r, c], [r, N - 1 - c], [N - 1 - r, N - 1 - c], [c, N - 1 - r], [N - 1 - c, r], [N - 1 - c, N - 1 - r]])
      m[rr * N + cc] = kind;
  });
  put("tw", [[0, 0], [0, 7]]);
  put("dw", [[1, 1], [2, 2], [3, 3], [4, 4]]);
  put("tl", [[1, 5], [5, 5]]);
  put("dl", [[0, 3], [2, 6], [3, 7], [6, 6]]);
  m[7 * N + 7] = "star";
  return m;
})();
const PREMIUM_LABEL = { tw: "3W", dw: "2W", tl: "3L", dl: "2L", star: "★" };

const $ = (id) => document.getElementById(id);
const store = {
  get(k, d) { try { const v = localStorage.getItem("tilefish." + k); return v == null ? d : v; } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem("tilefish." + k, v); } catch (e) { /* private mode: not kept */ } },
  json(k, d) { try { const v = localStorage.getItem("tilefish." + k); return v ? JSON.parse(v) : d; } catch (e) { return d; } },
  put(k, v) { try { localStorage.setItem("tilefish." + k, JSON.stringify(v)); } catch (e) { /* full or private */ } },
  del(k) { try { localStorage.removeItem("tilefish." + k); } catch (e) { /* */ } },
};
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const capital = (s) => s.charAt(0).toUpperCase() + s.slice(1);
const pretty = (t) => t.replace(/\./g, "·");
const pct = (x) => Math.round(100 * x) + "%";

// ---------- settings ----------
const settings = {
  mode: store.get("mode", "play"), level: store.get("level", "strong"), lexicon: store.get("lexicon", "CSW24"),
  clock: store.get("clock", "0"), customMin: store.get("customMin", "20"), first: store.get("first", "first"),
  overtime: store.get("overtime", "naspa"), think: store.get("think", ""), theme: store.get("theme", "system"),
  sound: store.get("sound", "0"),
};
function applyTheme() {
  if (settings.theme === "system") delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = settings.theme;
}
applyTheme();

// ---------- engine ----------
const VERSION = "";  // web/build.sh sets "?v=CODE&d=DATA", passed on to the worker's files
let worker = null, nextId = 1, waiting = new Map(), loadedLex = null, epoch = 0;
function startWorker() {
  worker = new Worker("worker.js" + VERSION);
  worker.onmessage = (e) => {
    const m = e.data, w = waiting.get(m.id);
    if (!w) return;
    if (m.progress != null) { if (w.progress) w.progress(m.progress); return; }
    waiting.delete(m.id);
    if (m.error) w.reject(new Error(m.error)); else w.resolve(m.out);
  };
  worker.onerror = (e) => {
    e.preventDefault();
    for (const w of waiting.values()) w.reject(new Error("the engine stopped"));
    waiting.clear();
    engineFailed("The engine stopped unexpectedly.");
  };
}
function call(msg, progress) {
  return new Promise((resolve, reject) => {
    const id = nextId++;
    waiting.set(id, { resolve, reject, progress });
    worker.postMessage(Object.assign({ id }, msg));
  });
}
async function ui(cmd) {
  const out = await call({ run: "ui " + cmd });
  return JSON.parse(out.trim().split("\n").pop());
}
// A load that stops moving (a stalled download, or an engine the browser stopped without
// a word) ends with a message after this long, and the next try starts a fresh engine.
const STALL_MS = 45000;
async function loadLexicon(lex, progress) {
  if (loadedLex === lex) return;
  let timer = 0;
  const arm = () => {
    clearTimeout(timer);
    timer = setTimeout(() => {
      for (const w of waiting.values()) w.reject(new Error("the word list stopped loading; check the connection and try again"));
      waiting.clear();
      worker.terminate();
      startWorker();
      loadedLex = null;
    }, STALL_MS);
  };
  arm();
  try {
    await call({ load: lex }, (p) => { arm(); if (progress) progress(p); });
  } finally {
    clearTimeout(timer);
  }
  loadedLex = lex;
}
// Throw away the running search (if any) and rebuild the engine's game from the record.
async function restartEngine() {
  epoch++;
  for (const w of waiting.values()) w.reject(new Error("cancelled"));
  waiting.clear();
  if (worker) worker.terminate();
  startWorker();
  loadedLex = null;
  busy = false;
  const my = epoch;
  setStatus("Restarting the engine", "thinking");
  await loadLexicon(G.lexicon);
  const st = await replay();
  if (my !== epoch) return null;
  return st;
}
async function replay() {
  let r;
  if (G.gcg) {
    await call({ write: { path: "/data/import.gcg", text: G.gcg } });
    r = await ui("import /data/import.gcg");
  } else if (G.cgp) {
    r = await ui("cgp " + G.cgp);
  } else {
    r = await ui("new " + (G.firstSide === 0 ? "first" : "second") + " " + G.seed);
  }
  if (!r.ok) throw new Error(r.error);
  for (const e of G.record) {
    r = await ui("force " + e.move);
    if (!r.ok) throw new Error("could not replay " + e.move + ": " + r.error);
  }
  r = await ui("seat " + viewSeat(r.state));
  return r.state;
}

// ---------- the game ----------
let G = null;        // the game: settings, seed, record of committed moves, clocks
let S = null;        // the engine's state as the viewing player sees it
let rack = [], pending = new Map(), order = [], cursor = null, selected = -1;
let exchanging = false, xsel = new Set(), busy = false, checked = null, checkSeq = 0;
let last = new Set(), lastMine = false;
let reviewAt = -1;   // >= 0: showing the board after this many moves (review)
let concealed = false;

const isBotGame = () => G && (G.mode === "play" || G.mode === "practice" || G.mode === "analysis");
const sideToMove = () => (S ? S.turn : 0);
function viewSeat(state) {
  if (!G) return 0;
  if (G.mode === "duo") return state ? state.turn : 0;
  return G.human;
}
const humanTurn = () => S && !S.over && !G.over && reviewAt < 0 && (G.mode === "duo" || S.turn === G.human);
const assistAllowed = () => G && (G.mode === "practice" || G.mode === "analysis");
function sideName(side) {
  if (!G) return "";
  if (G.mode === "duo" || G.mode === "review") return "Player " + (side + 1);
  return side === G.human ? "You" : "Tilefish";
}
function sideScore(side) {
  if (!S) return 0;
  return side === S.seat ? S.you : S.bot;
}
function newGameObject() {
  const mode = settings.mode;
  let firstSide = 0;
  if (mode !== "duo") {
    let f = settings.first;
    if (f === "alternate") { f = store.get("lastFirst", "second") === "first" ? "second" : "first"; store.set("lastFirst", f); }
    return {
      id: Date.now().toString(36), version: 1, mode, lexicon: settings.lexicon, level: settings.level,
      clockMin: clockMinutes(), overtime: settings.overtime, think: settings.think, seed: newSeed(),
      firstSide, human: f === "first" ? 0 : 1, record: [], used: [0, 0], running: -1, since: 0, paused: false,
      pauseNote: "", assisted: false, over: false, result: null, startedAt: new Date().toISOString(), verdicts: {},
    };
  }
  return {
    id: Date.now().toString(36), version: 1, mode, lexicon: settings.lexicon, level: settings.level,
    clockMin: clockMinutes(), overtime: settings.overtime, think: "", seed: newSeed(),
    firstSide, human: 0, record: [], used: [0, 0], running: -1, since: 0, paused: false, pauseNote: "",
    assisted: false, over: false, result: null, startedAt: new Date().toISOString(), verdicts: {},
  };
}
// ?seed=N replays one deal (tests, and sharing a deal); otherwise every game is new.
const URL_SEED = /^\d+$/.test(new URLSearchParams(location.search).get("seed") || "") ? +new URLSearchParams(location.search).get("seed") : null;
const newSeed = () => (URL_SEED != null ? URL_SEED : Math.floor(Math.random() * 2 ** 31));
function clockMinutes() {
  if (settings.clock === "custom") return Math.max(1, Math.min(120, +settings.customMin || 20));
  return +settings.clock;
}
function describe(g) {
  const parts = [];
  parts.push({ play: "Play", practice: "Practice", duo: "Two players", analysis: "Analysis", review: "Review" }[g.mode] || g.mode);
  if (g.mode !== "duo" && g.mode !== "review") parts.push(capital(g.level));
  parts.push(LEX_NAME[g.lexicon] || g.lexicon);
  if (g.mode !== "review" && g.mode !== "analysis")
    parts.push(g.clockMin ? g.clockMin + " min each, " + (g.overtime === "naspa" ? "overtime penalty" : "lose on time") : "untimed");
  return parts.join(" · ");
}
function save() {
  if (!G || G.mode === "review") return;
  if (G.over) store.del("current"); else store.put("current", snapshotForSave());
}
function snapshotForSave() {
  const g = Object.assign({}, G);
  g.used = clockUsed();
  g.running = G.running;
  g.since = 0;
  return g;
}

// ---------- clocks (from timestamps, never from timer callbacks) ----------
function clockUsed() {
  const u = G.used.slice();
  if (G.running >= 0 && !G.paused && G.since) u[G.running] += Date.now() - G.since;
  return u;
}
function startClock(side) {
  if (!G || !G.clockMin || G.over) { if (G) G.running = -1; return; }
  if (G.running >= 0 && !G.paused && G.since) G.used[G.running] += Date.now() - G.since;
  G.running = side;
  G.since = Date.now();
}
function stopClocks() {
  if (G && G.running >= 0 && !G.paused && G.since) G.used[G.running] += Date.now() - G.since;
  if (G) { G.running = -1; G.since = 0; }
}
function overMs(side, used) { return Math.max(0, used[side] - G.clockMin * 60000); }
function penalty(side, used) {
  if (!G.clockMin || G.overtime !== "naspa") return 0;
  const over = overMs(side, used);
  if (over <= 0) return 0;
  return 10 * Math.ceil(Math.ceil(over / 1000) / 60);  // -0:01..-1:00 = 10, -1:01..-2:00 = 20
}
function fmtClock(ms) {
  const neg = ms < 0;
  let s = neg ? Math.ceil(-ms / 1000) : Math.ceil(ms / 1000);
  const m = Math.floor(s / 60);
  s %= 60;
  return (neg ? "−" : "") + m + ":" + String(s).padStart(2, "0");
}
let beeped = {};
function tickClocks() {
  if (!G || !G.clockMin) return;
  const used = clockUsed();
  for (let c = 0; c < 2; c++) {
    const side = cardSide(c), el = $("clock-" + c);
    const left = G.clockMin * 60000 - used[side];
    el.hidden = false;
    let text = fmtClock(left);
    const pen = penalty(side, used);
    if (pen) text += " (−" + pen + ")";
    el.textContent = text;
    el.classList.toggle("low", left > 0 && left < 60000);
    el.classList.toggle("over", left <= 0);
    $("card-" + c).classList.toggle("running", G.running === side && !G.paused);
    if (G.running === side && !G.paused && settings.sound === "1" && humanSide(side)) {
      for (const mark of [60000, 30000, 10000])
        if (left < mark && left > mark - 2000 && !beeped[side + ":" + mark]) { beeped[side + ":" + mark] = 1; beep(); }
    }
  }
  const mc = $("mclock");
  if (mc) {
    const side = G.running >= 0 ? G.running : -1;
    mc.textContent = side >= 0 && !G.paused ? fmtClock(G.clockMin * 60000 - used[side]) : G.paused ? "paused" : "";
    mc.classList.toggle("over", side >= 0 && used[side] > G.clockMin * 60000);
  }
  if (G.running >= 0 && !G.paused && !G.over) {
    const over = overMs(G.running, used);
    if ((G.overtime === "flag" && over > 0) || (G.overtime === "naspa" && over > 10 * 60000)) timeForfeit(G.running);
  }
}
const humanSide = (side) => G.mode === "duo" || side === G.human;
let audioCtx = null;
function beep() {
  try {
    audioCtx = audioCtx || new (window.AudioContext || window.webkitAudioContext)();
    const o = audioCtx.createOscillator(), g = audioCtx.createGain();
    o.frequency.value = 880;
    g.gain.value = 0.06;
    o.connect(g).connect(audioCtx.destination);
    o.start();
    o.stop(audioCtx.currentTime + 0.12);
  } catch (e) { /* no audio */ }
}
setInterval(tickClocks, 250);
document.addEventListener("visibilitychange", tickClocks);
const cardSide = (c) => (G && G.mode !== "duo" ? (c === 0 ? G.human : 1 - G.human) : c);

// ---------- board and rack ----------
const boardEl = $("board"), rackEl = $("rack");
const squares = [];
function buildBoard() {
  for (let s = 0; s < N * N; s++) {
    const d = document.createElement("div");
    d.className = "sq" + (PREMIUM[s] ? " " + PREMIUM[s] : "");
    d.dataset.sq = s;
    d.setAttribute("role", "gridcell");
    d.setAttribute("aria-label", Math.floor(s / N) + 1 + COLS[s % N]);
    squares.push(d);
    boardEl.appendChild(d);
  }
  for (let c = 0; c < N; c++) { const sp = document.createElement("span"); sp.textContent = COLS[c]; $("coords-top").appendChild(sp); }
  for (let r = 0; r < N; r++) { const sp = document.createElement("span"); sp.textContent = r + 1; $("coords-left").appendChild(sp); }
}
function tileEl(ch, extra) {
  const t = document.createElement("div");
  t.className = "tile" + (extra ? " " + extra : "");
  const blank = ch === "?" || ch === ch.toLowerCase();
  if (blank) t.classList.add("blank");
  const c = document.createElement("span");
  c.className = "ch";
  c.textContent = ch === "?" ? "" : ch.toUpperCase();
  t.appendChild(c);
  if (!blank) {
    const v = document.createElement("span");
    v.className = "val";
    v.textContent = VALUES[ch];
    t.appendChild(v);
  }
  t.setAttribute("aria-label", ch === "?" ? "blank" : blank ? "blank as " + ch.toUpperCase() : ch);
  return t;
}
let shownBoard = null;  // the board on screen (the live one, or a reviewed position)
const at = (s) => (shownBoard ? shownBoard[Math.floor(s / N)][s % N] : ".");
const filled = (s) => at(s) !== ".";
const occupied = (s) => filled(s) || pending.has(s);

function renderBoard(fresh) {
  for (let s = 0; s < N * N; s++) {
    const d = squares[s];
    d.textContent = "";
    d.classList.toggle("cursor", !!cursor && cursor.sq === s && !occupied(s) && humanTurn());
    d.classList.toggle("down", !!cursor && cursor.down);
    if (filled(s)) {
      let cls = last.has(s) ? "last" + (lastMine ? " mine" : "") : "";
      if (fresh && fresh.has(s)) cls += " fresh";
      d.appendChild(tileEl(at(s), cls));
    } else if (pending.has(s)) {
      d.appendChild(tileEl(pending.get(s).ch, "pending"));
    } else if (PREMIUM[s]) {
      const l = document.createElement("span");
      l.className = "lbl";
      l.textContent = PREMIUM_LABEL[PREMIUM[s]];
      d.appendChild(l);
    }
  }
}
function renderRack() {
  rackEl.textContent = "";
  rackEl.classList.toggle("concealed", concealed);
  const n = Math.max(7, rack.length);
  for (let i = 0; i < n; i++) {
    const r = rack[i];
    if (!r || reviewAt >= 0) {
      const e = document.createElement("div");
      e.className = "slot";
      rackEl.appendChild(e);
      continue;
    }
    const t = tileEl(r.ch, r.used ? "used" : "");
    if (i === selected) t.classList.add("sel");
    if (xsel.has(i)) t.classList.add("xsel");
    t.dataset.slot = i;
    rackEl.appendChild(t);
  }
}
function renderSide() {
  if (!S) return;
  for (let c = 0; c < 2; c++) {
    const side = cardSide(c);
    $("name-" + c).textContent = sideName(side);
    $("pts-" + c).textContent = G.result ? G.result.final[side] : sideScore(side);
    $("card-" + c).classList.toggle("turn", !S.over && !G.over && S.turn === side && reviewAt < 0);
    $("clock-" + c).hidden = !G.clockMin;
  }
  const mini = $("mini");
  mini.hidden = false;
  mini.innerHTML = '<span class="y">' + sideScore(cardSide(0)) + '</span><span class="sep">–</span><span class="b">' + sideScore(cardSide(1)) + '</span><span class="mclock" id="mclock"></span>';
  $("config").textContent = describe(G) + (G.assisted ? " · assisted" : "") + (G.paused ? " · paused" + (G.pauseNote ? " (" + G.pauseNote + ")" : "") : "");
  $("bag").textContent = S.bag ? S.bag + " in the bag" : "bag empty";
  if (S.win != null && isBotGame() && reviewAt < 0) {
    $("chance").hidden = false;
    $("chance-fill").style.width = (100 * S.win).toFixed(1) + "%";
    $("chance-label").textContent = "Tilefish estimates your chance of winning at " + pct(S.win);
  } else $("chance").hidden = true;
  const log = $("log");
  log.textContent = "";
  if (!S.history.length) {
    const li = document.createElement("li");
    li.className = "empty";
    li.textContent = "No moves yet.";
    log.appendChild(li);
  }
  S.history.forEach((h, k) => {
    const li = document.createElement("li");
    const side = h.who === "you" ? S.seat : 1 - S.seat;
    li.className = "s" + (G.mode === "duo" ? side : side === G.human ? 0 : 1) + (reviewAt === k + 1 ? " at" : "");
    const b = document.createElement("button");
    b.textContent = h.type === "pass" ? "Pass" : h.type === "exchange" ? "Exchange " + h.move.replace(/^exch\s*/i, "") : h.move;
    b.title = "Show the board after this move";
    b.onclick = () => enterReview(k + 1);
    const sc = document.createElement("span");
    sc.className = "sc";
    sc.textContent = h.type === "play" ? "+" + h.score : "";
    const tot = document.createElement("span");
    tot.className = "tot";
    tot.textContent = h.total;
    li.append(b, sc, tot);
    log.appendChild(li);
  });
  if (reviewAt < 0) log.scrollTop = log.scrollHeight;
  const counts = {};
  for (const ch of S.unseen) counts[ch] = (counts[ch] || 0) + 1;
  const un = $("unseen");
  un.textContent = "";
  for (const ch of Object.keys(counts).sort()) {
    const sp = document.createElement("span");
    sp.textContent = ch === "?" ? "Blank" : ch;
    if (counts[ch] > 1) { const n = document.createElement("span"); n.className = "n"; n.textContent = counts[ch]; sp.appendChild(n); }
    un.appendChild(sp);
  }
  $("unseen-count").textContent = S.unseen.length;
  renderExports();
}
function renderControls() {
  const mine = humanTurn() && !busy && !concealed;
  const reviewing = reviewAt >= 0;
  $("controls").hidden = reviewing;
  $("review-nav").hidden = !reviewing;
  $("btn-shuffle").disabled = !S || reviewing;
  $("btn-recall").disabled = !mine || !pending.size;
  $("btn-exchange").disabled = !mine || S.bag < 7;
  $("btn-exchange").textContent = exchanging ? "Cancel" : "Exchange";
  $("btn-pass").disabled = !mine || exchanging;
  $("btn-hint").hidden = !assistAllowed();
  $("btn-hint").disabled = !mine || exchanging;
  const play = $("btn-play");
  if (exchanging) {
    play.textContent = xsel.size ? "Exchange " + xsel.size : "Exchange";
    play.disabled = !mine || !xsel.size;
  } else {
    const mv = buildMove();
    const ready = mine && checked && checked.ok && mv && checked.text === mv.text;
    play.textContent = ready ? "Play " + checked.score : "Play";
    play.disabled = !ready;
  }
  const live = G && !G.over && !reviewing && G.mode !== "review";
  $("secondary").hidden = !live;
  $("btn-takeback").hidden = !assistAllowed();
  $("btn-takeback").disabled = !G || !G.record.some((e) => humanSide(e.side));
  $("btn-pause").hidden = !(assistAllowed() && G && G.clockMin);
  $("btn-pause").textContent = G && G.paused ? "Resume clock" : "Pause clock";
  $("btn-resign").hidden = !G || G.mode === "analysis";
  if (reviewing && S) {
    $("rv-pos").textContent = reviewAt === 0 ? "Start" : "After move " + reviewAt + " of " + S.history.length;
    $("rv-prev").disabled = $("rv-first").disabled = reviewAt === 0;
    $("rv-next").disabled = $("rv-last").disabled = reviewAt >= S.history.length;
  }
}
function setStatus(html, cls) {
  const el = $("status");
  el.className = "status" + (cls ? " " + cls : "");
  el.innerHTML = html;
}
function statusForTurn() {
  if (!S || !G) return setStatus("&nbsp;");
  if (reviewAt >= 0) return setStatus(reviewStatus());
  if (G.over) return setStatus("Game over.");
  if (busy) return setStatus(G.mode === "duo" ? "Working" : "Tilefish is thinking", "thinking");
  if (G.paused) return setStatus("The clock is paused.");
  if (!humanTurn()) return setStatus("&nbsp;");
  if (exchanging) return setStatus("Pick the tiles to put back in the bag.");
  const who = G.mode === "duo" ? sideName(S.turn) + " to move." : "Your move.";
  if (!pending.size) return setStatus(who);
  const mv = buildMove();
  if (mv && mv.error) return setStatus('<span class="bad">' + esc(mv.error) + "</span>");
  if (!checked || !mv || checked.text !== mv.text) return setStatus("&nbsp;");
  if (checked.ok) return setStatus("<b>" + esc(pretty(mv.text)) + "</b> scores " + checked.score + ".");
  setStatus('<span class="bad">' + esc(capital(checked.error)) + "</span>");
}
let toastTimer = 0;
function toast(text, ok) {
  const t = $("toast");
  t.textContent = text;
  t.className = "toast show" + (ok ? " ok" : "");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = "toast" + (ok ? " ok" : ""); }, 2800);
}
function render(fresh) {
  renderBoard(fresh);
  renderRack();
  renderSide();
  renderControls();
  statusForTurn();
  tickClocks();
}

// ---------- building a move from the tiles placed this turn ----------
function buildMove() {
  const keys = [...pending.keys()];
  if (!keys.length) return null;
  const rows = keys.map((s) => Math.floor(s / N)), cols = keys.map((s) => s % N);
  const oneRow = rows.every((r) => r === rows[0]), oneCol = cols.every((c) => c === cols[0]);
  if (!oneRow && !oneCol) return { error: "Your tiles must be in one row or one column." };
  let down;
  if (keys.length === 1) {
    const r = rows[0], c = cols[0];
    const across = (c > 0 && filled(r * N + c - 1)) || (c < N - 1 && filled(r * N + c + 1));
    const vert = (r > 0 && filled((r - 1) * N + c)) || (r < N - 1 && filled((r + 1) * N + c));
    down = vert && !across;
  } else down = oneCol;
  const step = down ? N : 1;
  const inLine = (s, prev) => s >= 0 && s < N * N && (down || Math.floor(s / N) === Math.floor(prev / N));
  let s = Math.min(...keys);
  while (inLine(s - step, s) && occupied(s - step)) s -= step;
  const start = s;
  let word = "", used = 0;
  for (; occupied(s); s += step) {
    if (pending.has(s)) { word += pending.get(s).ch; used++; } else word += ".";
    if (!inLine(s + step, s)) break;
  }
  if (used !== pending.size) return { error: "Your tiles must form one connected word." };
  const r = Math.floor(start / N) + 1, c = COLS[start % N];
  return { text: (down ? c + r : r + c) + " " + word };
}
async function checkPending() {
  const mv = buildMove();
  checked = null;
  if (!mv || mv.error || !humanTurn() || busy) { renderControls(); statusForTurn(); return; }
  const seq = ++checkSeq, my = epoch;
  let r;
  try { r = await ui("check " + mv.text); } catch (e) { return; }
  if (seq !== checkSeq || my !== epoch) return;
  checked = { text: mv.text, ok: r.ok, score: r.score, error: r.error };
  renderControls();
  statusForTurn();
}

// ---------- placing tiles ----------
function freeSlot(ch) {
  for (let i = 0; i < rack.length; i++) if (rack[i] && !rack[i].used && rack[i].ch === ch) return i;
  return -1;
}
function pickBlank() {
  return new Promise((resolve) => {
    const sheet = $("blank-picker"), box = $("blank-letters");
    box.textContent = "";
    const done = (v) => { sheet.hidden = true; document.removeEventListener("keydown", onKey, true); resolve(v); };
    const onKey = (e) => {
      if (/^[a-z]$/i.test(e.key)) { e.preventDefault(); e.stopPropagation(); done(e.key.toLowerCase()); }
      else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); done(null); }
    };
    for (const L of "ABCDEFGHIJKLMNOPQRSTUVWXYZ") {
      const b = document.createElement("button");
      b.textContent = L;
      b.setAttribute("aria-label", "Blank as " + L);
      b.onclick = () => done(L.toLowerCase());
      box.appendChild(b);
    }
    sheet.onclick = (e) => { if (e.target === sheet) done(null); };
    document.addEventListener("keydown", onKey, true);
    sheet.hidden = false;
    box.firstChild.focus();
  });
}
async function place(slot, sq, letter) {
  if (!humanTurn() || busy || exchanging || concealed || occupied(sq) || !rack[slot] || rack[slot].used) return false;
  let ch = rack[slot].ch;
  if (ch === "?") {
    ch = letter ? letter.toLowerCase() : await pickBlank();
    if (!ch || occupied(sq)) return false;
  }
  rack[slot].used = true;
  pending.set(sq, { ch, slot });
  order.push(sq);
  selected = -1;
  return true;
}
function unplace(sq) {
  const p = pending.get(sq);
  if (!p) return;
  rack[p.slot].used = false;
  pending.delete(sq);
  order = order.filter((s) => s !== sq);
}
function recall() {
  for (const sq of [...pending.keys()]) unplace(sq);
  order = [];
  checked = null;
}
function advanceCursor() {
  if (!cursor) return;
  const step = cursor.down ? N : 1;
  let s = cursor.sq;
  while (occupied(s)) {
    const next = s + step;
    if (next >= N * N || (!cursor.down && Math.floor(next / N) !== Math.floor(s / N))) { cursor = null; return; }
    s = next;
  }
  cursor.sq = s;
}
async function typeLetter(key) {
  if (!cursor || !humanTurn() || busy || exchanging) return;
  advanceCursor();
  if (!cursor) return;
  const L = key.toUpperCase();
  let slot = freeSlot(L), letter = null;
  if (slot < 0) { slot = freeSlot("?"); letter = L; }
  if (slot < 0) { toast("You have no " + L + " and no blank."); return; }
  if (await place(slot, cursor.sq, letter)) { advanceCursor(); render(); checkPending(); }
}
function backspace() {
  if (!order.length) return;
  const sq = order[order.length - 1];
  unplace(sq);
  cursor = { sq, down: cursor ? cursor.down : false };
  render();
  checkPending();
}

// ---------- turns ----------
function rackFrom(state, keep) {
  const left = state.rack.split(""), out = [];
  for (const r of keep) { const i = left.indexOf(r.ch); if (i >= 0) { out.push({ ch: r.ch, used: false }); left.splice(i, 1); } }
  for (const ch of left) out.push({ ch, used: false });
  return out;
}
function showState(state, mover) {
  const before = shownBoard;
  const fresh = new Set();
  if (before) for (let s = 0; s < N * N; s++) if (before[Math.floor(s / N)][s % N] === "." && state.board[Math.floor(s / N)][s % N] !== ".") fresh.add(s);
  const sameView = S && S.seat === state.seat;
  const kept = sameView ? rack.filter((r, i) => r && !r.used && !xsel.has(i)) : [];
  S = state;
  shownBoard = state.board;
  rack = rackFrom(state, kept);
  pending = new Map();
  order = [];
  checked = null;
  selected = -1;
  exchanging = false;
  xsel = new Set();
  if (fresh.size) { last = fresh; lastMine = mover != null && humanSide(mover) && G.mode !== "duo" ? true : mover === 0; }
  if (cursor && occupied(cursor.sq)) cursor = null;
  render(fresh.size && mover != null && !humanSide(mover) ? fresh : null);
}
// A move was accepted: record it exactly, charge and switch the clocks, save.
async function committed(state, mover) {
  const used = clockUsed();
  const rec = await ui("record");
  const moveText = rec.moves[rec.moves.length - 1];
  G.used = used;
  G.since = Date.now();
  G.record.push({ move: moveText, side: mover, used: used.slice() });
  if (!state.over) startClock(state.turn); else stopClocks();
  showState(state, mover);
  save();
  if (state.over) finishGame("out");
}
async function submit() {
  if (!humanTurn() || busy) return;
  let text;
  if (exchanging) {
    if (!xsel.size) return;
    text = "exch " + [...xsel].map((i) => rack[i].ch).join("");
  } else {
    const mv = buildMove();
    if (!mv || mv.error || !checked || !checked.ok || checked.text !== mv.text) return;
    text = mv.text;
  }
  await playMove(text);
}
async function playMove(text) {
  const my = epoch, mover = S.turn;
  busy = true;
  renderControls();
  let r;
  try { r = await ui("move " + text); } catch (e) { busy = false; if (my === epoch) engineFailed(e.message); return; }
  busy = false;
  if (my !== epoch) return;
  if (!r.ok) { toast(capital(r.error)); render(); return; }
  hidePanels();
  await committed(r.state, mover);
  nextTurn();
}
function nextTurn() {
  if (!G || G.over || !S || S.over) return;
  if (G.mode === "duo") return handover();
  if (S.turn !== G.human) botTurn();
}
function botSpec() {
  let t = G.think ? +G.think : LEVEL_TIME[G.level];
  if (G.clockMin) {
    const left = (G.clockMin * 60000 - clockUsed()[1 - G.human]) / 1000;
    t = Math.max(0.5, Math.min(t || 1, left / 12));
  }
  if (G.level === "casual" && !G.think) return "static+";
  return (G.level === "champion" ? "champion" : "sim") + ":time=" + (+t).toFixed(1);
}
async function botTurn() {
  if (!G || G.over || !isBotGame() || !S || S.over || S.turn === G.human) return;
  const my = epoch, mover = S.turn;
  busy = true;
  render();
  let r;
  try { r = await ui("bot " + botSpec()); } catch (e) { if (my === epoch) { busy = false; engineFailed(e.message); } return; }
  if (my !== epoch || G.over) return;  // a stale answer: the game moved on without it
  busy = false;
  if (!r.ok) { toast(capital(r.error)); render(); return; }
  await committed(r.state, mover);
  const h = S.history[S.history.length - 1];
  if (h) toast(h.type === "play" ? "Tilefish played " + pretty(h.move) + " for " + h.score
    : h.type === "exchange" ? "Tilefish exchanged " + h.move.replace(/^exch\s*/i, "") + " tiles" : "Tilefish passed", true);
}
function handover() {
  concealed = true;
  render();
  $("handover-title").textContent = sideName(S.turn) + " to move";
  $("handover-sub").textContent = "Pass the screen over. The rack stays hidden until you are ready.";
  $("handover").hidden = false;
  $("btn-handover").focus();
}
$("btn-handover").onclick = async () => {
  $("handover").hidden = true;
  const r = await ui("seat " + S.turn);
  concealed = false;
  rack = [];
  S = null;
  showState(r.state, null);
};

// ---------- the end ----------
function finishGame(reason, loser) {
  if (!G || G.over && G.result) return;
  stopClocks();
  epoch++;  // nothing still running may touch this game
  busy = false;
  const used = G.used;
  const base = [sideScore(0), sideScore(1)];
  const pen = [penalty(0, used), penalty(1, used)];
  let final = [base[0] - pen[0], base[1] - pen[1]];
  if (reason === "time" && G.overtime === "naspa") {
    // NASPA 2016, V.G: 100 points off; the opponent wins by at least one; unplayed tiles disregarded.
    final[loser] = base[loser] - 100;
    final[1 - loser] = Math.max(base[1 - loser], final[loser] + 1);
  }
  let winner = final[0] > final[1] ? 0 : final[1] > final[0] ? 1 : -1;
  if (reason === "time" || reason === "resign") winner = 1 - loser;
  G.over = true;
  G.result = { final, winner, reason, penalties: pen, endedAt: new Date().toISOString() };
  archive();
  store.del("current");
  render();
  showEnd();
}
function timeForfeit(side) { finishGame("time", side); }
function showEnd() {
  const R = G.result;
  const w = R.winner;
  $("end-title").textContent = w < 0 ? "A tie" : G.mode === "duo" ? sideName(w) + " wins" : w === G.human ? "You won" : "Tilefish won";
  const c0 = cardSide(0), c1 = cardSide(1);
  $("end-score").textContent = R.final[c0] + " – " + R.final[c1];
  const parts = [];
  if (R.reason === "resign") parts.push(sideName(1 - w) + " resigned.");
  else if (R.reason === "time") parts.push(sideName(1 - w) + (G.overtime === "naspa" ? " went more than 10 minutes over time: 100 points off, and the game is lost (NASPA 2016)." : " ran out of time."));
  else if (S.endYou > 0 || S.endBot > 0) parts.push((S.endYou > 0 ? sideName(S.seat) : sideName(1 - S.seat)) + " went out first and gained the other rack's value.");
  else parts.push("The game ended after six scoreless turns.");
  for (const s of [0, 1]) if (R.penalties[s] && R.reason !== "time") parts.push(sideName(s) + " lost " + R.penalties[s] + " points for overtime.");
  if (S.botRack && G.mode !== "duo") parts.push("Tilefish was left with " + (S.botRack || "nothing") + ".");
  if (G.assisted) parts.push("Assisted game.");
  $("end-detail").textContent = parts.join(" ");
  $("btn-review").hidden = !G.record.length && !G.gcg;
  setTimeout(() => { $("end").hidden = false; $("btn-review").focus(); }, 500);
}
function archive() {
  const list = store.json("games", []);
  list.unshift({ id: G.id, date: G.result.endedAt, title: describe(G), names: [sideName(0), sideName(1)], final: G.result.final, game: snapshotForSave() });
  store.put("games", list.slice(0, 30));
}

// ---------- hints ----------
function hidePanels() { $("hints").hidden = true; }
async function hint(secs) {
  if (!humanTurn() || busy || !assistAllowed()) return;
  const my = epoch;
  busy = true;
  G.assisted = true;
  render();
  setStatus("Tilefish is looking at your rack" + (secs > 2 ? ' <button class="link strong" id="btn-hint-cancel">Stop</button>' : ""), "thinking");
  const cancel = $("btn-hint-cancel");
  if (cancel) cancel.onclick = async () => { const st = await restartEngine(); if (st) { S = null; showState(st, null); } };
  let r;
  try { r = await ui("hint " + secs); } catch (e) { return; }
  if (my !== epoch) return;
  busy = false;
  render();
  if (!r.ok) { toast(capital(r.error)); return; }
  const h = r.hint, box = $("hints");
  box.textContent = "";
  const head = document.createElement("div");
  head.className = "panel-head";
  head.innerHTML = "<span>Best found</span><span class=\"dim\">" + esc(h.exact ? h.method + ", solved" : h.method + ", " + h.seconds + " s") + "</span>";
  box.appendChild(head);
  const top = h.moves[0] && isFinite(h.moves[0].win) ? h.moves[0].win : null;
  for (const m of h.moves.slice(0, 5)) {
    const b = document.createElement("button");
    b.className = "hint-row";
    const exch = /^exch/i.test(m.move), pass = /^pass/i.test(m.move);
    const close = top != null && isFinite(m.win) && top - m.win < 0.01 && m !== h.moves[0];
    const wp = isFinite(m.win) ? (close ? "≈ " : "") + pct(m.win) : "";
    b.innerHTML = "<span>" + esc(pass ? "Pass" : exch ? "Exchange " + m.move.replace(/^exch\s*/i, "") : pretty(m.move)) + "</span><span class=\"sc\">" +
      (m.score ? "+" + m.score : "") + "</span><span class=\"wp\">" + wp + "</span><span class=\"sub\">keeps " + esc(m.leave || "nothing") +
      " · value " + (+m.value).toFixed(1) + (m.pruned ? " · dropped early" : "") + "</span>";
    b.title = "Show this move on the board";
    b.onclick = () => showHint(m.move);
    box.appendChild(b);
  }
  const foot = document.createElement("div");
  foot.className = "hint-foot";
  foot.innerHTML = "<span>Win chances are estimates; ≈ marks moves within 1%.</span>";
  if (!h.exact) {
    const more = document.createElement("button");
    more.className = "link strong";
    more.textContent = "Analyse longer";
    more.onclick = () => hint(8);
    foot.appendChild(more);
  }
  box.appendChild(foot);
  box.hidden = false;
}
async function showHint(text) {
  recall();
  exchanging = false;
  xsel = new Set();
  const t = text.trim();
  if (/^pass/i.test(t)) { render(); return; }
  if (/^exch/i.test(t)) {
    exchanging = true;
    for (const ch of t.replace(/^exch\s*/i, "").toUpperCase()) {
      const i = rack.findIndex((r, k) => r && r.ch === ch && !xsel.has(k));
      if (i >= 0) xsel.add(i);
    }
    render();
    return;
  }
  const [coord, word] = t.split(/\s+/);
  let m = coord.match(/^(\d+)([A-O])$/i), down = false, r, c;
  if (m) { r = +m[1] - 1; c = COLS.indexOf(m[2].toUpperCase()); } else { m = coord.match(/^([A-O])(\d+)$/i); down = true; c = COLS.indexOf(m[1].toUpperCase()); r = +m[2] - 1; }
  let through = false;
  for (const ch of word) {
    if (ch === "(") { through = true; continue; }
    if (ch === ")") { through = false; continue; }
    const sq = r * N + c;
    if (!through && ch !== ".") {
      const slot = ch === ch.toUpperCase() ? freeSlot(ch) : freeSlot("?");
      if (slot >= 0) await place(slot, sq, ch === ch.toLowerCase() ? ch : null);
    }
    if (down) r++; else c++;
  }
  render();
  checkPending();
}

// ---------- take-back, pause, resign ----------
async function takeback() {
  if (!assistAllowed() || !G || G.over) return;
  let k = G.record.length - 1;
  while (k >= 0 && !humanSide(G.record[k].side)) k--;
  if (k < 0) return;
  G.assisted = true;
  G.record = G.record.slice(0, k);
  const prev = G.record.length ? G.record[G.record.length - 1].used : [0, 0];
  G.used = prev.slice();
  hidePanels();
  recall();
  // Rebuilding from the record also cancels a search that was still running.
  let st;
  try { st = await restartEngine(); } catch (e) { engineFailed(e.message); return; }
  if (!st) return;
  G.running = -1;
  startClock(st.turn);
  S = null;
  showState(st, null);
  save();
  toast("Taken back. The tiles you drew are back in the bag.", true);
}
$("btn-takeback").onclick = takeback;
$("btn-pause").onclick = () => {
  if (!G || !G.clockMin || !assistAllowed()) return;
  if (!G.paused) { if (G.running >= 0 && G.since) G.used[G.running] += Date.now() - G.since; G.paused = true; G.since = 0; G.pauseNote = ""; G.assisted = true; }
  else { G.paused = false; G.since = Date.now(); G.pauseNote = ""; }
  save();
  render();
};
let resignArmed = 0;
$("btn-resign").onclick = () => {
  const b = $("btn-resign");
  if (!resignArmed) {
    b.textContent = "Confirm resign";
    resignArmed = setTimeout(() => { resignArmed = 0; b.textContent = "Resign"; }, 3000);
    return;
  }
  clearTimeout(resignArmed);
  resignArmed = 0;
  b.textContent = "Resign";
  const loser = G.mode === "duo" ? S.turn : G.human;
  finishGame("resign", loser);
};

// ---------- review ----------
async function enterReview(n) {
  if (!S || busy) return;
  if (!G.over && G.mode !== "review" && !assistAllowed()) { toast("Moves can be reviewed when the game is over."); return; }
  reviewAt = Math.max(0, Math.min(n, S.history.length));
  recall();
  cursor = null;
  const r = await ui("at " + reviewAt);
  if (!r.ok) return;
  shownBoard = r.board;
  last = new Set();
  if (reviewAt > 0) {
    const prev = reviewAt > 1 ? (await ui("at " + (reviewAt - 1))).board : null;
    for (let s = 0; s < N * N; s++) if (shownBoard[Math.floor(s / N)][s % N] !== "." && (!prev || prev[Math.floor(s / N)][s % N] === ".")) last.add(s);
    const h = S.history[reviewAt - 1];
    lastMine = h.who === "you";
  }
  render();
  renderReviewPanel();
}
function leaveReview() {
  reviewAt = -1;
  shownBoard = S.board;
  $("review").hidden = true;
  render();
}
function reviewStatus() {
  if (reviewAt === 0) return "The empty board.";
  const h = S.history[reviewAt - 1];
  const side = h.who === "you" ? S.seat : 1 - S.seat;
  return "<b>" + esc(sideName(side)) + "</b>: " + esc(h.type === "pass" ? "Pass" : h.type === "exchange" ? "Exchange" : pretty(h.move)) + (h.type === "play" ? ", +" + h.score : "");
}
function reviewable() { return G && (G.over || G.mode === "review" || assistAllowed()); }
function reviewSides() { return G.mode === "duo" || G.mode === "review" ? [0, 1] : [G.human]; }
function renderReviewPanel() {
  const box = $("review");
  box.hidden = false;
  box.textContent = "";
  const head = document.createElement("div");
  head.className = "panel-head";
  head.innerHTML = "<span>Review</span>";
  const close = document.createElement("button");
  close.className = "link";
  close.textContent = G.over || G.mode === "review" ? "Final position" : "Back to the game";
  close.onclick = leaveReview;
  head.appendChild(close);
  box.appendChild(head);
  const body = document.createElement("div");
  body.className = "review-body";
  const v = reviewAt > 0 ? G.verdicts[reviewAt - 1] : null;
  if (v) {
    const d = document.createElement("div");
    d.className = "verdict";
    const loss = v.winLoss;
    const label = v.same ? "Tilefish's choice too." : loss < 0.01 ? "About as good as " + pretty(v.best) + " (within 1%)." :
      "Tilefish preferred " + pretty(v.best) + ": an estimated " + (100 * loss).toFixed(1) + "% less chance of winning" + (v.exact ? " (solved)" : "") + ".";
    d.textContent = "With " + v.rack + ": " + label + " [" + v.method + "]";
    body.appendChild(d);
    if (v.alts && v.alts.length) {
      const ol = document.createElement("ol");
      ol.className = "alts";
      for (const a of v.alts.slice(0, 4)) { const li = document.createElement("li"); li.textContent = pretty(a.move) + "  +" + a.score + "  " + pct(a.win); ol.appendChild(li); }
      body.appendChild(ol);
    }
  } else if (reviewAt > 0) {
    const d = document.createElement("div");
    d.className = "verdict";
    d.textContent = Object.keys(G.verdicts).length ? "No verdict for this move." : "Run the analysis to judge each move.";
    body.appendChild(d);
  }
  const row = document.createElement("div");
  row.className = "row";
  const go = document.createElement("button");
  go.className = "link strong";
  go.textContent = Object.keys(G.verdicts).length ? "Analyse again" : "Analyse the moves";
  go.onclick = analyseGame;
  row.appendChild(go);
  if (reviewAt > 0 && reviewAt <= G.record.length && !G.gcg && !G.cgp) {
    const again = document.createElement("button");
    again.className = "link strong";
    again.textContent = "Try this move again";
    again.title = "Practise from the position before this move, with the same tiles to come";
    again.onclick = () => tryAgain(reviewAt - 1);
    row.appendChild(again);
  }
  body.appendChild(row);
  const sum = summaryText();
  if (sum) { const p = document.createElement("div"); p.className = "verdict"; p.textContent = sum; body.appendChild(p); }
  box.appendChild(body);
}
function summaryText() {
  const lines = [];
  for (const side of reviewSides()) {
    const idx = S.history.map((h, k) => [h, k]).filter(([h]) => (h.who === "you" ? S.seat : 1 - S.seat) === side);
    const judged = idx.filter(([, k]) => G.verdicts[k]);
    const bingos = idx.filter(([h]) => h.tiles === 7).length;
    let s = sideName(side) + ": " + idx.length + " moves, " + bingos + " bingo" + (bingos === 1 ? "" : "s");
    if (judged.length) {
      const same = judged.filter(([, k]) => G.verdicts[k].same || G.verdicts[k].winLoss < 0.01).length;
      const lost = judged.reduce((a, [, k]) => a + G.verdicts[k].winLoss, 0);
      const worst = judged.filter(([, k]) => G.verdicts[k].winLoss >= 0.03).length;
      s += "; " + same + " of " + judged.length + " as good as Tilefish's choice; " + worst + " costly (3%+); estimated " + (100 * lost).toFixed(1) + "% winning chance given up in total";
    }
    if (G.clockMin && G.record.length) s += "; clock used " + fmtClock(G.used[side]).replace("−", "");
    lines.push(s + ".");
  }
  return lines.join(" ");
}
async function analyseGame() {
  if (!S) return;
  if (G.gcg == null && !G.record.length) return;
  const my = epoch;
  busy = true;
  G.verdicts = {};
  const todo = S.history.map((h, k) => [h, k]).filter(([h]) => reviewSides().includes(h.who === "you" ? S.seat : 1 - S.seat)).map(([, k]) => k);
  for (let i = 0; i < todo.length; i++) {
    setStatus("Analysing move " + (i + 1) + " of " + todo.length + ' <button class="link strong" id="btn-an-stop">Stop</button>', "thinking");
    $("btn-an-stop").onclick = async () => { const st = await restartEngine(); if (st) { busy = false; S = st; enterReview(reviewAt); } };
    let r;
    try { r = await ui("review " + todo[i] + " 1"); } catch (e) { return; }
    if (my !== epoch) return;
    if (r.ok) G.verdicts[todo[i]] = r;
    renderReviewPanel();
  }
  busy = false;
  if (G.over) archiveUpdate();
  render();
  renderReviewPanel();
}
function archiveUpdate() {
  const list = store.json("games", []);
  const i = list.findIndex((g) => g.id === G.id);
  if (i >= 0) { list[i].game = snapshotForSave(); store.put("games", list); }
}
async function tryAgain(k) {
  const r = await ui("rewind " + k);
  if (!r.ok) { toast(capital(r.error)); return; }
  const mover = G.record[k].side;
  G = Object.assign({}, G, { id: Date.now().toString(36), mode: "practice", record: G.record.slice(0, k), over: false, result: null,
    assisted: true, verdicts: {}, clockMin: 0, running: -1, used: [0, 0], human: G.mode === "duo" ? mover : G.human, level: G.level || "strong" });
  if (G.mode !== "duo" && mover !== G.human) G.human = mover;
  await ui("seat " + G.human);
  reviewAt = -1;
  $("review").hidden = true;
  $("end").hidden = true;
  S = null;
  const st = (await ui("state")).state;
  showState(st, null);
  save();
  nextTurn();
}
["rv-first", "rv-prev", "rv-next", "rv-last"].forEach((id) => {
  $(id).onclick = () => {
    const n = S.history.length;
    enterReview(id === "rv-first" ? 0 : id === "rv-prev" ? reviewAt - 1 : id === "rv-next" ? reviewAt + 1 : n);
  };
});

// ---------- exports ----------
function download(name, text, type) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([text], { type: type || "text/plain" }));
  a.download = name;
  document.body.appendChild(a);
  a.click();
  setTimeout(() => { URL.revokeObjectURL(a.href); a.remove(); }, 1000);
}
function renderExports() {
  const box = $("exports");
  box.textContent = "";
  if (!G) return;
  const add = (label, fn) => { const b = document.createElement("button"); b.className = "link"; b.textContent = label; b.onclick = fn; box.appendChild(b); };
  if (G.over || G.mode === "review" || assistAllowed()) {
    add("Save GCG", async () => { const r = await ui("gcg"); download("tilefish-" + G.id + ".gcg", r.gcg); });
  }
  if (G.mode !== "review") add("Save Tilefish file", () => download("tilefish-" + G.id + ".json", JSON.stringify(snapshotForSave(), null, 1), "application/json"));
  add("Copy position", async () => {
    const r = await ui("cgpout");
    try { await navigator.clipboard.writeText(r.cgp); toast("Position copied (your rack only).", true); } catch (e) { toast(r.cgp); }
  });
  if (reviewable() && reviewAt < 0 && S && S.history.length) add("Review", () => enterReview(S.history.length));
}

// ---------- pointer: tap, drag and drop ----------
let drag = null;
function squareAt(x, y) { const el = document.elementFromPoint(x, y); const sq = el && el.closest(".sq"); return sq ? +sq.dataset.sq : -1; }
function rackSlotAt(x, y) { const el = document.elementFromPoint(x, y); if (!el || !el.closest("#rack")) return -2; const t = el.closest("[data-slot]"); return t ? +t.dataset.slot : -1; }
function startDrag(e, from) {
  drag = { from, x: e.clientX, y: e.clientY, moved: false, ghost: null };
  document.addEventListener("pointermove", moveDrag);
  document.addEventListener("pointerup", endDrag, { once: true });
}
// Where a dragged tile lands: the square under it, or the nearest free square next to it
// when that one is taken (so a drop a little off target still works).
function dropTarget(sq) {
  if (sq < 0) return -1;
  if (!occupied(sq)) return sq;
  const r = Math.floor(sq / N), c = sq % N;
  for (const [dr, dc] of [[0, 1], [1, 0], [0, -1], [-1, 0], [1, 1], [1, -1], [-1, 1], [-1, -1]]) {
    const rr = r + dr, cc = c + dc;
    if (rr >= 0 && rr < N && cc >= 0 && cc < N && !occupied(rr * N + cc)) return rr * N + cc;
  }
  return -1;
}
function moveDrag(e) {
  if (!drag) return;
  if (!drag.moved && Math.hypot(e.clientX - drag.x, e.clientY - drag.y) < 6) return;
  if (!drag.moved) {
    drag.moved = true;
    const src = drag.from.slot != null ? rackEl.querySelector('[data-slot="' + drag.from.slot + '"]') : squares[drag.from.sq].firstChild;
    drag.size = src.getBoundingClientRect().width;
    // On a touch screen the tile rides above the finger, so the finger does not hide it.
    drag.lift = e.pointerType === "touch" ? drag.size * 0.9 : 0;
    drag.ghost = src.cloneNode(true);
    drag.ghost.classList.add("ghost");
    drag.ghost.classList.remove("sel", "pending");
    document.body.appendChild(drag.ghost);
    src.style.visibility = "hidden";
  }
  const x = e.clientX, y = e.clientY - drag.lift;
  const sq = squareAt(x, y);
  // Over the board the tile takes the size of a square, so you see exactly where it goes.
  const size = sq >= 0 ? squares[0].getBoundingClientRect().width : drag.size;
  drag.ghost.style.width = drag.ghost.style.height = size + "px";
  drag.ghost.style.left = x + "px";
  drag.ghost.style.top = y + "px";
  squares.forEach((d) => d.classList.remove("drop"));
  drag.target = dropTarget(sq);
  if (drag.target >= 0) squares[drag.target].classList.add("drop");
}
async function endDrag(e) {
  document.removeEventListener("pointermove", moveDrag);
  const d = drag;
  drag = null;
  if (!d) return;
  squares.forEach((q) => q.classList.remove("drop"));
  if (d.ghost) d.ghost.remove();
  if (!d.moved) return tap(d.from);
  const sq = d.target != null ? d.target : -1, slotTo = rackSlotAt(e.clientX, e.clientY);
  if (d.from.slot != null) {
    if (sq >= 0 && !occupied(sq)) await place(d.from.slot, sq);
    else if (slotTo >= -1) reorder(d.from.slot, slotTo);
  } else {
    const p = pending.get(d.from.sq);
    if (sq >= 0 && !occupied(sq) && p) { pending.delete(d.from.sq); pending.set(sq, p); order = order.map((s) => (s === d.from.sq ? sq : s)); }
    else if (sq !== d.from.sq) unplace(d.from.sq);
  }
  render();
  checkPending();
}
function reorder(from, to) {
  if (to < 0) to = rack.length - 1;
  if (to === from) return;
  const [t] = rack.splice(from, 1);
  rack.splice(to, 0, t);
  for (const p of pending.values()) {
    if (p.slot === from) p.slot = to;
    else if (from < to && p.slot > from && p.slot <= to) p.slot--;
    else if (from > to && p.slot >= to && p.slot < from) p.slot++;
  }
}
async function tap(from) {
  if (from.slot != null) {
    const i = from.slot;
    if (exchanging) { if (xsel.has(i)) xsel.delete(i); else xsel.add(i); }
    else if (cursor && !occupied(cursor.sq) && humanTurn() && !busy) { if (await place(i, cursor.sq)) { advanceCursor(); checkPending(); } }
    else selected = selected === i ? -1 : i;
    render();
    if (selected >= 0 && humanTurn()) setStatus("Now click a square for " + (rack[i].ch === "?" ? "the blank" : rack[i].ch) + ".");
    return;
  }
  unplace(from.sq);
  render();
  checkPending();
}
boardEl.addEventListener("pointerdown", (e) => {
  const sqEl = e.target.closest(".sq");
  if (!sqEl) return;
  const sq = +sqEl.dataset.sq;
  if (pending.has(sq)) { e.preventDefault(); startDrag(e, { sq }); }
});
boardEl.addEventListener("click", async (e) => {
  const sqEl = e.target.closest(".sq");
  if (!sqEl || !humanTurn()) return;
  const sq = +sqEl.dataset.sq;
  if (occupied(sq)) return;
  if (selected >= 0) { if (await place(selected, sq)) { render(); checkPending(); } return; }
  cursor = cursor && cursor.sq === sq ? { sq, down: !cursor.down } : { sq, down: false };
  hidePanels();
  render();
});
rackEl.addEventListener("pointerdown", (e) => {
  const t = e.target.closest("[data-slot]");
  if (!t || !rack[+t.dataset.slot] || rack[+t.dataset.slot].used) return;
  e.preventDefault();
  startDrag(e, { slot: +t.dataset.slot });
});

// ---------- keyboard ----------
const sheetOpen = () => ["start", "end", "blank-picker", "handover", "games", "help"].some((id) => !$(id).hidden);
document.addEventListener("keydown", (e) => {
  if (sheetOpen()) {
    if (e.key === "Escape") { for (const id of ["games", "help"]) $(id).hidden = true; }
    return;
  }
  if (e.ctrlKey || e.metaKey || e.altKey || /input|textarea/i.test(e.target.tagName)) return;
  const k = e.key;
  if (reviewAt >= 0) {
    if (k === "ArrowLeft") { e.preventDefault(); enterReview(reviewAt - 1); }
    if (k === "ArrowRight") { e.preventDefault(); enterReview(reviewAt + 1); }
    if (k === "Escape") leaveReview();
    return;
  }
  if (/^[a-z]$/i.test(k)) { e.preventDefault(); typeLetter(k); }
  else if (k === "Backspace") { e.preventDefault(); backspace(); }
  else if (k === "Enter" && e.target.tagName !== "BUTTON") { e.preventDefault(); submit(); }
  else if (k === "Escape") { recall(); hidePanels(); exchanging = false; xsel = new Set(); cursor = null; render(); }
  else if (k === " " && e.target.tagName !== "BUTTON") { e.preventDefault(); shuffle(); }
  else if (k.startsWith("Arrow") && cursor) {
    e.preventDefault();
    const r = Math.floor(cursor.sq / N), c = cursor.sq % N;
    if (k === "ArrowRight" && c < N - 1) cursor = { sq: cursor.sq + 1, down: false };
    if (k === "ArrowLeft" && c > 0) cursor = { sq: cursor.sq - 1, down: false };
    if (k === "ArrowDown" && r < N - 1) cursor = { sq: cursor.sq + N, down: true };
    if (k === "ArrowUp" && r > 0) cursor = { sq: cursor.sq - N, down: true };
    render();
  }
});
function shuffle() {
  if (!rack.length || reviewAt >= 0) return;
  const idx = rack.map((_, i) => i);
  for (let i = idx.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [idx[i], idx[j]] = [idx[j], idx[i]]; }
  const map = {};
  idx.forEach((oldI, newI) => { map[oldI] = newI; });
  rack = idx.map((i) => rack[i]);
  for (const p of pending.values()) p.slot = map[p.slot];
  xsel = new Set([...xsel].map((i) => map[i]));
  selected = -1;
  render();
}

// ---------- buttons ----------
$("btn-shuffle").onclick = shuffle;
$("btn-recall").onclick = () => { recall(); render(); };
$("btn-play").onclick = submit;
$("btn-hint").onclick = () => hint(1.5);
$("btn-exchange").onclick = () => { recall(); hidePanels(); exchanging = !exchanging; xsel = new Set(); cursor = null; render(); };
let passArmed = 0;
$("btn-pass").onclick = () => {
  const b = $("btn-pass");
  if (!passArmed) { b.textContent = "Confirm pass"; b.classList.add("armed"); passArmed = setTimeout(() => { passArmed = 0; b.textContent = "Pass"; b.classList.remove("armed"); }, 3000); return; }
  clearTimeout(passArmed);
  passArmed = 0;
  b.textContent = "Pass";
  b.classList.remove("armed");
  recall();
  playMove("pass");
};
$("btn-new").onclick = () => openStart();
$("btn-end-new").onclick = () => { $("end").hidden = true; openStart(); };
$("btn-review").onclick = () => { $("end").hidden = true; enterReview(S.history.length); };
$("btn-rematch").onclick = () => {
  $("end").hidden = true;
  if (G.mode !== "duo") settings.first = G.human === 0 ? "second" : "first";
  settings.mode = G.mode === "review" || G.mode === "analysis" ? "practice" : G.mode;
  startGame();
};
$("btn-focus").onclick = () => {
  const on = !$("app").classList.contains("focus");
  $("app").classList.toggle("focus", on);
  $("btn-focus").setAttribute("aria-pressed", on);
  store.set("focus", on ? "1" : "0");
};
if (store.get("focus", "0") === "1") { $("app").classList.add("focus"); $("btn-focus").setAttribute("aria-pressed", "true"); }
$("btn-help").onclick = () => { $("help").hidden = false; $("btn-help-close").focus(); };
$("btn-help-close").onclick = () => { $("help").hidden = true; };
$("btn-games").onclick = openGames;
$("btn-games-close").onclick = () => { $("games").hidden = true; };
$("banner-retry").onclick = async () => {
  $("banner").hidden = true;
  try { const st = await restartEngine(); if (st) { S = null; showState(st, null); nextTurn(); } } catch (e) { engineFailed(e.message); }
};
function engineFailed(msg) {
  busy = false;
  $("banner-text").textContent = msg + " Your game is kept.";
  $("banner").hidden = false;
  if (G && !G.over) { stopClocksForFailure(); save(); }
  render();
}
function stopClocksForFailure() {
  if (G.running >= 0 && !G.paused && G.since) G.used[G.running] += Date.now() - G.since;
  G.paused = true;
  G.since = 0;
  G.pauseNote = "engine restarting";
}

// ---------- games list ----------
function openGames() {
  const list = store.json("games", []);
  const ol = $("games-list");
  ol.textContent = "";
  if (!list.length) { const li = document.createElement("li"); li.textContent = "No finished games yet."; ol.appendChild(li); }
  list.forEach((g, i) => {
    const li = document.createElement("li");
    const d = new Date(g.date);
    const info = document.createElement("span");
    info.textContent = d.toLocaleDateString() + " " + d.toLocaleTimeString([], { hour: "2-digit", minute: "2-digit" }) + " · " + g.names[0] + " " + g.final[0] + " – " + g.final[1] + " " + g.names[1] + " · " + g.title;
    const acts = document.createElement("span");
    acts.className = "acts";
    const rv = document.createElement("button"); rv.className = "link strong"; rv.textContent = "Review";
    rv.onclick = async () => { $("games").hidden = true; await openSaved(g.game, true); };
    const ex = document.createElement("button"); ex.className = "link"; ex.textContent = "Save file";
    ex.onclick = () => download("tilefish-" + g.id + ".json", JSON.stringify(g.game, null, 1), "application/json");
    const del = document.createElement("button"); del.className = "link"; del.textContent = "Delete";
    del.onclick = () => { const l = store.json("games", []); l.splice(i, 1); store.put("games", l); openGames(); };
    acts.append(rv, ex, del);
    li.append(info, acts);
    ol.appendChild(li);
  });
  $("games").hidden = false;
  $("btn-games-close").focus();
}

// ---------- start, resume, import ----------
function openStart() {
  const cur = store.json("current", null);
  $("resume").hidden = !cur || cur.over;
  if (cur && !cur.over) $("resume-text").textContent = describe(cur) + ", " + cur.record.length + " moves";
  $("start").hidden = false;
  syncStart();
}
function syncStart() {
  document.querySelectorAll(".seg").forEach((seg) => {
    const name = seg.dataset.name;
    seg.querySelectorAll("button").forEach((b) => { b.classList.toggle("on", b.dataset.v === settings[name]); b.setAttribute("aria-checked", b.dataset.v === settings[name]); b.setAttribute("role", "radio"); });
  });
  document.querySelectorAll("[data-for]").forEach((el) => { el.hidden = !el.dataset.for.split(" ").includes(settings.mode); });
  $("mode-note").textContent = NOTES[settings.mode];
  $("level-note").textContent = NOTES[settings.level];
  $("lex-note").textContent = NOTES[settings.lexicon];
  $("ot-note").textContent = NOTES[settings.overtime];
  $("custom-row").hidden = settings.clock !== "custom";
  $("custom-min").value = settings.customMin;
  $("think").value = settings.think;
  $("sound").checked = settings.sound === "1";
  $("btn-start").textContent = settings.mode === "analysis" ? "Open position" : "Start game";
  const fake = { mode: settings.mode, level: settings.level, lexicon: settings.lexicon, clockMin: clockMinutes(), overtime: settings.overtime };
  let s = describe(fake);
  if (settings.mode === "play" || settings.mode === "practice") s += " · " + (settings.first === "first" ? "you move first" : settings.first === "second" ? "Tilefish moves first" : "first move alternates");
  if (settings.think && settings.mode !== "duo") s += " · Tilefish thinks up to " + settings.think + " s";
  $("summary").textContent = settings.mode === "analysis" ? "" : s;
}
document.querySelectorAll(".seg").forEach((seg) => {
  seg.addEventListener("click", (e) => {
    const b = e.target.closest("button");
    if (!b) return;
    settings[seg.dataset.name] = b.dataset.v;
    store.set(seg.dataset.name, b.dataset.v);
    if (seg.dataset.name === "theme") applyTheme();
    syncStart();
  });
});
$("custom-min").onchange = () => { settings.customMin = $("custom-min").value; store.set("customMin", settings.customMin); syncStart(); };
$("think").onchange = () => { settings.think = $("think").value; store.set("think", settings.think); syncStart(); };
$("sound").onchange = () => { settings.sound = $("sound").checked ? "1" : "0"; store.set("sound", settings.sound); if (settings.sound === "1") beep(); };

async function withLoading(lex, fn) {
  const btn = $("btn-start");
  btn.disabled = true;
  $("loading").hidden = false;
  $("loading-fill").style.width = "4%";
  $("loading-text").textContent = BUNDLED.has(lex) ? "Loading the word list" : "Loading " + LEX_NAME[lex] + " (downloaded once)";
  try {
    if (!worker) startWorker();
    if (loadedLex !== lex) { epoch++; await loadLexicon(lex, (p) => { $("loading-fill").style.width = (100 * p).toFixed(0) + "%"; }); }
    $("loading-fill").style.width = "100%";
    await fn();
    $("loading").hidden = true;
    $("start").hidden = true;
  } catch (err) {
    $("loading-fill").style.width = "0";
    $("loading-text").textContent = capital(err.message) + (!BUNDLED.has(lex) ? ". ENABLE works without a download." : ".");
  } finally {
    btn.disabled = false;
  }
}
function resetView() {
  S = null; shownBoard = null; rack = []; pending = new Map(); order = []; last = new Set(); reviewAt = -1;
  cursor = null; selected = -1; exchanging = false; xsel = new Set(); busy = false; concealed = false; beeped = {};
  $("review").hidden = true;
  $("banner").hidden = true;
  hidePanels();
}
async function startGame() {
  if (settings.mode === "analysis") return openAnalysis();
  const wasBusy = busy;
  G = newGameObject();
  resetView();
  await withLoading(G.lexicon, async () => {
    // A new game is a fresh engine state; a search still running for the old one is discarded.
    if (wasBusy) await restartEngine();
    epoch++;
    const r = await ui("new " + (G.firstSide === 0 ? "first" : "second") + " " + G.seed);
    if (!r.ok) throw new Error(r.error);
    const st = (await ui("seat " + viewSeat(r.state))).state;
    showState(st, null);
    if (humanTurn()) cursor = { sq: 7 * N + 7, down: false };
    startClock(st.turn);
    save();
    render();
    if (G.mode === "duo") handover(); else nextTurn();
  });
}
async function openSaved(g, review) {
  G = Object.assign({ verdicts: {} }, g);
  resetView();
  await withLoading(G.lexicon, async () => {
    const st = await restartEngine();
    if (!st) return;
    S = null;
    showState(st, null);
    if (G.over || review) { $("start").hidden = true; await enterReview(S.history.length); return; }
    // A restored game starts paused; time while the page was closed is not charged.
    if (G.clockMin) { G.paused = true; G.pauseNote = "restored"; G.since = 0; }
    $("banner").hidden = true;
    save();
    render();
    if (G.mode === "duo") handover(); else nextTurn();
  });
}
$("btn-resume").onclick = () => { const cur = store.json("current", null); if (cur) openSaved(cur, false); };
async function openAnalysis() {
  const text = $("cgp-in").value.trim();
  if (!text) { $("loading").hidden = false; $("loading-text").textContent = "Paste a position, or open a file."; return; }
  G = Object.assign(newGameObject(), { mode: "analysis", cgp: text.replace(/\s+lex\s+\S+;?\s*$/i, ""), clockMin: 0, assisted: true });
  resetView();
  await withLoading(G.lexicon, async () => {
    const r = await ui("cgp " + G.cgp);
    if (!r.ok) throw new Error(r.error);
    G.human = r.state.turn;
    const st = (await ui("seat " + G.human)).state;
    showState(st, null);
    render();
  });
}
$("file-in").onchange = async (e) => {
  const f = e.target.files[0];
  if (!f) return;
  const text = await f.text();
  e.target.value = "";
  let saved = null;
  try { saved = JSON.parse(text); } catch (err) { saved = null; }
  if (saved && saved.version && saved.seed != null && Array.isArray(saved.record)) {
    settings.lexicon = saved.lexicon;
    return openSaved(saved, !!saved.over);
  }
  if (!/^\s*[#>]/m.test(text)) { $("loading").hidden = false; $("loading-text").textContent = "That file is neither a GCG record nor a Tilefish file."; return; }
  G = Object.assign(newGameObject(), { mode: "review", gcg: text, clockMin: 0, record: [], over: true });
  const lexLine = text.match(/^#lexicon\s+(\S+)/m);
  if (lexLine && /^(CSW|NWL)/i.test(lexLine[1])) G.lexicon = /^CSW/i.test(lexLine[1]) ? "CSW24" : "NWL23";
  else if (lexLine && /^OXENDICT/i.test(lexLine[1])) G.lexicon = "OXENDICT";
  resetView();
  await withLoading(G.lexicon, async () => {
    await call({ write: { path: "/data/import.gcg", text } });
    const r = await ui("import /data/import.gcg");
    if (!r.ok) throw new Error("this record could not be read: " + r.error);
    G.human = 0;
    showState(r.state, null);
    await enterReview(r.state.history.length);
  });
};
$("btn-start").onclick = startGame;

buildBoard();
openStart();
render();
