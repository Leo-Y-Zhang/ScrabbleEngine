"use strict";
// Tilefish in the browser.  The engine (tilefish.cpp, compiled to WebAssembly) runs in
// worker.js and owns the game: rules, scoring, the bag.  This file keeps the record of
// committed moves with the clocks, which is enough to rebuild the engine's game exactly
// (the seed fixes every draw), so a reload, a crashed worker, a cancelled search or a
// take-back all go through the same replay.

const N = 15;
const COLS = "ABCDEFGHIJKLMNO";
const VALUES = { A: 1, B: 3, C: 3, D: 2, E: 1, F: 4, G: 2, H: 4, I: 1, J: 8, K: 5, L: 1, M: 3, N: 1, O: 1, P: 3, Q: 10, R: 1, S: 1, T: 1, U: 1, V: 4, W: 4, X: 8, Y: 4, Z: 10 };
const LEVEL_TIME = { beginner: 0, easy: 0, casual: 0, strong: 3, champion: 10 };
const WEAK = new Set(["beginner", "easy"]);  // levels that choose among the static best moves in the page
const LEX_NAME = { CSW24: "Collins 2024", NWL23: "NWL 2023", CEL: "Common English", ENABLE: "ENABLE", OXENDICT: "Oxford spelling" };
const BUNDLED = new Set(["CEL", "ENABLE", "OXENDICT"]);  // free lists that come with the page
const NOTES = {
  CSW24: "Collins Scrabble Words, the World Championship list. About 10 MB, downloaded once and kept by this browser.",
  NWL23: "The North American tournament list. About 8 MB, downloaded once and kept by this browser.",
  CEL: "The Common English Lexicon: about 68,000 everyday words, without the obscure ones tournament lists allow. A free list (MIT licence) that comes with Tilefish, good for friendly games and new players.",
  ENABLE: "A free public-domain list that comes with Tilefish.",
  OXENDICT: "British English in Oxford spelling (realize, colour), a free list that comes with Tilefish. Built from the English Speller Database: not the Oxford English Dictionary's own list.",
  beginner: "Plays short, everyday words, scores modestly and rarely bingos: a gentle game for learning.",
  easy: "A friendly club player: sound moves, the odd bingo, and now and then a slip.",
  casual: "Plays at once, from its evaluation of each move, with exact endgames. Already strong.",
  strong: "Simulates its 15 best candidates up to 600 times each: about a second a move on one core, less on several.",
  champion: "Its full search, up to 10 seconds a move (about half that on a multi-core device, searching more).",
  play: "A game against Tilefish with no help. Your clock keeps running if you switch tabs.",
  practice: "Against Tilefish with hints, take-backs and a pause button. Games where you use them are marked as assisted.",
  duo: "Two people at one screen. The rack is hidden between turns.",
  analysis: "Paste a position or open a game record (.gcg or a Tilefish file) to review it or play on from it.",
  naspa: "As in the NASPA rules of 1 December 2016: 10 points off for each started minute past zero; more than 10 minutes over loses the game.",
  wespa: "As in WESPA's rules (and the ABSP's): 10 points off for each minute or part of one past zero; 10 minutes over forfeits the game, by at least 100 points.",
  flag: "The game ends, lost, when a clock reaches zero.",
};
// Tournament rules.  Under NASPA's or WESPA's a word not in the list can be played (a
// phony): Tilefish, which knows every word, challenges it off and the turn scores nothing.
// The player may challenge Tilefish's plays; Tilefish plays only words in the list, so such
// a challenge fails and costs what the rules say.  Words are not marked as they are placed.
const RULES_NAME = { checked: "Casual", naspa: "NASPA rules", wespa: "WESPA rules" };
const RULES_NOTE = {
  checked: "Words are checked as you place them: a word not in the list cannot be played, so there are no phonies and no challenges.",
  naspa: "North American tournament rules. You may play a word that is not in the list; Tilefish challenges it off and you score nothing that turn. You may challenge Tilefish's plays: with Collins an unsuccessful challenge gives it 5 points a word, otherwise it costs your turn (double challenge). Words are not marked as you place them.",
  wespa: "International rules, which the ABSP in the UK and most associations outside North America use. You may play a word that is not in the list; Tilefish challenges it off and you score nothing that turn. An unsuccessful challenge of Tilefish's play gives it the points below for each word challenged. Words are not marked as you place them.",
};
const WPEN_NOTE = { 5: "5 points a word: WESPA's preferred penalty, and the ABSP's in top divisions.", 10: "10 points a word, a penalty WESPA also allows.", 0: "A free challenge: no penalty, as in many ABSP divisions." };
// The challenge rule a game is played under: "void" (words checked as they are placed),
// "double" (an unsuccessful challenge loses the challenger's turn), "free", or the points
// a word challenged that an unsuccessful challenge gives the challenged player ("5", "10").
function chalRule(g) {
  if (!g || !g.rules || g.rules === "checked" || (g.mode !== "play" && g.mode !== "practice")) return "void";
  if (g.rules === "naspa") return g.lexicon === "CSW24" ? "5" : "double";
  return g.wpen === "0" ? "free" : g.wpen === "10" ? "10" : "5";
}
const tournament = () => chalRule(G) !== "void";
const isPhonyError = (c) => !!c && !c.ok && /^not in the lexicon/.test(c.error || "");
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
const PREMIUM_SAY = { tw: "triple word", dw: "double word", tl: "triple letter", dl: "double letter", star: "centre star, double word" };

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
  rules: store.get("rules", "checked"), wpen: store.get("wpen", "5"),
  sound: store.get("sound", "0"), sfx: store.get("sfx", "0"), rate: store.get("rate", "1"),
};
function applyTheme() {
  if (settings.theme === "system") delete document.documentElement.dataset.theme;
  else document.documentElement.dataset.theme = settings.theme;
}
applyTheme();

// ---------- engine ----------
const ENGINE_OK = typeof WebAssembly === "object" && typeof Worker === "function";
const VERSION = "";  // web/build.sh sets "?v=CODE&d=DATA", passed on to the worker's files
let worker = null, nextId = 1, waiting = new Map(), loadedLex = null, epoch = 0;
// Search threads the engine got (worker.js: one per core where the page is cross-origin
// isolated).  With several, timed searches are shortened and still search more than one
// core did in the full time: the levels answer sooner, and hints, ratings, reviews and
// puzzles come faster.
let engineThreads = 1;
const speed = () => (engineThreads >= 3 ? 0.5 : engineThreads === 2 ? 0.75 : 1);
const secs = (s) => Math.max(0.2, +(s * speed()).toFixed(2));
function startWorker() {
  if (!ENGINE_OK) throw new Error("this browser cannot run the engine (it needs WebAssembly); please update it or try another browser");
  worker = new Worker("worker.js" + VERSION);
  worker.onmessage = (e) => {
    const m = e.data, w = waiting.get(m.id);
    if (!w) return;
    if (m.progress != null) { if (w.progress) w.progress(m.progress, m); return; }
    waiting.delete(m.id);
    if (m.threads) engineThreads = m.threads;
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
    await call({ load: lex }, (p, m) => { arm(); if (progress) progress(p, m); });
  } finally {
    clearTimeout(timer);
  }
  loadedLex = lex;
}
// Downloads a word list into the browser's cache in the background (the engine is not
// touched), so that Start finds it there.  A list needs a click on it, or an earlier
// game with it, before its download starts.
function prefetch(lex) {
  if (!ENGINE_OK) return;
  if (!worker) startWorker();
  worker.postMessage({ prefetch: lex });
}
// The word lists this browser keeps (worker.js), which load without a connection; the
// start screen says which, and marks the others while the device is offline.
let onDevice = new Set();
const offline = () => navigator.onLine === false;
async function refreshOnDevice() {
  if (!ENGINE_OK) return;
  if (!worker) startWorker();
  try { onDevice = new Set(await call({ onDevice: true })); } catch (e) { return; }
  if (!$("start").hidden) syncStart();
}
window.addEventListener("online", () => { if (!$("start").hidden) syncStart(); });
window.addEventListener("offline", () => { if (!$("start").hidden) syncStart(); });
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
// Rebuilds the game in the engine: an idle engine (as on a page just opened, which
// started its engine and word list early) is kept; one still searching is restarted.
async function resumeEngine() {
  if (busy) return restartEngine();
  const my = ++epoch;
  await loadLexicon(G.lexicon);
  const st = await replay();
  return my === epoch ? st : null;
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
const humanTurn = () => S && !S.over && !G.over && reviewAt < 0 && !puzzleDone() && (G.mode === "duo" || S.turn === G.human);
// Tiles can be set out while Tilefish thinks, as a plan for your next move; they are
// checked, and can be played, once its move is on the board.
const canArrange = () => !!setup || !!exploring || (S && !S.over && !G.over && reviewAt < 0 && !concealed && !puzzleDone() && (G.mode === "duo" ? humanTurn() : true));
const puzzleDone = () => G && G.mode === "puzzle" && G.puzzle && (G.puzzle.solved || G.puzzle.revealed);
const assistAllowed = () => G && (G.mode === "practice" || G.mode === "analysis");
function sideName(side) {
  if (!G) return "";
  if (G.mode === "review" && (G.from === "play" || G.from === "practice")) return side === G.human ? "Player" : "Tilefish";  // a shared game
  if (G.mode === "duo" || G.mode === "review") return "Player " + (side + 1);
  if (G.mode === "puzzle") return side === G.human ? "You" : "Opponent";
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
      rules: settings.rules, wpen: settings.wpen, challenged: -1,
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
  parts.push({ play: "Play", practice: "Practice", duo: "Two players", analysis: "Analysis", review: "Review", puzzle: g.puzzle ? puzzleName(!!g.puzzle.daily, g.puzzle.kind) : "Puzzle" }[g.mode] || g.mode);
  if (g.mode !== "duo" && g.mode !== "review" && g.mode !== "puzzle") parts.push(capital(g.level));
  parts.push(LEX_NAME[g.lexicon] || g.lexicon);
  const rule = chalRule(g);
  if (rule !== "void") parts.push(RULES_NAME[g.rules] + (rule === "double" ? ", double challenge" : rule === "free" ? ", free challenge" : ", " + rule + " points a word"));
  if (g.mode !== "review" && g.mode !== "analysis" && g.mode !== "puzzle")
    parts.push(g.clockMin ? g.clockMin + " min each, " + (g.overtime === "naspa" ? "NASPA overtime" : g.overtime === "wespa" ? "WESPA overtime" : "lose on time") : "untimed");
  return parts.join(" · ");
}
function save() {
  if (!G || G.mode === "review" || G.mode === "puzzle") return;
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
  if (!G.clockMin || (G.overtime !== "naspa" && G.overtime !== "wespa")) return 0;
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
    if ((G.overtime === "flag" && over > 0) || (G.overtime === "naspa" && over > 10 * 60000) || (G.overtime === "wespa" && over >= 10 * 60000))
      timeForfeit(G.running);
  }
}
const humanSide = (side) => G.mode === "duo" || side === G.human;
// Sound effects, made on the spot with Web Audio (no files): a wooden click for a tile,
// a rattle for a shuffle, clicks in step with the opponent's tiles dropping, a rising
// figure for a bingo.  Vibration on phones goes with them.  All off unless chosen.
const sfx = (() => {
  let ctx = null, noise = null;
  const on = () => settings.sfx === "1";
  const ac = () => {
    try {
      ctx = ctx || new (window.AudioContext || window.webkitAudioContext)();
      if (ctx.state === "suspended") ctx.resume();
      return ctx;
    } catch (e) { return null; }
  };
  const buzz = (p) => { try { if (navigator.vibrate) navigator.vibrate(p); } catch (e) { /* */ } };
  function click(c, at, freq, vol) {
    if (!noise) {
      noise = c.createBuffer(1, Math.floor(c.sampleRate * 0.05), c.sampleRate);
      const d = noise.getChannelData(0);
      for (let i = 0; i < d.length; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / d.length, 4);
    }
    const src = c.createBufferSource(), bp = c.createBiquadFilter(), g = c.createGain();
    src.buffer = noise;
    bp.type = "bandpass"; bp.frequency.value = freq; bp.Q.value = 3.5;
    g.gain.setValueAtTime(vol, at);
    g.gain.exponentialRampToValueAtTime(0.0008, at + 0.045);
    src.connect(bp).connect(g).connect(c.destination);
    src.start(at);
    const o = c.createOscillator(), g2 = c.createGain();
    o.frequency.setValueAtTime(freq / 7, at);
    o.frequency.exponentialRampToValueAtTime(freq / 14, at + 0.05);
    g2.gain.setValueAtTime(vol * 0.5, at);
    g2.gain.exponentialRampToValueAtTime(0.0008, at + 0.06);
    o.connect(g2).connect(c.destination);
    o.start(at);
    o.stop(at + 0.07);
  }
  function tone(c, at, freq, vol, len, type) {
    const o = c.createOscillator(), g = c.createGain();
    o.type = type || "sine";
    o.frequency.value = freq;
    g.gain.setValueAtTime(0.0008, at);
    g.gain.exponentialRampToValueAtTime(vol, at + 0.012);
    g.gain.exponentialRampToValueAtTime(0.0008, at + len);
    o.connect(g).connect(c.destination);
    o.start(at);
    o.stop(at + len + 0.02);
  }
  const run = (fn) => { if (!on()) return; const c = ac(); if (c) fn(c, c.currentTime + 0.005); };
  return {
    place() { run((c, t) => click(c, t, 1900, 0.32)); if (on()) buzz(6); },
    lift() { run((c, t) => click(c, t, 1400, 0.16)); },
    shuffle() { run((c, t) => { for (let i = 0; i < 6; i++) click(c, t + i * 0.028, 1500 + 300 * Math.random(), 0.12); }); },
    play(n, bingo) {
      run((c, t) => {
        for (let i = 0; i < Math.max(1, n); i++) click(c, t + i * 0.05, 1700 + 120 * (i % 3), 0.26);
        if (bingo) [523.3, 659.3, 784, 1046.5].forEach((f, i) => tone(c, t + 0.18 + i * 0.09, f, 0.09, 0.32));
      });
      if (on()) buzz(bingo ? [12, 40, 12, 40, 24] : 12);
    },
    opp(n, bingo) {
      run((c, t) => {
        for (let i = 0; i < Math.max(1, n); i++) click(c, t + 0.02 + i * 0.03, 1300 + 140 * (i % 2), 0.2);
        if (bingo) [392, 493.9, 587.3].forEach((f, i) => tone(c, t + 0.2 + i * 0.09, f, 0.07, 0.3));
      });
      if (on()) buzz([8, 30, 8]);
    },
    bad() { run((c, t) => tone(c, t, 150, 0.08, 0.16, "triangle")); if (on()) buzz(30); },
    good() { run((c, t) => [659.3, 784, 1046.5].forEach((f, i) => tone(c, t + i * 0.08, f, 0.08, 0.3))); },
    end(won) { run((c, t) => (won ? [523.3, 659.3, 784, 1046.5] : [440, 523.3, 659.3]).forEach((f, i) => tone(c, t + i * 0.11, f, 0.08, 0.6))); },
  };
})();
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
  if (!fresh && rvFresh && reviewAt >= 0) fresh = rvFresh;
  rvFresh = null;
  const marks = reviewAt >= 0 ? reviewMarks() : liveMarks();
  const bad = badSquares();
  for (let s = 0; s < N * N; s++) {
    const d = squares[s];
    d.textContent = "";
    d.classList.toggle("cursor", !!cursor && cursor.sq === s && (!occupied(s) || !!setup) && canArrange());
    d.classList.toggle("down", !!cursor && cursor.down);
    d.classList.toggle("lastsq", last.has(s) && filled(s) && !(marks && preview));
    d.classList.toggle("mine", lastMine);
    d.classList.toggle("badword", !!bad && bad.has(s));
    // What a screen reader says for the square: its name and what is on it.
    const name = Math.floor(s / N) + 1 + COLS[s % N], ch = filled(s) ? at(s) : pending.has(s) ? pending.get(s).ch : "";
    d.setAttribute("aria-label", name + (ch ? ", " + ch.toUpperCase() + (ch === ch.toLowerCase() ? " (blank)" : "") + (pending.has(s) ? ", placed this turn" : "")
      : PREMIUM[s] ? ", " + PREMIUM_SAY[PREMIUM[s]] : ""));
    if (filled(s)) {
      let cls = last.has(s) ? "last" + (lastMine ? " mine" : "") : "";
      if (fresh && fresh.has(s)) cls += " fresh";
      d.appendChild(tileEl(at(s), cls));
    } else if (pending.has(s)) {
      d.appendChild(tileEl(pending.get(s).ch, "pending" + (s === justPlaced ? " popin" : "")));
    } else if (PREMIUM[s]) {
      const l = document.createElement("span");
      l.className = "lbl";
      l.textContent = PREMIUM_LABEL[PREMIUM[s]];
      d.appendChild(l);
    }
  }
  drawReviewMarks(marks);
  justPlaced = -1;
  if (zoomed && cursor) keepVisible(cursor.sq);
}
function renderRack() {
  rackEl.textContent = "";
  rackEl.classList.toggle("concealed", concealed);
  rackEl.classList.toggle("review", reviewAt >= 0 && !exploring);
  if (reviewAt >= 0 && !exploring) {
    const v = shownVerdict(), left = v ? v.rack.split("") : [];
    const spent = tilesOf(preview ? preview.move : v ? v.played : "").split("");
    for (let i = 0; i < Math.max(7, left.length); i++) {
      if (!left[i]) { const e = document.createElement("div"); e.className = "slot"; rackEl.appendChild(e); continue; }
      const j = spent.indexOf(left[i]);
      if (j >= 0) spent.splice(j, 1);
      rackEl.appendChild(tileEl(left[i], j >= 0 ? (preview ? "spent alt" : "spent") : "kept"));
    }
    return;
  }
  const n = Math.max(7, rack.length);
  for (let i = 0; i < n; i++) {
    const r = rack[i];
    if (!r) {
      const e = document.createElement("div");
      e.className = "slot";
      rackEl.appendChild(e);
      continue;
    }
    const t = tileEl(r.ch, r.used ? "used" : "");
    if (i === selected) t.classList.add("sel");
    if (xsel.has(i)) t.classList.add("xsel");
    t.dataset.slot = i;
    t.dataset.id = r.id;
    rackEl.appendChild(t);
  }
}
function renderSide() {
  if (!S) return;
  for (let c = 0; c < 2; c++) {
    const side = cardSide(c);
    $("name-" + c).textContent = sideName(side);
    // A score that went up bumps, with the points gained floating beside it.
    const ptsEl = $("pts-" + c), val = G.result ? G.result.final[side] : sideScore(side), was = +ptsEl.textContent;
    if (reviewAt < 0 && ptsEl.dataset.game === G.id && val > was) {
      ptsEl.classList.remove("bump");
      ptsEl.removeAttribute("data-gain");
      void ptsEl.offsetWidth;
      ptsEl.classList.add("bump");
      ptsEl.dataset.gain = "+" + (val - was);
    } else if (ptsEl.dataset.game !== G.id) ptsEl.removeAttribute("data-gain");
    ptsEl.dataset.game = G.id;
    ptsEl.textContent = val;
    $("card-" + c).classList.toggle("turn", !S.over && !G.over && S.turn === side && reviewAt < 0 && !puzzleDone());
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
    const cls = classify(verdictAt(k));
    b.innerHTML = (cls ? badge(cls, "sm") : "") + esc(historyLabel(h, k));
    b.title = "Show the board after this move";
    b.onclick = () => enterReview(k + 1);
    const sc = document.createElement("span");
    sc.className = "sc";
    sc.textContent = h.type === "play" ? "+" + h.score + (h.bonus ? " +" + h.bonus : "") : "";
    const tot = document.createElement("span");
    tot.className = "tot";
    tot.textContent = h.total;
    li.append(b, sc, tot);
    log.appendChild(li);
  });
  if (reviewAt < 0) log.scrollTop = log.scrollHeight;
  const counts = {};
  for (const ch of S.unseen) counts[ch] = (counts[ch] || 0) + 1;
  const tr = tracker(counts);
  $("unseen-notes").innerHTML = '<div class="tr-sum"><span><b>' + tr.vowels + "</b> vowels</span><span><b>" + tr.cons + "</b> consonants</span>" +
    (tr.blanks ? "<span><b>" + tr.blanks + "</b> blank" + (tr.blanks > 1 ? "s" : "") + "</span>" : "") + "</div>" +
    (tr.notes.length ? '<ul class="tr-notes">' + tr.notes.map((n) => "<li>" + esc(n) + "</li>").join("") + "</ul>" : "") + inferHtml();
  if ($("btn-infer")) $("btn-infer").onclick = estimateRack;
  const un = $("unseen");
  un.textContent = "";
  for (const ch of Object.keys(counts).sort()) {
    const sp = document.createElement("span");
    sp.textContent = ch === "?" ? "Blank" : ch;
    if (counts[ch] > 1) { const n = document.createElement("span"); n.className = "n"; n.textContent = counts[ch]; sp.appendChild(n); }
    un.appendChild(sp);
  }
  $("unseen-count").textContent = S.unseen.length + " · " + tr.vowels + " vowels";
  renderExports();
}
// Practice and Analyse: what the opponent probably kept from its last play (`ui infer`,
// the inference the simulation itself uses), each tile's chance beside its chance in a
// plain draw from the tiles unseen then.  Using it marks the game as assisted, as a hint does.
let inferState = null;  // {at, busy} while it runs, then {at, r}
function inferAvailable() {
  if (!G || !assistAllowed() || !humanTurn() || busy || !S || !S.bag || reviewAt >= 0) return false;
  const h = S.history[S.history.length - 1];
  return !!h && h.type === "play" && h.tiles < 7 && moverOf(S.history.length - 1) !== S.seat;  // a bingo keeps nothing
}
async function estimateRack() {
  if (!inferAvailable()) return;
  const at = S.history.length, my = epoch;
  inferState = { at, busy: true };
  G.assisted = true;
  renderSide();
  let r;
  try { r = await ui("infer " + secs(1)); } catch (e) { inferState = null; return; }
  if (my !== epoch || !S || S.history.length !== at) return;
  inferState = { at, r };
  save();
  renderSide();
}
function inferHtml() {
  if (!S || reviewAt >= 0) return "";
  const st = inferState && inferState.at === S.history.length ? inferState : null;
  const opp = sideName(1 - S.seat);
  if (st && st.busy) return '<div class="infer dim">Estimating ' + esc(opp) + "'s rack…</div>";
  if (st && st.r) {
    const r = st.r;
    if (!r.ok) return '<div class="infer dim">' + esc(capital(r.error)) + ".</div>";
    const name = (t) => (t === "?" ? "blank" : t);
    const list = (ts) => ts.map((t) => "<b>" + name(t.tile) + "</b> " + pct(t.p) + ' <span class="dim">(' + pct(t.base) + ")</span>").join(", ");
    const up = r.tiles.filter((t) => t.p - t.base >= 0.04).sort((a, b) => b.p - b.base - (a.p - a.base)).slice(0, 4);
    const down = r.tiles.filter((t) => t.base - t.p >= 0.04).sort((a, b) => b.base - b.p - (a.base - a.p)).slice(0, 4);
    let h = '<div class="infer"><div class="infer-head">' + esc(opp) + " kept " + r.kept + " tile" + (r.kept === 1 ? "" : "s") + " after " + esc(pretty(r.move)) + ".</div>";
    if (up.length) h += '<div class="infer-row"><span class="dim">Likelier than by chance:</span> ' + list(up) + "</div>";
    if (down.length) h += '<div class="infer-row"><span class="dim">Less likely:</span> ' + list(down) + "</div>";
    if (!up.length && !down.length) h += '<div class="infer-row">Its play says little about the tiles it kept.</div>';
    return h + '<div class="infer-row dim">An estimate: racks with which its play was a good choice count for more. In brackets, the chance in a plain draw.</div></div>';
  }
  return inferAvailable() ? '<button class="link strong infer-btn" id="btn-infer">Estimate ' + esc(opp) + "'s rack</button>" : "";
}
// What the unseen tiles (the bag and the opponent's rack) mean for your next moves.
function tracker(counts) {
  const n = (ch) => counts[ch] || 0, total = S.unseen.length;
  const vowels = "AEIOU".split("").reduce((a, ch) => a + n(ch), 0), blanks = n("?"), cons = total - vowels - blanks;
  const mine = reviewAt < 0 ? rack.map((r) => r.ch) : [];
  const opp = G.mode === "duo" ? sideName(1 - S.seat) : G.mode === "puzzle" ? "the opponent" : "Tilefish";
  const notes = [];
  if (mine.includes("Q") && !n("U") && !mine.includes("U")) notes.push("You hold the Q and no U is left to draw.");
  else if (n("Q") && !n("U")) notes.push("The Q is still out and every U is gone.");
  if (n("S") === 1) notes.push("One S left."); else if (!n("S") && total > 7) notes.push("No S left.");
  if (blanks === 2) notes.push("Both blanks are still out.");
  const power = ["J", "Q", "X", "Z"].filter((ch) => n(ch));
  if (power.length) notes.push(power.join(", ") + (power.length > 1 ? " are" : " is") + " still out.");
  const ratio = total - blanks ? vowels / (total - blanks) : 0;
  if (total >= 14 && ratio > 0.5) notes.push("A vowel-heavy pool: keep consonants.");
  else if (total >= 14 && ratio < 0.3) notes.push("Few vowels left: keep one or two.");
  if (S.bag === 0 && total) notes.push(capital(opp) + " holds exactly these tiles.");
  else if (S.bag > 0 && S.bag <= 7) notes.push(S.bag + " in the bag; the other " + (total - S.bag) + " are on " + opp + "'s rack.");
  return { vowels, cons, blanks, notes };
}
function renderControls() {
  const mine = humanTurn() && !busy && !concealed;
  const reviewing = reviewAt >= 0 && !exploring;
  $("controls").hidden = reviewing;
  $("controls").classList.toggle("exploring", !!exploring);
  $("review-nav").hidden = reviewAt < 0 || !!exploring;
  $("btn-shuffle").disabled = $("btn-sort").disabled = !S || reviewing;
  $("btn-recall").disabled = !canArrange() || concealed || !pending.size;
  $("btn-exchange").disabled = !mine || S.bag < 7;
  $("btn-exchange").textContent = exchanging ? "Cancel" : "Exchange";
  $("btn-pass").disabled = !mine || exchanging;
  $("btn-challenge").hidden = !tournament() || G.over;
  $("btn-challenge").disabled = !mine || exchanging || !canChallenge();
  $("btn-hint").hidden = !assistAllowed();
  $("btn-hint").disabled = !mine || exchanging;
  const play = $("btn-play");
  if (exploring) {
    const mv = buildMove();
    const ready = checked && checked.ok && mv && checked.text === mv.text && !exploring.judging;
    play.textContent = exploring.judging ? "Judging" : ready ? "Judge " + checked.score : "Judge";
    play.disabled = !ready;
  } else if (exchanging) {
    play.textContent = xsel.size ? "Exchange " + xsel.size : "Exchange";
    play.disabled = !mine || !xsel.size;
  } else {
    const mv = buildMove();
    const phony = tournament() && isPhonyError(checked);
    const ready = mine && checked && (checked.ok || phony) && mv && checked.text === mv.text;
    const bd = ready && phony ? breakdown() : null;
    play.textContent = ready ? "Play " + (phony ? (bd ? bd.total : "") : checked.score) : "Play";
    play.disabled = !ready;
  }
  const live = G && !G.over && reviewAt < 0 && G.mode !== "review" && G.mode !== "puzzle";
  $("secondary").hidden = !live;
  $("btn-takeback").hidden = !assistAllowed();
  $("btn-takeback").disabled = !G || !G.record.some((e) => humanSide(e.side));
  $("btn-pause").hidden = !(G && G.clockMin && (assistAllowed() || G.paused));
  $("btn-pause").textContent = G && G.paused ? "Resume clock" : "Pause clock";
  $("btn-resign").hidden = !G || G.mode === "analysis" || G.mode === "puzzle";
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
  if (exploring) return setStatus(pending.size ? pendingStatus() : "Set out a move from the rack; <b>Judge</b> (Enter) rates it.");
  if (reviewAt >= 0) return setStatus(reviewStatus());
  if (puzzleDone()) return setStatus(G.puzzle.solved ? "Solved." : "The answer is on the board.");
  if (G.over) return setStatus("Game over.");
  if (busy) {
    const bd = pending.size && !analysing ? breakdown() : null;
    return setStatus((bd ? wordChips(bd, null) + ' <span class="dim">·</span> ' : "") + (G.mode === "duo" ? "Working" : analysing ? "Analysing" : "Tilefish is thinking"), "thinking");
  }
  if (G.paused) return setStatus("The clock is paused.");
  if (!humanTurn()) return setStatus("&nbsp;");
  if (exchanging) return setStatus("Pick the tiles to put back in the bag (click them, or type their letters).");
  const who = G.mode === "duo" ? sideName(S.turn) + " to move." : "Your move.";
  if (!pending.size) return setStatus(who + (G.mode === "puzzle" ? ' <span class="dim">Find the best move.</span>' : lastMoveNote()));
  setStatus(pendingStatus());
}
function pendingStatus() {
  const mv = buildMove();
  if (mv && mv.error) return '<span class="bad">' + esc(mv.error) + "</span>";
  const bd = breakdown();
  if (!checked || !mv || checked.text !== mv.text) return bd ? wordChips(bd, null) : "&nbsp;";
  if (checked.words && checked.words.length && bd && (checked.ok || /lexicon/.test(checked.error || ""))) return wordChips(bd, checked);
  if (checked.ok) return "<b>" + esc(pretty(mv.text)) + "</b> scores " + checked.score + ".";
  if (tournament() && isPhonyError(checked)) return "<b>" + esc(pretty(mv.text)) + "</b>" + (bd ? " scores " + bd.total : "") + ".";
  return '<span class="bad">' + esc(capital(checked.error)) + "</span>";
}
// The words a move forms, each with its points and the bonus squares it uses, worked out
// here from the board (the engine's own total is the one shown when the two differ).
function breakdown() {
  const mv = buildMove();
  if (!mv || mv.error) return null;
  const p = parsePlay(mv.text);
  if (!p) return null;
  const letterAt = (sq) => (pending.has(sq) ? pending.get(sq).ch : at(sq));
  const line = (sq, down) => {
    const step = down ? N : 1;
    const ok = (s2, prev) => s2 >= 0 && s2 < N * N && (down || Math.floor(s2 / N) === Math.floor(prev / N));
    let a = sq;
    while (ok(a - step, a) && occupied(a - step)) a -= step;
    const out = [];
    for (let z = a; ; z += step) { out.push(z); if (!(ok(z + step, z) && occupied(z + step))) break; }
    return out;
  };
  const scoreWord = (sqs) => {
    let sum = 0, wm = 1;
    const tags = [];
    for (const sq of sqs) {
      const ch = letterAt(sq);
      let v = ch === ch.toLowerCase() ? 0 : VALUES[ch] || 0;
      if (pending.has(sq)) {
        const pm = PREMIUM[sq];
        if (pm === "dl") { v *= 2; tags.push("2L"); } else if (pm === "tl") { v *= 3; tags.push("3L"); }
        else if (pm === "dw" || pm === "star") { wm *= 2; tags.push("2W"); } else if (pm === "tw") { wm *= 3; tags.push("3W"); }
      }
      sum += v;
    }
    return { word: sqs.map(letterAt).join("").toUpperCase(), score: sum * wm, tags, sqs };
  };
  const fresh = p.cells.filter((c) => c.fresh).map((c) => c.sq);
  if (!fresh.length) return null;
  const words = [];
  const main = line(fresh[0], p.down);
  if (main.length >= 2) words.push(scoreWord(main));
  for (const sq of fresh) { const cw = line(sq, !p.down); if (cw.length >= 2) words.push(scoreWord(cw)); }
  const bingo = fresh.length === 7 ? 50 : 0;
  return { words, bingo, total: words.reduce((a, w) => a + w.score, 0) + bingo };
}
function wordValidity(bd, chk) {
  if (!chk || !chk.words || (tournament() && !exploring)) return bd.words.map(() => null);  // tournament rules: you find out by playing
  if (chk.words.length === bd.words.length) return chk.words.map((w) => w.ok);
  return bd.words.map((w) => { const m = chk.words.find((x) => x.word === w.word); return m ? m.ok : null; });
}
function wordChips(bd, chk) {
  const valid = wordValidity(bd, chk);
  const chips = bd.words.map((w, i) => '<span class="wchip' + (valid[i] === false ? " bad" : valid[i] ? " ok" : "") + '" title="' +
    esc(w.tags.length ? w.tags.join(" ") : "no bonus squares") + '">' + (valid[i] === false ? "✗ " : "") + esc(w.word) + " <b>" + w.score + "</b>" +
    (w.tags.length ? '<span class="wtags">' + w.tags.join(" ") + "</span>" : "") + "</span>");
  if (bd.bingo) chips.push('<span class="wchip bingo">Bingo <b>+50</b></span>');
  const total = chk && chk.ok ? chk.score : bd.total;  // (a phony scores what the board says, until challenged)
  const bad = valid.some((v) => v === false);
  return '<span class="wchips">' + chips.join("") + (bd.words.length > 1 || bd.bingo ? '<span class="wsum">= ' + total + "</span>" : "") + "</span>" +
    (bad ? ' <span class="bad">not in ' + esc(LEX_NAME[G.lexicon] || G.lexicon) + "</span>" : "");
}
// Squares of the words the engine rejected, outlined in red on the board.
function badSquares() {
  if (!checked || !checked.words || reviewAt >= 0 && !exploring) return null;
  const bd = breakdown(), mv = buildMove();
  if (!bd || !mv || checked.text !== mv.text) return null;
  const valid = wordValidity(bd, checked), out = new Set();
  bd.words.forEach((w, i) => { if (valid[i] === false) w.sqs.forEach((q) => out.add(q)); });
  return out.size ? out : null;
}
// A move as the history lists it.
function historyLabel(h, k) {
  if (h.type === "phony") return h.move + " (challenged off)";
  if (h.type === "pass") return G && G.record[k] && G.record[k].chal ? "Lost challenge" : "Pass";
  if (h.type === "exchange") return "Exchange " + h.move.replace(/^exch\s*/i, "");
  return h.move + (h.bonus ? " (challenged: +" + h.bonus + ")" : "");
}
// The opponent's last move, in words, under the board until you start your own.
function lastMoveNote() {
  const h = S.history[S.history.length - 1];
  if (!h || h.who === "you" && G.mode !== "duo") return "";
  const name = G.mode === "duo" ? sideName(1 - S.turn) : "Tilefish";
  return ' <span class="dim">' + esc(name) + " " + (h.type === "play" ? "played <b>" + esc(pretty(h.move)) + "</b> for " + h.score
    : h.type === "exchange" ? "exchanged " + esc(h.move.replace(/^exch\s*/i, "")) + " tiles" : "passed") + ".</span>";
}
let toastTimer = 0;
function toast(text, ok) {
  if (!ok) sfx.bad();
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
  if (!mv || mv.error || (!exploring && (!humanTurn() || busy))) { renderBoard(); renderControls(); statusForTurn(); return; }
  const seq = ++checkSeq, my = epoch;
  let r;
  try { r = await ui(exploring ? "judge " + exploring.k + " " + mv.text + " 0" : "check " + mv.text); } catch (e) { return; }
  if (seq !== checkSeq || my !== epoch) return;
  checked = { text: mv.text, ok: r.ok, score: r.score, error: r.error, words: r.words };
  renderBoard();
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
  if (!canArrange() || (exchanging && humanTurn()) || occupied(sq) || !rack[slot] || rack[slot].used) return false;
  let ch = rack[slot].ch;
  if (ch === "?") {
    ch = letter ? letter.toLowerCase() : await pickBlank();
    if (!ch || occupied(sq)) return false;
  }
  rack[slot].used = true;
  pending.set(sq, { ch, slot });
  order.push(sq);
  selected = -1;
  justPlaced = sq;
  sfx.place();
  return true;
}
let justPlaced = -1;  // the square of the tile just put down (it pops in)
function unplace(sq) {
  const p = pending.get(sq);
  if (!p) return;
  sfx.lift();
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
  if (!cursor || !canArrange() || exchanging) return;
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
let tileIds = 0;
function rackFrom(state, keep) {
  const left = state.rack.split(""), out = [];
  for (const r of keep) { const i = left.indexOf(r.ch); if (i >= 0) { out.push({ ch: r.ch, used: false, id: r.id || ++tileIds }); left.splice(i, 1); } }
  for (const ch of left) out.push({ ch, used: false, id: ++tileIds });
  return out;
}
function showState(state, mover) {
  const before = shownBoard;
  const fresh = new Set();
  if (before) for (let s = 0; s < N * N; s++) if (before[Math.floor(s / N)][s % N] === "." && state.board[Math.floor(s / N)][s % N] !== ".") fresh.add(s);
  const sameView = S && S.seat === state.seat;
  // A plan made during the opponent's turn stays where its squares are still free.
  const plan = sameView && mover != null && !humanSide(mover) && !state.over && pending.size ? [...pending.entries()] : [];
  const kept = sameView ? rack.filter((r, i) => r && (plan.length || !r.used) && !xsel.has(i)) : [];
  S = state;
  shownBoard = state.board;
  rack = rackFrom(state, kept.map((r) => ({ ch: r.ch, id: r.id })));
  pending = new Map();
  order = [];
  for (const [sq, p] of plan) {
    if (filled(sq)) continue;
    const slot = rack.findIndex((r) => r && !r.used && r.ch === (p.ch === p.ch.toLowerCase() ? "?" : p.ch));
    if (slot < 0) continue;
    rack[slot].used = true;
    pending.set(sq, { ch: p.ch, slot });
    order.push(sq);
  }
  checked = null;
  selected = -1;
  exchanging = false;
  xsel = new Set();
  if (fresh.size) { last = fresh; lastMine = mover != null && humanSide(mover) && G.mode !== "duo" ? true : mover === 0; }
  if (cursor && occupied(cursor.sq)) cursor = null;
  render(fresh.size && mover != null && !humanSide(mover) ? fresh : null);
  if (pending.size) checkPending();
}
// A move was accepted: record it exactly, charge and switch the clocks, save.
async function committed(state, mover) {
  // A pause the page made itself (a restored game, an engine restart) ends with the next move;
  // one chosen in Practice stays until resumed.
  if (G.paused && G.pauseNote) { G.paused = false; G.pauseNote = ""; }
  const used = clockUsed();
  const rec = await ui("record");
  const moveText = rec.moves[rec.moves.length - 1];
  G.used = used;
  G.since = Date.now();
  G.record.push({ move: moveText, side: mover, used: used.slice() });
  if (!state.over) startClock(state.turn); else stopClocks();
  showState(state, mover);
  const lastH = S.history[S.history.length - 1];
  if (lastH && G.mode !== "duo" && !humanSide(mover)) sfx.opp(lastH.tiles, lastH.tiles === 7);
  else if (lastH) sfx.play(lastH.tiles, lastH.tiles === 7);
  save();
  // A puzzle is not a game: a move that goes out is judged (and taken back if wrong) by
  // puzzleCheck, never filed under Games or counted.
  if (state.over && G.mode !== "puzzle") finishGame("out");
}
async function submit() {
  if (exploring) return judgeTry();
  if (!humanTurn() || busy) return;
  let text;
  if (exchanging) {
    if (!xsel.size) return;
    text = "exch " + [...xsel].map((i) => rack[i].ch).join("");
  } else {
    const mv = buildMove();
    if (!mv || mv.error || !checked || checked.text !== mv.text) return;
    if (!checked.ok) return tournament() && isPhonyError(checked) ? playPhony(mv.text) : undefined;
    text = mv.text;
  }
  await playMove(text);
}
// Tournament rules: a play with a word not in the list.  Tilefish knows every word, so it
// always challenges: the play comes off, scores nothing, and the turn passes.
async function playPhony(text) {
  const my = epoch, mover = S.turn;
  const bad = (checked.words || []).filter((w) => !w.ok).map((w) => w.word);
  busy = true;
  renderControls();
  let r;
  try { r = await ui("phony " + text); } catch (e) { busy = false; if (my === epoch) engineFailed(e.message); return; }
  busy = false;
  if (my !== epoch) return;
  if (!r.ok) { toast(capital(r.error)); render(); return; }
  hidePanels();
  $("rating").hidden = true;
  await committed(r.state, mover);
  toast("Tilefish challenged: " + bad.join(", ") + (bad.length > 1 ? " are" : " is") + " not in " + (LEX_NAME[G.lexicon] || G.lexicon) +
    ". Your play comes off and scores nothing.");
  nextTurn();
}
// Tournament rules: the player challenges Tilefish's last play.  Tilefish plays only words
// in the list, so the play stands and the challenge costs what the rules say.
const canChallenge = () => {
  if (!tournament() || !S || !humanTurn() || G.challenged === S.history.length) return false;
  const h = S.history[S.history.length - 1];
  return !!h && h.type === "play" && h.who !== "you";
};
async function challenge(only) {
  if (!canChallenge() || busy) return;
  const my = epoch, n = S.history.length, rule = chalRule(G);
  const r = await ui("lastwords");
  if (my !== epoch || !r.ok) { if (!r.ok) toast(capital(r.error)); return; }
  let words = r.words;
  if (only) words = words.filter((w) => w.word === only);
  // Points a word: with several words, the player picks the ones to challenge.
  if (!only && words.length > 1 && rule !== "double" && rule !== "free") {
    setStatus("Challenge which word? " + words.map((w) => '<button class="wchip pick" data-w="' + esc(w.word) + '">' + esc(w.word) + "</button>").join("") +
      ' <button class="link" data-w="*">All ' + words.length + '</button> <button class="link" data-w="">Cancel</button>');
    $("status").querySelectorAll("[data-w]").forEach((b) => {
      b.onclick = () => { const w = b.dataset.w; if (!w) return render(); challenge(w === "*" ? "*all" : w); };
    });
    return;
  }
  if (only === "*all") words = r.words;
  if (!words.length) return render();
  G.challenged = n;
  const list = words.map((w) => w.word).join(", "), verb = words.length > 1 ? " are" : " is";
  if (words.some((w) => !w.ok)) {  // not expected: Tilefish plays only words in the list
    toast(words.filter((w) => !w.ok).map((w) => w.word).join(", ") + " is not in the list.");
    save();
    return render();
  }
  if (rule === "double") {
    const k = G.record.length;
    toast(list + verb + " good. You lose your turn (double challenge).");
    await playMove("pass");
    if (G.record[k]) { G.record[k].chal = 1; save(); render(); }
    return;
  }
  if (rule === "free") {
    toast(list + verb + " good. No penalty (free challenge).", true);
    save();
    return render();
  }
  const pts = +rule * words.length;
  const b = await ui("bonus " + pts);
  if (my !== epoch) return;
  if (!b.ok) { toast(capital(b.error)); return render(); }
  const rec = await ui("record");
  if (my !== epoch) return;
  G.record[G.record.length - 1].move = rec.moves[rec.moves.length - 1];
  S = Object.assign({}, S, { you: b.state.you, bot: b.state.bot, history: b.state.history });
  toast(list + verb + " good: " + pts + " points to Tilefish (" + rule + " a word).");
  save();
  render();
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
  $("rating").hidden = true;
  await committed(r.state, mover);
  if (G.mode === "puzzle") return puzzleCheck();
  nextTurn();
}
function nextTurn() {
  if (!G || G.over || !S || S.over) return;
  if (G.mode === "duo") return handover();
  if (S.turn !== G.human) botTurn();
}
function botSpec() {
  let t = G.think ? +G.think : LEVEL_TIME[G.level] * speed();
  if (G.clockMin) {
    const left = (G.clockMin * 60000 - clockUsed()[1 - G.human]) / 1000;
    t = Math.max(0.5, Math.min(t || 1, left / 12));
  }
  if (G.level === "casual" && !G.think) return "static+";
  if (WEAK.has(G.level)) return "static+";
  return (G.level === "champion" ? "champion" : "sim") + ":time=" + (+t).toFixed(1);
}
async function botTurn() {
  if (!G || G.over || !isBotGame() || !S || S.over || S.turn === G.human) return;
  const my = epoch, mover = S.turn;
  busy = true;
  render();
  let r;
  try { r = WEAK.has(G.level) ? await weakTurn() : await ui("bot " + botSpec()); } catch (e) { if (my === epoch) { busy = false; engineFailed(e.message); } return; }
  if (my !== epoch || G.over) return;  // a stale answer: the game moved on without it
  busy = false;
  if (!r.ok) { toast(capital(r.error)); render(); return; }
  await committed(r.state, mover);
  const h = S.history[S.history.length - 1];
  if (h) toast(h.type === "play" ? "Tilefish played " + pretty(h.move) + " for " + h.score
    : h.type === "exchange" ? "Tilefish exchanged " + h.move.replace(/^exch\s*/i, "") + " tiles" : "Tilefish passed", true);
  rateLastMove();
}
// The easier levels: a move chosen in the page among the static best, then played with
// `ui force`.  Beginner aims at about half the best score with short words and seldom
// bingos; Easy picks near the top, slipping now and then.  A short pause, so the move
// does not land the instant yours does.
const wordLen = (m) => { const p = parsePlay(m.move); return p ? p.cells.length : 0; };
function pickWeak(moves, level) {
  const plays = moves.filter((m) => !isExch(m.move) && !isPass(m.move));
  if (!plays.length) return moves.length ? moves[0].move : "pass";
  const best = Math.max(...plays.map((m) => m.score));
  const rnd = Math.random;
  if (level === "beginner") {
    const bingo = plays.find((m) => m.tiles === 7);
    if (bingo && S.history.length > 6 && rnd() < 0.04) return bingo.move;
    let pool = plays.filter((m) => m.tiles < 7 && wordLen(m) <= 6);
    if (!pool.length) pool = plays;
    const target = best * (0.3 + 0.3 * rnd());
    pool.sort((a, b) => Math.abs(a.score - target) - Math.abs(b.score - target));
    return pool[Math.floor(rnd() * Math.min(6, pool.length))].move;
  }
  if (plays[0].tiles === 7 && rnd() < 0.55) return plays[0].move;
  const pool = plays.filter((m) => m.tiles < 7 || rnd() < 0.3).slice(0, 24);
  let i = 0;
  while (i < pool.length - 1 && rnd() < 0.42) i++;
  return (pool[i] || plays[0]).move;
}
async function weakTurn() {
  const t0 = Date.now();
  const list = await ui("moves 160");
  if (!list.ok) return list;
  const mv = pickWeak(list.moves, G.level);
  const wait = 650 + Math.random() * 700 - (Date.now() - t0);
  if (wait > 0) await new Promise((res) => setTimeout(res, wait));
  return ui("force " + mv);
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
  } else if (reason === "time" && G.overtime === "wespa") {
    // WESPA: ten minutes over (100 points off) forfeits; the margin is the margin then, or 100 if more.
    final[loser] = base[loser] - 100;
    final[1 - loser] = Math.max(base[1 - loser], final[loser] + 100);
  }
  let winner = final[0] > final[1] ? 0 : final[1] > final[0] ? 1 : -1;
  if (reason === "time" || reason === "resign") winner = 1 - loser;
  G.over = true;
  G.result = { final, winner, reason, penalties: pen, endedAt: new Date().toISOString() };
  setTimeout(() => sfx.end(G.mode === "duo" ? winner >= 0 : winner === G.human), 450);
  archive();
  recordStats();
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
  else if (R.reason === "time") parts.push(sideName(1 - w) + (G.overtime === "naspa" ? " went more than 10 minutes over time: 100 points off, and the game is lost (NASPA 2016)."
    : G.overtime === "wespa" ? " went 10 minutes over time and forfeits the game (WESPA): 100 points off, and a margin of at least 100." : " ran out of time."));
  else if (S.endYou > 0 || S.endBot > 0) parts.push((S.endYou > 0 ? sideName(S.seat) : sideName(1 - S.seat)) + " went out first and gained the other rack's value.");
  else parts.push("The game ended after six scoreless turns.");
  for (const s of [0, 1]) if (R.penalties[s] && R.reason !== "time") parts.push(sideName(s) + " lost " + R.penalties[s] + " points for overtime.");
  if (S.botRack && G.mode !== "duo") parts.push("Tilefish was left with " + (S.botRack || "nothing") + ".");
  if (G.assisted) parts.push("Assisted game.");
  $("end-detail").textContent = parts.join(" ");
  $("btn-review").hidden = !G.record.length && !G.gcg;
  $("btn-end-share").hidden = !shareable(G);
  // (not if a new game was started, or the start card opened, in the meantime)
  const g = G;
  setTimeout(() => { if (G !== g || !$("start").hidden) return; $("end").hidden = false; $("btn-review").focus(); }, 500);
}
function archive() {
  const list = store.json("games", []);
  list.unshift({ id: G.id, date: G.result.endedAt, title: describe(G), names: [sideName(0), sideName(1)], final: G.result.final, game: snapshotForSave() });
  store.put("games", list.slice(0, 30));
}

// ---------- hints ----------
function hidePanels() { $("hints").hidden = true; if (hintState) hintState.shown = false; }
// Hints come in steps, as a coach would give them: a nudge (a bingo? how many tiles, what
// score?), then where the move goes, then the moves themselves.
let hintState = null;  // {h, step, at, shown}
const secsFor = (s) => secs(s);
async function hint(secsArg) {
  if (!humanTurn() || !assistAllowed()) return;
  if (hintState && hintState.at === S.history.length && !secsArg) {
    if (!hintState.shown) hintState.shown = true;
    else if (hintState.step < 3) hintState.step++;
    renderHint();
    return;
  }
  if (busy) return;
  const sec = secsArg || 1.5;
  const my = epoch;
  busy = true;
  G.assisted = true;
  render();
  setStatus("Tilefish is looking at your rack" + (sec > 2 ? ' <button class="link strong" id="btn-hint-cancel">Stop</button>' : ""), "thinking");
  const cancel = $("btn-hint-cancel");
  if (cancel) cancel.onclick = async () => { const st = await restartEngine(); if (st) { S = null; showState(st, null); } };
  let r;
  try { r = await ui("hint " + secsFor(sec)); } catch (e) { return; }
  if (my !== epoch) return;
  busy = false;
  if (!r.ok) { render(); toast(capital(r.error)); return; }
  const deeper = hintState && hintState.at === S.history.length;
  hintState = { h: r.hint, step: deeper ? 3 : 1, at: S.history.length, shown: true };
  render();
  renderHint();
}
function hintNudge(h) {
  const best = h.moves[0];
  if (!best) return "No move found.";
  if (isPass(best.move)) return "Passing is best here.";
  if (isExch(best.move)) return "Tilefish would exchange this turn rather than play: the rack needs fresh tiles.";
  const n = tilesOf(best.move).length, plays = h.moves.filter((m) => !isExch(m.move) && !isPass(m.move));
  const top = Math.max(...plays.map((m) => m.score));
  if (n === 7) return "There is a bingo: all seven tiles, for " + best.score + " points.";
  return "The best play uses " + n + " tile" + (n === 1 ? "" : "s") + " and scores " + best.score + "." +
    (top - best.score >= 6 ? " It is not the highest-scoring move: what it keeps matters more." : "") +
    (best.leave ? " It keeps " + best.leave.length + " tile" + (best.leave.length === 1 ? "" : "s") + "." : "");
}
function hintWhere(h) {
  const best = h.moves[0];
  const p = best && parsePlay(best.move);
  if (!p) return "";
  const sq = p.cells[0].sq, r = Math.floor(sq / N) + 1, c = COLS[sq % N];
  return p.down ? "It goes down column " + c + ", starting on row " + r + " (" + p.coord + "), " + p.cells.length + " squares long."
    : "It goes across row " + r + ", starting in column " + c + " (" + p.coord + "), " + p.cells.length + " squares long.";
}
function renderHint() {
  const box = $("hints"), hs = hintState;
  if (!hs) return;
  const h = hs.h;
  let html = '<div class="panel-head"><span>Hint <span class="dim">' + hs.step + " of 3</span></span><span class=\"dim\">" +
    esc(h.exact ? h.method + ", solved" : h.method + ", " + h.seconds + " s") + "</span></div>";
  html += '<div class="hint-steps"><p class="hint-step">' + esc(hintNudge(h)) + "</p>";
  if (hs.step >= 2) html += '<p class="hint-step where">' + esc(hintWhere(h) || "It is not a play on the board.") + "</p>";
  html += "</div>";
  if (hs.step >= 3) {
    const top = h.moves[0] && isFinite(h.moves[0].win) ? h.moves[0].win : null;
    for (const m of h.moves.slice(0, 5)) {
      const close = top != null && isFinite(m.win) && top - m.win < 0.01 && m !== h.moves[0];
      const wp = isFinite(m.win) ? (close ? "≈ " : "") + pct(m.win) : "";
      html += '<button class="hint-row" data-move="' + esc(m.move) + '" title="Put this move on the board"><span>' + esc(moveLabel(m.move)) + '</span><span class="sc">' +
        (m.score ? "+" + m.score : "") + '</span><span class="wp">' + wp + '</span><span class="sub">keeps ' + esc(m.leave || "nothing") +
        " · value " + (+m.value).toFixed(1) + (m.pruned ? " · dropped early" : "") + "</span></button>";
    }
  }
  if (hs.step >= 3 && h.exact && h.line && h.line.length) html += '<p class="coach-line hint-line"><span class="lbl">Best play to the end:</span> ' + lineHtml(h.line, S.turn) + "</p>";
  html += '<div class="hint-foot"><span>' + (hs.step >= 3 ? (h.exact ? "Solved exactly: the last figure is the end result of each move." : "Win chances are estimates; ≈ marks moves within 1%.") : "Each step tells a little more.") + "</span>" +
    (hs.step < 3 ? '<button class="ctl small" id="hint-more">' + (hs.step === 1 ? "Where?" : "Show the moves") + "</button>"
      : !h.exact ? '<button class="link strong" id="hint-longer">Analyse longer</button>' : "") + "</div>";
  box.innerHTML = html;
  box.hidden = false;
  box.querySelectorAll("[data-move]").forEach((b) => (b.onclick = () => showHint(b.dataset.move)));
  if ($("hint-more")) $("hint-more").onclick = () => { hs.step++; renderHint(); renderBoard(); };
  if ($("hint-longer")) $("hint-longer").onclick = () => hint(8);
  renderBoard();
}
// Marks on the live board: where the hinted move goes (from the second step).
function liveMarks() {
  const hs = hintState;
  if (!hs || !hs.shown || hs.step < 2 || $("hints").hidden || !S || hs.at !== S.history.length || !humanTurn()) return null;
  const p = hs.h.moves[0] && parsePlay(hs.h.moves[0].move);
  if (!p) return null;
  return { spans: [{ p, kind: "hint", tag: hs.step >= 3 ? p.coord : "" }], ghosts: new Map(), coords: [{ p, kind: "hint" }] };
}

// Practice: each of your moves is judged once Tilefish has answered it (so its own thinking
// is not slowed), and the verdict appears beside the board with the better move a tap away.
function rateLastMove() {
  if (!G || G.mode !== "practice" || settings.rate === "0" || !S || G.over) return;
  let k = S.history.length - 1;
  while (k >= 0 && moverOf(k) !== G.human) k--;
  if (k < 0 || verdictAt(k)) return;
  const my = epoch, id = G.id;
  ui("review " + k + " " + secs(0.6)).then((r) => {
    if (my !== epoch || !G || G.id !== id || !r.ok || G.record.length <= k) return;
    G.verdicts = G.verdicts || {};
    G.verdicts[k] = r;
    save();
    renderSide();
    showRating(k);
  }).catch(() => {});
}
function showRating(k) {
  const v = verdictAt(k), box = $("rating");
  if (!v || reviewAt >= 0) { box.hidden = true; return; }
  const cls = classify(v), info = CLASS_INFO[cls];
  let why = "";
  if (v.same) why = cls === "best" ? "Tilefish's choice too." : info.tip;
  else why = "Best was <b>" + esc(moveLabel(v.best)) + "</b>" + (v.bestScore ? " (+" + v.bestScore + ")" : "") +
    (v.exact ? "." : ": " + pp(Math.max(0, v.bestWin - playedWin(v))) + " more winning chance.");
  box.className = "panel rating " + cls;
  box.innerHTML = '<div class="rating-row">' + badge(cls, "lg") + '<div class="rating-text"><div><b>' + esc(moveLabel(v.played)) + "</b> is " + info.say +
    '</div><div class="rating-sub">' + why + '</div></div><button class="link rating-x" aria-label="Dismiss" title="Dismiss">×</button></div>' +
    (v.same ? "" : '<div class="rating-acts"><button class="ctl small" id="rating-see">See it on the board</button></div>');
  box.hidden = false;
  box.querySelector(".rating-x").onclick = () => { box.hidden = true; };
  if ($("rating-see")) $("rating-see").onclick = () => { box.hidden = true; enterReview(k + 1); };
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
  for (const key of Object.keys(G.verdicts || {})) if (+key >= k) delete G.verdicts[key];
  $("rating").hidden = true;
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
$("btn-challenge").onclick = () => challenge();
$("btn-pause").onclick = () => {
  if (!G || !G.clockMin || (!assistAllowed() && !G.paused)) return;  // Play: only a restored clock can be resumed
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
// A game review in the manner of a chess site's: every move is judged with only what its
// player could see (`ui review`), by the winning chance it gave up against Tilefish's
// choice.  From that come a class for each move, an accuracy for each player, a graph of
// the winning chances, and the better move drawn on the board where it should have gone.
// The boards of every position are fetched once, so stepping through a game never waits
// for the engine, even while it is still analysing.
const CLASS_INFO = {
  brilliant: { name: "Brilliant", sym: "!!", say: "brilliant", tip: "The best move, and it gave up 12 or more points of score for a clearly better game." },
  great: { name: "Great", sym: "!", say: "a great move", tip: "The only strong move: every other choice gave up 5% or more." },
  best: { name: "Best", sym: "star", say: "the best move", tip: "Tilefish's own choice." },
  excellent: { name: "Excellent", sym: "thumb", say: "excellent", tip: "Within 1% of the best move's winning chance." },
  good: { name: "Good", sym: "check", say: "good", tip: "Gave up less than 3% winning chance." },
  inaccuracy: { name: "Inaccuracy", sym: "?!", say: "an inaccuracy", tip: "Gave up 3% to 6% winning chance." },
  mistake: { name: "Mistake", sym: "?", say: "a mistake", tip: "Gave up 6% to 12% winning chance." },
  blunder: { name: "Blunder", sym: "??", say: "a blunder", tip: "Gave up 12% or more winning chance (or, in a solved endgame, the result)." },
};
const CLASS_ORDER = Object.keys(CLASS_INFO);
const KEY_CLASSES = new Set(["brilliant", "great", "inaccuracy", "mistake", "blunder"]);
const ICON = {
  star: '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M8 1.4l2 4.2 4.6.6-3.4 3.1.9 4.6L8 11.6l-4.1 2.3.9-4.6-3.4-3.1 4.6-.6z"/></svg>',
  thumb: '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M1.6 7.1h2.6v7.1H1.6zM5.3 7.2 8 1.9c1.3 0 2 .9 1.7 2.1l-.5 2.4h3.9c1 0 1.6.9 1.4 1.8l-1.1 4.7c-.2.8-.9 1.3-1.6 1.3H5.3z"/></svg>',
  check: '<svg viewBox="0 0 16 16" aria-hidden="true"><path d="M3.2 8.6l3 3L12.8 4.8" fill="none" stroke="currentColor" stroke-width="2.6" stroke-linecap="round" stroke-linejoin="round"/></svg>',
};
function badge(cls, extra) {
  const info = CLASS_INFO[cls];
  return '<span class="cls ' + cls + (extra ? " " + extra : "") + '" role="img" title="' + info.name + '" aria-label="' + info.name + '">' + (ICON[info.sym] || esc(info.sym)) + "</span>";
}
const WIN_STEPS = [0.01, 0.03, 0.06, 0.12];  // excellent | good | inaccuracy | mistake | blunder
const POINT_STEPS = [2, 5, 10, 20];          // the same, in points, where the result is settled
const STEP_CLASSES = ["excellent", "good", "inaccuracy", "mistake", "blunder"];
const stepOf = (x, steps) => { let i = 0; while (i < steps.length && x >= steps[i]) i++; return i; };
const num = (x) => (x == null || !isFinite(x) ? null : +x);
// A game all but settled (or a solved endgame) is judged by the points given up instead.
const settled = (v) => v.bestWin >= 0.97 || v.bestWin <= 0.03;
const playedWin = (v) => (num(v.playedWin) != null ? +v.playedWin : v.bestWin - v.winLoss);
function runnerUp(v) {
  let w = null;
  for (const a of v.alts || []) if (a.move !== v.best && num(a.win) != null && (w == null || a.win > w)) w = +a.win;
  return w;
}
function isBrilliant(v) {
  const t = v.top;
  if (!t || t.move === v.played || isExch(v.played) || isPass(v.played)) return false;
  if (t.score - (v.playedScore || 0) < 12) return false;
  if (v.exact) return num(v.playedValue) != null && v.playedValue - t.value >= 10;
  return !settled(v) && playedWin(v) - t.win >= 0.03;
}
function isGreat(v) {
  const w = runnerUp(v);
  if (w == null || (!v.exact && settled(v))) return false;
  return v.bestWin - w >= (v.exact ? 0.5 : 0.05);
}
function classify(v) {
  if (!v) return null;
  if (v.same) return isBrilliant(v) ? "brilliant" : isGreat(v) ? "great" : "best";
  const wl = Math.max(0, +v.winLoss || 0), vl = Math.max(0, +v.valueLoss || 0);
  let s = stepOf(wl, WIN_STEPS);
  if (v.exact) s = wl > 0 ? 4 : stepOf(vl, POINT_STEPS);
  else if (settled(v)) s = Math.max(s, stepOf(vl, POINT_STEPS));
  return STEP_CLASSES[s];
}
// Accuracy as a chess site computes it from the winning chance lost, with Scrabble's
// smaller swings counted double; in a settled game a point of value counts as half a percent.
function accuracyOf(v) {
  let loss = Math.max(0, +v.winLoss || 0);
  if (v.exact ? loss === 0 : settled(v)) loss = Math.max(loss, 0.005 * Math.max(0, +v.valueLoss || 0));
  if (v.same) loss = 0;
  return Math.max(0, Math.min(100, 103.1668 * Math.exp(-0.04354 * 200 * loss) - 3.1669));
}

// Moves as text: "8D WO(R)D" across from 8D, "D8 WORD" down; brackets: already on the board.
const isExch = (t) => /^exch/i.test(t || ""), isPass = (t) => /^pass/i.test(t || "");
function parsePlay(text) {
  const t = (text || "").trim();
  let m = t.match(/^(\d{1,2})([A-O])\s+(\S+)$/i), r, c, down = false;
  if (m) { r = +m[1] - 1; c = COLS.indexOf(m[2].toUpperCase()); } else {
    m = t.match(/^([A-O])(\d{1,2})\s+(\S+)$/i);
    if (!m) return null;
    c = COLS.indexOf(m[1].toUpperCase()); r = +m[2] - 1; down = true;
  }
  const cells = [];
  let through = false;
  for (const ch of m[3]) {
    if (ch === "(") { through = true; continue; }
    if (ch === ")") { through = false; continue; }
    if (r < 0 || c < 0 || r >= N || c >= N) return null;
    cells.push({ sq: r * N + c, ch, fresh: !through && ch !== "." });
    if (down) r++; else c++;
  }
  return cells.length ? { cells, down, coord: t.split(/\s+/)[0].toUpperCase() } : null;
}
function tilesOf(text) {  // the tiles a move takes from the rack, blanks as "?"
  if (!text || isPass(text)) return "";
  if (isExch(text)) return text.replace(/^exch\s*/i, "").toUpperCase();
  const p = parsePlay(text);
  return p ? p.cells.filter((x) => x.fresh).map((x) => (x.ch === x.ch.toLowerCase() ? "?" : x.ch)).join("") : "";
}
function leaveOf(rack, text) {
  const left = (rack || "").split("");
  for (const ch of tilesOf(text)) { const i = left.indexOf(ch); if (i >= 0) left.splice(i, 1); }
  return left.join("");
}
const moveLabel = (t) => (isPass(t) ? "Pass" : isExch(t) ? "Exchange " + t.replace(/^exch\s*/i, "") : pretty(t));
const isBingo = (t) => !isExch(t) && tilesOf(t).length === 7;
// An endgame line (the exact solver's best play from a position), each move with its player.
function lineHtml(line, mover) {
  return '<span class="ln">' + line.map((mv, i) => '<span class="ln-move"><span class="dim">' + esc(sideName((mover + i) % 2)) + "</span> " + esc(moveLabel(mv)) + "</span>")
    .join(' <span class="ln-sep" aria-hidden="true">›</span> ') + "</span>";
}
const sgn = (x) => (x > 0 ? "+" : x < 0 ? "−" : "") + Math.abs(Math.round(x));
const pp = (x) => (100 * x).toFixed(Math.abs(x) < 0.1 ? 1 : 0) + "%";

let rvBoards = null, rvKey = "";  // the board after each number of moves
let exploring = null;             // trying your own move in a reviewed position: {k, result, judging}
let preview = null;               // a move shown on the board in place of the one played: {move, pinned}
let analysing = null;             // {done, total} while the analysis runs
let classesOpen = true;
const moverOf = (k) => (S.history[k].who === "you" ? S.seat : 1 - S.seat);
const verdictAt = (k) => (G && G.verdicts ? G.verdicts[k] : null);
const shownVerdict = () => (reviewAt > 0 ? verdictAt(reviewAt - 1) : null);
function judgedSides() { return G.over || G.mode === "review" || G.mode === "duo" ? [0, 1] : [G.human]; }
function reviewSecs() { return +store.get("reviewSecs", "1") || 1; }

async function reviewBoards() {
  const key = G.id + ":" + S.history.length + ":" + (G.gcg ? G.gcg.length : 0);
  if (rvBoards && rvKey === key) return;
  const out = [];
  for (let k = 0; k <= S.history.length; k++) {
    const r = await ui("at " + k);
    if (!r.ok) return;
    out.push(r.board);
  }
  rvBoards = out;
  rvKey = key;
}
async function enterReview(n, opts) {
  if (!S) return;
  if (!reviewable()) { toast("Moves can be reviewed when the game is over."); return; }
  const key = G.id + ":" + S.history.length + ":" + (G.gcg ? G.gcg.length : 0);
  if (!rvBoards || rvKey !== key) {
    if (busy) return;
    await reviewBoards();
    if (!rvBoards) return;
  }
  const was = reviewAt;
  if (exploring) { rack = exploring.saved; exploring = null; pending = new Map(); order = []; checked = null; }
  reviewAt = Math.max(0, Math.min(n, S.history.length));
  if (!(opts && opts.keepPreview)) preview = null;
  if (was < 0) { recall(); cursor = null; }
  showReviewBoard(was);
  render();
  renderReviewPanel();
  scrollLogTo(reviewAt);
  // A Practice game arrives with only your own moves rated (as you played them): the
  // review judges the rest and keeps those ratings, so they read as they did in the game.
  if (opts && opts.analyse && unjudged().length) analyseGame(reviewSecs(), Object.keys(G.verdicts || {}).length > 0);
}
// The moves the review should judge (every move once the game is over) that have no verdict.
function unjudged() {
  const sides = judgedSides();
  return S.history.map((h, k) => k).filter((k) => sides.includes(moverOf(k)) && !verdictAt(k));
}
function showReviewBoard(was) {
  shownBoard = rvBoards[reviewAt];
  last = new Set();
  if (reviewAt > 0) {
    const prev = rvBoards[reviewAt - 1];
    for (let s = 0; s < N * N; s++) if (shownBoard[Math.floor(s / N)][s % N] !== "." && prev[Math.floor(s / N)][s % N] === ".") last.add(s);
    lastMine = humanSide(moverOf(reviewAt - 1)) && G.mode !== "duo" && G.mode !== "review" ? true : moverOf(reviewAt - 1) === 0;
  }
  rvFresh = was >= 0 && reviewAt === was + 1 ? last : null;  // stepping forward drops the tiles in
}
let rvFresh = null;
function leaveReview() {
  if (exploring) { rack = exploring.saved; exploring = null; pending = new Map(); order = []; checked = null; }
  reviewAt = -1;
  preview = null;
  shownBoard = S.board;
  last = new Set();
  $("review").hidden = true;
  $("evalbar").hidden = true;
  render();
}
function reviewStatus() {
  if (reviewAt === 0) return "The empty board.";
  const h = S.history[reviewAt - 1];
  const v = verdictAt(reviewAt - 1), cls = classify(v);
  const text = h.type === "play" ? pretty(h.move) : historyLabel(h, reviewAt - 1);
  let s = (cls ? badge(cls, "sm") + " " : "") + "<b>" + esc(sideName(moverOf(reviewAt - 1))) + "</b>: " + esc(text) + (h.type === "play" ? ", +" + h.score : "");
  if (preview) s += ' <span class="dim">· showing ' + esc(moveLabel(preview.move)) + " instead</span>";
  return s;
}
function reviewable() { return G && (G.over || G.mode === "review" || assistAllowed()); }

// What the board shows in review: the move played (outlined in its class's colour, with
// its badge) and, when Tilefish preferred another, that move where it should have gone
// (dashed, with its coordinate and an arrow for its direction).  A previewed move is shown
// on the board before the move, in full.
function reviewMarks() {
  if (exploring) {
    const r = exploring.result, b = r && !r.same && parsePlay(r.best);
    if (!b) return null;
    const marks = { spans: [{ p: b, kind: "best", tag: "Best " + b.coord }], ghosts: new Map(), coords: [{ p: b, kind: "best" }] };
    for (const c of b.cells) if (c.fresh) marks.ghosts.set(c.sq, { ch: c.ch, faint: true });
    return marks;
  }
  if (reviewAt <= 0 || !rvBoards) return null;
  const v = shownVerdict(), cls = classify(v);
  const h = S.history[reviewAt - 1];
  const marks = { spans: [], ghosts: new Map(), coords: [] };
  const played = h.type === "play" ? parsePlay(v ? v.played : h.move) : null;
  if (preview) {
    const p = parsePlay(preview.move);
    if (p) {
      for (const c of p.cells) if (c.fresh) marks.ghosts.set(c.sq, { ch: c.ch, faint: false });
      marks.spans.push({ p, kind: "best preview", tag: p.coord });
      marks.coords.push({ p, kind: "best" });
    }
    if (played) marks.spans.push({ p: played, kind: "played ghost", cls });
    return marks;
  }
  if (played) {
    marks.spans.push({ p: played, kind: "played", cls, badge: cls });
    marks.coords.push({ p: played, kind: "played", cls });
  }
  if (v && !v.same && classify(v) !== "excellent") {
    const b = parsePlay(v.best);
    if (b) {
      for (const c of b.cells) if (c.fresh && !filled(c.sq)) marks.ghosts.set(c.sq, { ch: c.ch, faint: true });
      marks.spans.push({ p: b, kind: "best", tag: "Best " + b.coord });
      marks.coords.push({ p: b, kind: "best" });
    }
  }
  return marks;
}
function drawReviewMarks(marks) {
  boardEl.querySelectorAll(".rv-span").forEach((e) => e.remove());
  for (const el of document.querySelectorAll("#coords-top span, #coords-left span")) { el.className = ""; el.style.removeProperty("--c"); }
  if (!marks) return;
  for (const [sq, g] of marks.ghosts) {
    if (occupied(sq)) continue;
    squares[sq].textContent = "";
    squares[sq].appendChild(tileEl(g.ch, "suggest" + (g.faint ? " faint" : "")));
  }
  for (const s of marks.spans) {
    const first = s.p.cells[0].sq, len = s.p.cells.length;
    const r = Math.floor(first / N), c = first % N;
    const d = document.createElement("div");
    d.className = "rv-span " + s.kind + (s.p.down ? " down" : " across");
    d.style.gridRow = r + 1 + " / span " + (s.p.down ? len : 1);
    d.style.gridColumn = c + 1 + " / span " + (s.p.down ? 1 : len);
    if (s.cls) d.style.setProperty("--c", "var(--c-" + s.cls + ")");
    if (s.tag) {
      const t = document.createElement("span");
      t.className = "rv-tag " + (s.p.down ? (c > 0 ? "left" : "right") : r > 0 ? "above" : "below");
      t.textContent = s.tag;
      d.appendChild(t);
    }
    if (s.badge) {
      const b = document.createElement("span");
      b.className = "rv-badge";
      b.innerHTML = badge(s.badge);
      d.appendChild(b);
    }
    boardEl.appendChild(d);
  }
  const top = $("coords-top").children, left = $("coords-left").children;
  for (const m of marks.coords) {
    const sq = m.p.cells[0].sq, r = Math.floor(sq / N), c = sq % N;
    for (const el of [top[c], left[r]]) {
      el.className = "hl " + m.kind;
      if (m.cls) el.style.setProperty("--c", "var(--c-" + m.cls + ")");
    }
  }
}

// The winning chance of the side on the left-hand card (you, or player 1) after each
// number of moves: from the judged position before a move where there is one (it knows
// the rack drawn), else from the estimate after the move before it.
function winSeries() {
  const n = S.history.length, A = cardSide(0);
  const pts = new Array(n + 1).fill(null);
  const forA = (w, side) => (side === A ? w : 1 - w);
  const ks = Object.keys(G.verdicts || {}).map(Number).filter((k) => k < n);
  for (const k of ks) pts[k + 1] = forA(playedWin(G.verdicts[k]), moverOf(k));
  for (const k of ks) pts[k] = forA(G.verdicts[k].bestWin, moverOf(k));
  const R = G.result;
  if (R && G.over) pts[n] = R.winner < 0 ? 0.5 : R.winner === A ? 1 : 0;
  else if (S.over) { const a = sideScore(A), b = sideScore(1 - A); pts[n] = a > b ? 1 : a < b ? 0 : 0.5; }
  if (pts[0] == null && ks.length) pts[0] = 0.5;
  return pts.map((p) => (p == null ? null : Math.max(0, Math.min(1, p))));
}
function seriesAt(pts, k) {
  for (let i = k; i >= 0; i--) if (pts[i] != null) return pts[i];
  return null;
}
function renderEvalBar(pts) {
  const bar = $("evalbar");
  const p = reviewAt >= 0 && pts ? seriesAt(pts, reviewAt) : null;
  bar.hidden = p == null;
  if (p == null) return;
  $("evalbar-fill").style.height = (100 * p).toFixed(1) + "%";
  const lbl = $("evalbar-label");
  lbl.textContent = Math.round(100 * (p >= 0.5 ? p : 1 - p));
  lbl.className = "evalbar-label " + (p >= 0.5 ? "lo" : "hi");
  bar.title = sideName(cardSide(0)) + ": " + pct(p) + " to win here";
}
function graphSvg(pts) {
  const n = S.history.length, W = Math.max(200, $("review").clientWidth - 24 || 300), H = 86;
  const x = (k) => (n ? (k / n) * W : 0), y = (p) => 3 + (1 - p) * (H - 6);
  const known = pts.map((p, k) => [k, p]).filter(([, p]) => p != null);
  let svg = '<svg class="wgraph" viewBox="0 0 ' + W + " " + H + '" width="' + W + '" height="' + H + '" role="img" aria-label="Winning chances through the game">';
  svg += '<rect class="wg-b" x="0" y="0" width="' + W + '" height="' + H + '"/>';
  if (known.length) {
    const line = known.map(([k, p]) => x(k).toFixed(1) + "," + y(p).toFixed(1)).join(" L");
    svg += '<path class="wg-a" d="M' + x(known[0][0]).toFixed(1) + "," + H + " L" + line + " L" + x(known[known.length - 1][0]).toFixed(1) + "," + H + 'Z"/>';
    svg += '<path class="wg-line" d="M' + line + '"/>';
  }
  svg += '<line class="wg-mid" x1="0" x2="' + W + '" y1="' + y(0.5) + '" y2="' + y(0.5) + '"/>';
  if (reviewAt >= 0) svg += '<line class="wg-at" x1="' + x(reviewAt) + '" x2="' + x(reviewAt) + '" y1="0" y2="' + H + '"/>';
  for (let k = 0; k < n; k++) {
    const cls = classify(verdictAt(k));
    if (!cls || !KEY_CLASSES.has(cls)) continue;
    const p = seriesAt(pts, k + 1);
    if (p == null) continue;
    svg += '<circle class="wg-dot" style="fill:var(--c-' + cls + ')" cx="' + x(k + 1).toFixed(1) + '" cy="' + y(p).toFixed(1) + '" r="3.6"><title>' +
      esc(k + 1 + ". " + sideName(moverOf(k)) + ": " + historyLabel(S.history[k], k) + " (" + CLASS_INFO[cls].name + ")") + "</title></circle>";
  }
  return svg + "</svg>";
}
function renderReviewPanel() {
  if (reviewAt < 0) return;
  const box = $("review");
  box.hidden = false;
  const verdicts = G.verdicts || {};
  const judged = Object.keys(verdicts).length;
  const pts = winSeries();
  renderEvalBar(pts);
  let html = '<div class="panel-head"><span>Game review</span><button class="link" id="rv-close">' + (G.over || G.mode === "review" ? "Final position" : "Back to the game") + "</button></div>";
  html += '<div class="review-body">';
  // the move on the board
  const k = reviewAt - 1, v = k >= 0 ? verdictAt(k) : null;
  if (exploring) html += exploreHtml();
  else if (v) html += coachHtml(v, k);
  else if (k >= 0 && analysing) html += '<div class="coach pending">Judging this move…</div>';
  else if (k >= 0 && G.noVerdict && G.noVerdict[k]) html += '<div class="coach pending">This move cannot be judged: ' + esc(G.noVerdict[k]) + ".</div>";
  else if (k >= 0 && judged) html += '<div class="coach pending">This move was not judged' + (judgedSides().includes(moverOf(k)) ? "." : " (only your own moves are judged until the game is over).") + "</div>";
  // the analysis
  if (analysing) {
    html += '<div class="rv-progress"><div class="loading-bar"><div class="loading-fill" style="width:' + (100 * analysing.done / Math.max(1, analysing.total)).toFixed(1) +
      '%"></div></div><div class="rv-progress-row"><span>Analysing move ' + Math.min(analysing.done + 1, analysing.total) + " of " + analysing.total + '</span><button class="link strong" id="rv-stop">Stop</button></div></div>';
  } else if (!judged && G.noVerdict && Object.keys(G.noVerdict).length) {
    const why = Object.values(G.noVerdict);
    html += '<p class="rv-intro">No move could be judged: ' + (why.every((w) => /rack/.test(w)) ? "the record does not give the racks the moves were played from" : esc(why[0])) + ".</p>";
  } else if (!judged) {
    html += '<p class="rv-intro">Tilefish judges every move with only what its player could see, marks the best and the costly ones, and shows on the board where the better move would have gone.</p>';
  }
  if (judged) html += '<div class="wgraph-wrap" id="rv-graph">' + graphSvg(pts) + "</div>";
  if (judged) html += summaryHtml();
  if (!analysing) {
    const secs = reviewSecs();
    html += '<div class="rv-start"><div class="seg rv-depth" role="radiogroup" aria-label="Depth">' +
      [[0.3, "Quick"], [1, "Standard"], [3, "Deep"]].map(([s, l]) => '<button data-secs="' + s + '" role="radio" aria-checked="' + (s === secs) + '" class="' + (s === secs ? "on" : "") + '">' + l + "</button>").join("") +
      '</div><button class="ctl primary" id="rv-go">' + (judged ? "Analyse again" : "Start review") + "</button></div>";
  }
  html += "</div>";
  box.innerHTML = html;
  $("rv-close").onclick = leaveReview;
  if ($("rv-stop")) $("rv-stop").onclick = stopAnalysis;
  if ($("rv-go")) $("rv-go").onclick = () => analyseGame(reviewSecs());
  box.querySelectorAll(".rv-depth button").forEach((b) => (b.onclick = () => { store.set("reviewSecs", b.dataset.secs); renderReviewPanel(); }));
  const graph = $("rv-graph");
  if (graph) graph.onclick = (e) => {
    const r = graph.firstChild.getBoundingClientRect();
    enterReview(Math.round(((e.clientX - r.left) / r.width) * S.history.length));
  };
  if (exploring) wireExplore(); else wireCoach(v, k);
  const det = $("rv-classes");
  if (det) det.ontoggle = () => { classesOpen = det.open; };
  box.querySelectorAll("[data-jump]").forEach((b) => (b.onclick = () => jumpToClass(b.dataset.jump, +b.dataset.side)));
}
const possessive = (side) => (sideName(side) === "You" ? "Your" : sideName(side) + "'s");
function coachHtml(v, k, opts) {
  const cls = classify(v), info = CLASS_INFO[cls], side = moverOf(k);
  const lines = [];
  const bs = v.bestScore || 0, ps = v.playedScore || 0;
  const lb = leaveOf(v.rack, v.best), lp = leaveOf(v.rack, v.played);
  const keeps = (a, b) => "keeps " + (a || "nothing") + " instead of " + (b || "nothing");
  if (cls === "brilliant") {
    lines.push("It scores " + (v.top.score - ps) + " less than " + esc(moveLabel(v.top.move)) + " (+" + v.top.score + ") but keeps a better game: " +
      (v.exact ? sgn(v.playedValue - v.top.value) + " points by the end." : pp(playedWin(v) - v.top.win) + " more winning chance."));
  } else if (cls === "great") {
    lines.push("The only strong move here: the next best gives up " + (v.exact ? "the result." : pp(v.bestWin - runnerUp(v)) + " winning chance."));
  } else if (v.same) {
    lines.push("Tilefish's choice too.");
  } else {
    lines.push("Best was <b>" + esc(moveLabel(v.best)) + "</b>" + (bs ? " (+" + bs + ")" : "") + ".");
    if (isBingo(v.best) && !isBingo(v.played)) lines.push("That one is a bingo: all seven tiles, for the 50-point bonus.");
    else if (isExch(v.best) && !isExch(v.played)) lines.push("Exchanging was better: it keeps " + (lb || "nothing") + " and draws fresh tiles.");
    else if (isExch(v.played) && !isExch(v.best)) lines.push("Playing was better than exchanging: it scores " + bs + " and " + keeps(lb, lp) + ".");
    else if (isPass(v.played)) lines.push("Passing gave away a turn.");
    else if (bs > ps) lines.push("It scores " + (bs - ps) + " more" + (lb !== lp ? " and " + keeps(lb, lp) : "") + ".");
    else if (bs < ps) lines.push("It scores " + (ps - bs) + " less, but " + (lb !== lp ? keeps(lb, lp) : "leaves a better board") + ".");
    else if (lb !== lp) lines.push("The same score, but it " + keeps(lb, lp) + ".");
    else if (parsePlay(v.best) && parsePlay(v.played) && parsePlay(v.best).coord === parsePlay(v.played).coord) lines.push("The same squares, score and tiles kept, but its word leaves the opponent less to play with.");
    else lines.push("The same score and tiles kept, on a spot that leaves the opponent less.");
  }
  let chance;
  if (v.exact) {
    const best = num(v.playedValue) != null ? v.playedValue + (+v.valueLoss || 0) : null;
    chance = "Endgame, solved: best play gains " + sgn(best) + " from here" + (v.same ? "." : ", this move " + sgn(v.playedValue) + ".");
  } else if (v.same) {
    chance = possessive(side) + " winning chance: " + pct(v.bestWin) + ".";
  } else {
    chance = possessive(side) + " winning chance: " + pct(v.bestWin) + " with the best move, " + pct(playedWin(v)) + " after this one" +
      (+v.valueLoss >= 0.5 ? " (" + (+v.valueLoss).toFixed(1) + " points of value)" : "") + ".";
  }
  let html = '<div class="coach ' + cls + '"><div class="coach-head">' + badge(cls, "lg") + "<div><b>" + esc(moveLabel(v.played)) + "</b> is " + info.say +
    '<div class="coach-sub">' + (opts && opts.explore ? "your try" : esc(sideName(side)) + " · move " + (k + 1) + " · rack " + esc(v.rack.replace(/\?/g, "·"))) + "</div></div></div>";
  html += '<p class="coach-text">' + lines.join(" ") + '</p><p class="coach-chance">' + esc(chance) + "</p>";
  if (v.exact && v.line && v.line.length) {
    html += '<p class="coach-line"><span class="lbl">Best play to the end:</span> ' + lineHtml(v.line, side) + "</p>";
    if (!v.same && v.playedLine && v.playedLine.length > 1) html += '<p class="coach-line"><span class="lbl">After the move played:</span> ' + lineHtml(v.playedLine, side) + "</p>";
  }
  // the moves Tilefish considered, the one played among them
  const alts = (v.alts || []).slice(0, 5);
  const bestValue = (num(v.playedValue) || 0) + (+v.valueLoss || 0);
  const rows = alts.map((a) => ({ a, played: a.move === v.played }));
  if (!rows.some((r) => r.played)) rows.push({ a: { move: v.played, score: ps, win: playedWin(v), value: v.playedValue }, played: true, extra: true });
  html += '<ol class="alts2">';
  for (const { a, played, extra } of rows) {
    const acls = a.move === v.best ? "best" : played ? cls : STEP_CLASSES[v.exact ? (a.win < v.bestWin ? 4 : stepOf(Math.max(0, bestValue - a.value), POINT_STEPS)) : stepOf(Math.max(0, v.bestWin - a.win), WIN_STEPS)];
    const on = preview && preview.move === a.move;
    html += '<li class="' + (played ? "played" : "") + (extra ? " extra" : "") + (on ? " on" : "") + '"><button data-alt="' + esc(a.move) + '" title="Show this move on the board">' + badge(acls, "sm") +
      '<span class="mv">' + esc(moveLabel(a.move)) + "</span>" + '<span class="sc">' + (a.score ? "+" + a.score : "") + "</span>" +
      '<span class="wp">' + (v.exact ? sgn(a.value) : num(a.win) != null ? pct(a.win) : "") + "</span>" +
      '<span class="sub">' + (played ? "played · " : "") + "keeps " + esc(leaveOf(v.rack, a.move).replace(/\?/g, "·") || "nothing") + "</span></button></li>";
  }
  html += "</ol>";
  if (opts && opts.explore) return html + "</div>";
  html += '<div class="coach-acts">';
  if (!v.same) html += '<button class="ctl" id="rv-best" title="Show the best move on the board (B)">' + (preview && preview.move === v.best ? "Show played" : "Show best") + "</button>";
  if (rvBoards) html += '<button class="ctl" id="rv-try" title="Set out your own move in this position and have it judged">Try a move</button>';
  if (reviewAt > 0 && reviewAt <= G.record.length && !G.gcg && !G.cgp) html += '<button class="ctl" id="rv-retry" title="Play from the position before this move, with the same tiles to come">Retry</button>';
  if (rvBoards && !analysing) html += '<button class="ctl" id="rv-analyse" title="Open the position before this move in Analyse, where you can change it, get hints and play on">Analyse from here</button>';
  html += '<span class="ctl-gap"></span><span class="keynav"><button class="ctl" id="rv-kprev" title="Previous key move (↑)" aria-label="Previous key move">‹</button><span class="dim">Key</span><button class="ctl" id="rv-knext" title="Next key move (↓)" aria-label="Next key move">›</button></span></div>';
  return html + "</div>";
}
function wireCoach(v, k) {
  const box = $("review");
  if ($("rv-best")) $("rv-best").onclick = () => togglePreview(v.best);
  if ($("rv-retry")) $("rv-retry").onclick = () => tryAgain(reviewAt - 1);
  if ($("rv-analyse")) $("rv-analyse").onclick = () => analyseFromReview(k);
  if ($("rv-try")) $("rv-try").onclick = () => startExplore(k);
  if ($("rv-kprev")) { $("rv-kprev").onclick = () => jumpKey(-1); $("rv-kprev").disabled = keyMove(-1) < 0; }
  if ($("rv-knext")) { $("rv-knext").onclick = () => jumpKey(1); $("rv-knext").disabled = keyMove(1) < 0; }
  box.querySelectorAll("[data-alt]").forEach((b) => {
    const mv = b.dataset.alt;
    b.onclick = () => togglePreview(mv);
    // Pointing at a move shows it on the board at once; it stays only when clicked.
    b.onpointerenter = (e) => { if (e.pointerType === "mouse" && !(preview && preview.pinned)) setPreview(mv === v.played ? null : { move: mv, pinned: false }); };
    b.onpointerleave = (e) => { if (e.pointerType === "mouse" && preview && !preview.pinned) setPreview(null); };
  });
}
// The analysis board: the position before a move, with the mover's rack, where any move
// can be set out and judged as the review judges the one played (`ui judge`).
// The position before move k, with its mover's rack and both scores, in the board editor
// (Analyse), from where it can be changed, analysed and played on.
function analyseFromReview(k) {
  const v = verdictAt(k);
  if (!rvBoards || !rvBoards[k]) return;
  const side = moverOf(k), score = [0, 0], seen = [false, false];
  for (let i = k - 1; i >= 0 && !(seen[0] && seen[1]); i--) {
    const s = moverOf(i);
    if (!seen[s]) { score[s] = S.history[i].total; seen[s] = true; }
  }
  openSetup({ board: rvBoards[k], rack: v ? v.rack : "", you: score[side], them: score[1 - side] });
}
function startExplore(k) {
  const v = verdictAt(k);
  if (!v || !rvBoards) return;
  preview = null;
  exploring = { k, result: null, judging: false, saved: rack };
  shownBoard = rvBoards[k];
  last = new Set();
  rack = rackFrom({ rack: v.rack }, []);
  pending = new Map(); order = []; checked = null; cursor = null; selected = -1; exchanging = false;
  render();
  renderReviewPanel();
}
function exitExplore() {
  if (!exploring) return;
  rack = exploring.saved;
  exploring = null;
  pending = new Map(); order = []; checked = null; cursor = null;
  showReviewBoard(-1);
  render();
  renderReviewPanel();
}
async function judgeTry() {
  const mv = buildMove();
  if (!exploring || exploring.judging || !mv || mv.error || !checked || !checked.ok || checked.text !== mv.text) return;
  const ex = exploring;
  ex.judging = true;
  renderControls();
  renderReviewPanel();
  let r;
  try { r = await ui("judge " + ex.k + " " + mv.text + " " + secs(Math.max(0.6, reviewSecs()))); } catch (e) { ex.judging = false; return; }
  ex.judging = false;
  if (exploring !== ex) return;
  ex.result = r.ok ? r : null;
  if (!r.ok) toast(capital(r.error));
  else if (KEY_CLASSES.has(classify(r)) && !["brilliant", "great"].includes(classify(r))) sfx.bad(); else sfx.good();
  render();
  renderReviewPanel();
}
function exploreHtml() {
  const ex = exploring, v = verdictAt(ex.k);
  let html = '<div class="coach explore"><div class="coach-head"><span class="explore-ico" aria-hidden="true">✎</span><div><b>Try a move</b>' +
    '<div class="coach-sub">' + esc(sideName(moverOf(ex.k))) + "'s position before move " + (ex.k + 1) + " · rack " + esc(v.rack.replace(/\?/g, "·")) + "</div></div></div>";
  if (ex.judging) html += '<p class="coach-text">Judging…</p>';
  else if (!ex.result) html += '<p class="coach-text">Set out tiles from the rack below, then press <b>Judge</b> (or Enter). Played in the game: <b>' + esc(moveLabel(v.played)) + "</b> (" + CLASS_INFO[classify(v)].name.toLowerCase() + ").</p>";
  html += '<div class="coach-acts"><button class="ctl" id="ex-back">Back to the review</button></div></div>';
  if (ex.result) html += coachHtml(ex.result, ex.k, { explore: true });
  return html;
}
function wireExplore() {
  if ($("ex-back")) $("ex-back").onclick = exitExplore;
}
function setPreview(p) {
  if (reviewAt <= 0) return;
  preview = p && !isPass(p.move) ? p : null;
  shownBoard = preview && !isExch(preview.move) ? rvBoards[reviewAt - 1] : rvBoards[reviewAt];
  rvFresh = null;
  renderBoard();
  renderRack();
  statusForTurn();
  const box = $("review");
  box.querySelectorAll(".alts2 li").forEach((li) => li.classList.toggle("on", !!preview && li.firstChild.dataset.alt === preview.move));
  const b = $("rv-best"), v = shownVerdict();
  if (b && v) b.textContent = preview && preview.move === v.best ? "Show played" : "Show best";
}
function togglePreview(mv) {
  const v = shownVerdict();
  if (!v) return;
  if ((preview && preview.pinned && preview.move === mv) || mv === v.played) setPreview(null);
  else setPreview({ move: mv, pinned: true });
}
function keyMove(dir) {
  const sides = judgedSides();
  for (let k = reviewAt - 1 + dir; k >= 0 && k < S.history.length; k += dir) {
    const cls = classify(verdictAt(k));
    if (cls && KEY_CLASSES.has(cls) && sides.includes(moverOf(k))) return k;
  }
  return -1;
}
function jumpKey(dir) { const k = keyMove(dir); if (k >= 0) enterReview(k + 1); }
function jumpToClass(cls, side) {
  const n = S.history.length, ks = [];
  for (let k = 0; k < n; k++) if (moverOf(k) === side && classify(verdictAt(k)) === cls) ks.push(k);
  if (!ks.length) return;
  const next = ks.find((k) => k > reviewAt - 1);
  enterReview((next != null ? next : ks[0]) + 1);
}
function summaryHtml() {
  const sides = [cardSide(0), cardSide(1)].filter((s) => judgedSides().includes(s));
  const stats = sides.map((side) => {
    const ks = S.history.map((h, k) => k).filter((k) => moverOf(k) === side);
    const vs = ks.map((k) => verdictAt(k)).filter(Boolean);
    const counts = {};
    for (const v of vs) { const c = classify(v); counts[c] = (counts[c] || 0) + 1; }
    const acc = vs.length ? vs.reduce((a, v) => a + accuracyOf(v), 0) / vs.length : null;
    const value = vs.length ? vs.reduce((a, v) => a + Math.max(0, +v.valueLoss || 0), 0) / vs.length : null;
    const bingos = ks.filter((k) => S.history[k].tiles === 7).length;
    const pts = ks.reduce((a, k) => a + (S.history[k].score || 0), 0);
    return { side, counts, acc, value, bingos, avg: ks.length ? pts / ks.length : 0, n: vs.length };
  });
  let html = '<div class="acc-row">';
  for (const s of stats) {
    html += '<div class="acc ' + (s.side === cardSide(0) ? "a" : "b") + '"><span class="who">' + esc(sideName(s.side)) + '</span><span class="acc-n">' +
      (s.acc == null ? "–" : s.acc.toFixed(1)) + '</span><span class="acc-sub">accuracy · ' + s.bingos + " bingo" + (s.bingos === 1 ? "" : "s") + " · " +
      s.avg.toFixed(1) + " a move" + (s.value != null ? " · " + s.value.toFixed(1) + " value lost a move" : "") + "</span></div>";
  }
  html += "</div>";
  html += '<details class="classes" id="rv-classes"' + (classesOpen ? " open" : "") + '><summary>Move classes</summary><table class="cls-table"><thead><tr>' +
    stats.map((s, i) => '<th class="cnt ' + (i ? "r" : "l") + '">' + esc(sideName(s.side)) + "</th>" + (i ? "" : "<th></th>")).join("") + (stats.length === 1 ? "<th></th>" : "") + "</tr></thead><tbody>";
  for (const c of CLASS_ORDER) {
    html += '<tr title="' + esc(CLASS_INFO[c].tip) + '">';
    stats.forEach((s, i) => {
      const n = s.counts[c] || 0;
      const cell = '<td class="cnt ' + (i ? "r" : "l") + '">' + (n ? '<button class="link" data-jump="' + c + '" data-side="' + s.side + '" title="Show ' + esc(possessive(s.side)) + " " + esc(CLASS_INFO[c].name.toLowerCase()) + ' moves">' + n + "</button>" : '<span class="zero">0</span>') + "</td>";
      if (i === 0) html += cell + '<td class="cname">' + badge(c, "sm") + " " + CLASS_INFO[c].name + "</td>";
      else html += cell;
    });
    if (stats.length === 1) html += "<td></td>";
    html += "</tr>";
  }
  return html + "</tbody></table></details>";
}
async function analyseGame(sec, keep) {
  if (!S || busy) return;
  if (G.gcg == null && !G.record.length) return;
  await reviewBoards();
  const my = epoch;
  busy = true;
  if (!keep) G.verdicts = {};
  G.noVerdict = {};  // why a move could not be judged (an imported record without its racks)
  preview = null;
  const todo = unjudged();
  analysing = { done: 0, total: todo.length };
  render();
  renderReviewPanel();
  for (let i = 0; i < todo.length; i++) {
    let r;
    try { r = await ui("review " + todo[i] + " " + secsFor(sec)); } catch (e) { return; }
    if (my !== epoch) return;
    if (r.ok) G.verdicts[todo[i]] = r; else G.noVerdict[todo[i]] = r.error;
    analysing.done = i + 1;
    if (reviewAt >= 0) { renderBoard(); renderSide(); renderReviewPanel(); statusForTurn(); }
  }
  analysing = null;
  busy = false;
  if (G.over || G.mode === "review") { archiveUpdate(); statsAccuracy(); }
  render();
  renderReviewPanel();
}
async function stopAnalysis() {
  const at = reviewAt;
  analysing = null;
  const st = await restartEngine();
  if (!st) return;
  busy = false;
  S = st;
  shownBoard = st.board;
  if (at >= 0) enterReview(at); else render();
}
function archiveUpdate() {
  const list = store.json("games", []);
  const i = list.findIndex((g) => g.id === G.id);
  if (i >= 0) { list[i].game = snapshotForSave(); store.put("games", list); }
}
function scrollLogTo(n) {  // within the list only, never the page
  const log = $("log"), li = log.children[n - 1];
  if (!li) return;
  if (li.offsetTop < log.scrollTop) log.scrollTop = li.offsetTop - 4;
  else if (li.offsetTop + li.offsetHeight > log.scrollTop + log.clientHeight) log.scrollTop = li.offsetTop + li.offsetHeight - log.clientHeight + 4;
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
  preview = null;
  rvBoards = null;
  $("review").hidden = true;
  $("evalbar").hidden = true;
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
let rvResize = 0;
window.addEventListener("resize", () => {
  cancelAnimationFrame(rvResize);
  rvResize = requestAnimationFrame(() => { if (reviewAt >= 0 && !$("review").hidden) { const g = $("rv-graph"); if (g) g.innerHTML = graphSvg(winSeries()); } });
});

// ---------- puzzles ----------
// "Find the best move": a position from a game the static player plays against itself,
// kept when its best move is worth finding (a bingo, or clearly better than the obvious
// highest-scoring play).  The daily puzzle comes from the date, so everyone with the same
// word list gets the same one.  Your move is judged as the review judges a move; within
// 1% of the best counts as solved, and you get three tries.
const dayKey = (d) => d.getFullYear() + "-" + String(d.getMonth() + 1).padStart(2, "0") + "-" + String(d.getDate()).padStart(2, "0");
const daySeed = (key) => 4000000 + Math.round(Date.parse(key + "T00:00:00Z") / 86400000) * 8;
const egDaySeed = (key) => 9000000 + Math.round(Date.parse(key + "T00:00:00Z") / 86400000) * 64;
// The log keeps a day's move puzzle under its date and its endgame under "eg:" and the date.
const dailyKey = (day, kind) => (kind === "endgame" ? "eg:" + day : day);
const SOLVED = new Set(["brilliant", "great", "best", "excellent"]);
function puzzleLog() { return store.json("puzzles", {}); }
function puzzleStreak(kind) {
  const log = puzzleLog();
  const solved = (d) => { const e = log[dailyKey(dayKey(d), kind)]; return e && e.solved && !e.late; };  // an earlier day's puzzle solved later does not count
  const d = new Date();
  if (!solved(d)) d.setDate(d.getDate() - 1);
  let n = 0;
  while (solved(d)) { n++; d.setDate(d.getDate() - 1); }
  return n;
}
async function findPuzzle(base) {
  let chosen = null;
  for (let a = 0; a < 5; a++) {
    const seed = base + a;
    let r = await ui("new first " + seed);
    const plies = 7 + (seed % 9);
    for (let i = 0; i < plies && r.ok && !r.state.over; i++) {
      await ui("seat " + (1 - r.state.turn));  // `bot` moves for the side not seated
      r = await ui("bot static");
    }
    if (!r.ok || r.state.over || r.state.bag < 8) continue;
    const turn = r.state.turn;
    await ui("seat " + turn);
    const st = await ui("moves 40");
    if (!st.ok || !st.moves.length) continue;
    const plays = st.moves.filter((m) => !isExch(m.move) && !isPass(m.move));
    const top = plays[0], topScore = plays.length ? Math.max(...plays.map((m) => m.score)) : 0;
    const good = top && !isExch(st.moves[0].move) && (top.tiles === 7 || topScore - top.score >= 8);
    if (!good && a < 4) continue;
    chosen = { seed, turn, moves: (await ui("record")).moves };
    break;
  }
  if (!chosen) return null;
  const h = await ui("hint " + secs(1.2));
  if (!h.ok || !h.hint.moves.length) return null;
  chosen.hint = h.hint;
  return chosen;
}
// Endgame puzzles: a static game played on past the empty bag until the two racks hold
// 11 tiles or fewer (no blanks), so the exact solver usually settles it in well under a
// second; a position it has not solved in 1.2 s is passed over.  Kept when the best play is
// the only one that good and the obvious move, the highest-scoring one, ends at least 4
// points worse; else the one where the obvious move costs most of those tried (up to 24,
// or 4 s once one is found), a single best play breaking ties.
// The daily endgame comes from the date, so every device has to choose the same one: its
// seeds are tried in a fixed order with no clock, and each position is solved with a budget
// in search nodes, not seconds (`hint 0 nodes=N`: one thread, an empty table), so which
// positions are solved, and their values, are the same everywhere; the choice rests on
// those values alone.  Once chosen it is kept on the device (DAILY_EG_VERSION names the
// rule, so a puzzle chosen by an older one is not mixed with a newer one's).
const DAILY_EG_TILES = 10, DAILY_EG_NODES = 100000, DAILY_EG_VERSION = 1;
function savedDailyEndgame(lex, day) {
  const e = store.json("dailyEndgames", {})[DAILY_EG_VERSION + " " + lex + " " + day];
  return e && e.seed != null && Array.isArray(e.moves) && e.hint && Array.isArray(e.hint.moves) && e.hint.moves.length ? e : null;
}
function saveDailyEndgame(lex, day, pz) {
  const all = store.json("dailyEndgames", {}), old = dayKey(new Date(Date.now() - 40 * 86400000));
  for (const k of Object.keys(all)) if (!k.startsWith(DAILY_EG_VERSION + " ") || k.split(" ").pop() < old) delete all[k];
  all[DAILY_EG_VERSION + " " + lex + " " + day] = { seed: pz.seed, turn: pz.turn, moves: pz.moves,
    hint: { exact: pz.hint.exact, line: pz.hint.line, moves: pz.hint.moves.slice(0, 5).map((m) => ({ move: m.move, score: m.score, value: m.value, win: m.win })) } };
  store.put("dailyEndgames", all);
}
async function findEndgame(base, daily) {
  let chosen = null, quality = -1, at = -1, done = false;
  const t0 = Date.now(), cap = daily ? DAILY_EG_TILES : 11;
  for (let a = 0; a < 24 && !done && !(!daily && chosen && Date.now() - t0 > 4000); a++) {
    const seed = base + a;
    let r = await ui("new first " + seed);
    at = seed;
    for (let i = 0; i < 60 && r.ok && !r.state.over && r.state.bag > 0; i++) {
      await ui("seat " + (1 - r.state.turn));
      r = await ui("bot static");
    }
    if (!r.ok || r.state.over || r.state.bag !== 0) continue;
    await ui("seat " + r.state.turn);
    let st = (await ui("state")).state;
    for (let i = 0; i < 8 && !st.over && st.rack.length + st.unseen.length > cap; i++) {
      await ui("seat " + (1 - st.turn));
      r = await ui("bot static");
      await ui("seat " + r.state.turn);
      st = (await ui("state")).state;
    }
    if (st.over || st.rack.length < 3 || st.rack.length + st.unseen.length > cap || st.rack.includes("?") || st.unseen.includes("?")) continue;
    const h = await ui(daily ? "hint 0 nodes=" + DAILY_EG_NODES : "hint 1.2");
    if (!h.ok || !h.hint.exact || !h.hint.moves.length || isPass(h.hint.moves[0].move)) continue;
    const plays = h.hint.moves.filter((m) => !isPass(m.move));
    const topScore = Math.max(...plays.map((m) => m.score));
    // the obvious move: the highest-scoring play (the best of them when several score the most;
    // the engine makes each of their values exact)
    const greedy = Math.max(...plays.filter((m) => m.score === topScore).map((m) => m.value));
    const gain = h.hint.moves[0].value - greedy;
    const single = h.hint.moves.length < 2 || h.hint.moves[1].value < h.hint.moves[0].value;
    const q = 2 * gain + (single ? 1 : 0);
    if (q > quality) {
      chosen = { seed, turn: st.turn, moves: (await ui("record")).moves, hint: h.hint };
      quality = q;
      done = single && gain >= 4;
    }
  }
  if (chosen && chosen.seed !== at) {  // the engine moved on to later tries: back to the one chosen
    await ui("new first " + chosen.seed);
    for (const mv of chosen.moves) await ui("force " + mv);
    await ui("seat " + chosen.turn);
  }
  return chosen;
}
// A puzzle's name: the daily ones (a move and an endgame each day), or a free one.
function puzzleName(daily, kind) {
  return daily ? (kind === "endgame" ? "Daily endgame" : "Daily puzzle") : kind === "endgame" ? "Endgame puzzle" : "Puzzle";
}
async function openPuzzle(daily, kind, day) {
  kind = kind || (daily ? "move" : store.get("puzzleKind", "move"));
  if (!daily) store.set("puzzleKind", kind);
  const lex = settings.lexicon, key = daily ? day || dayKey(new Date()) : null;
  const base = daily ? (kind === "endgame" ? egDaySeed(key) : daySeed(key)) : 1000 + Math.floor(Math.random() * 2 ** 29) * 8;
  const wasBusy = busy;
  G = { id: Date.now().toString(36), version: 1, mode: "puzzle", lexicon: lex, level: "strong", clockMin: 0, overtime: "naspa", think: "", seed: 0,
    firstSide: 0, human: 0, record: [], used: [0, 0], running: -1, since: 0, paused: false, pauseNote: "", assisted: false, over: false, result: null,
    startedAt: new Date().toISOString(), verdicts: {}, puzzle: { daily: key, kind, tries: 0, solved: false, revealed: false, answer: null, alts: [], last: null } };
  resetView();
  if (loadedLex !== lex) openStart();
  $("puzzle").hidden = false;
  $("puzzle").innerHTML = '<div class="panel-head"><span>' + puzzleName(daily, kind) + '</span></div><div class="puzzle-body"><p class="dim">Setting up the position…</p></div>';
  await withLoading(lex, async () => {
    if (wasBusy) await restartEngine();  // (resetView has cleared busy)
    epoch++;
    const my = epoch;
    busy = true;
    setStatus("Setting up the puzzle", "thinking");
    let pz = daily && kind === "endgame" ? savedDailyEndgame(lex, key) : null;
    if (pz) {  // chosen before on this device: back to its position
      await ui("new first " + pz.seed);
      for (const mv of pz.moves) await ui("force " + mv);
      await ui("seat " + pz.turn);
    } else {
      pz = kind === "endgame" ? await findEndgame(base, !!daily) : await findPuzzle(base);
      if (pz && daily && kind === "endgame" && my === epoch) saveDailyEndgame(lex, key, pz);
    }
    busy = false;
    if (my !== epoch) return;
    if (!pz) throw new Error("no puzzle could be set up; try again");
    G.seed = pz.seed;
    G.human = pz.turn;
    G.record = pz.moves.map((m, i) => ({ move: m, side: i % 2, used: [0, 0] }));
    G.puzzle.answer = pz.hint.moves[0].move;
    G.puzzle.alts = pz.hint.moves.slice(0, 5);
    G.puzzle.line = pz.hint.exact && pz.hint.line ? pz.hint.line : null;
    const st = (await ui("state")).state;
    S = null;
    showState(st, null);
    cursor = null;
    render();
    renderPuzzle();
  });
}
async function puzzleCheck() {
  const k = G.record.length - 1, pz = G.puzzle, my = epoch;
  busy = true;
  render();
  setStatus("Judging your move", "thinking");
  let r;
  const eg = pz.kind === "endgame";
  try { r = await ui("review " + k + " " + (eg ? 6 : secs(1.5))); } catch (e) { return; }
  busy = false;
  if (my !== epoch || G.mode !== "puzzle") return;
  pz.tries++;
  pz.last = r.ok ? r : null;
  const cls = r.ok ? classify(r) : null;
  // An endgame is solved only by a move that ends as well as the best (within half a point).
  const ok = r.ok && (eg && r.exact ? r.same || Math.max(0, +r.valueLoss || 0) < 0.5 : cls && SOLVED.has(cls));
  pz.marks = (pz.marks || "") + (ok ? "y" : "n");
  if (ok) {
    pz.solved = true;
    sfx.good();
    logPuzzle();
    render();
  } else {
    sfx.bad();
    await ui("undo");
    G.record.pop();
    const st = (await ui("state")).state;
    S = null;
    showState(st, null);
    if (pz.tries >= 3) { pz.revealed = true; logPuzzle(); await showAnswer(); }
  }
  renderPuzzle();
}
async function showAnswer() {
  const pz = G.puzzle;
  pz.revealed = true;
  logPuzzle();
  recall();
  const was = pz.solved;
  pz.solved = false;   // let the answer's tiles go down
  pz.revealed = false;
  await showHint(pz.last && !pz.last.same ? pz.last.best : pz.answer);
  pz.solved = was;
  pz.revealed = true;
  render();
  renderPuzzle();
}
function logPuzzle() {
  const pz = G.puzzle;
  const log = puzzleLog(), key = pz.daily ? dailyKey(pz.daily, pz.kind) : "free:" + G.id;
  const prev = log[key] || {};
  log[key] = { solved: prev.solved || pz.solved, tries: pz.tries, revealed: !pz.solved && pz.revealed, lex: G.lexicon, at: new Date().toISOString(), kind: pz.kind || "move",
    late: prev.solved ? !!prev.late : !!pz.daily && pz.daily !== dayKey(new Date()) };
  store.put("puzzles", log);
}
// The daily puzzle's result as a few lines of text, a mark for each try: no position,
// so it gives nothing away to someone who has not solved it yet.
async function sharePuzzle() {
  const pz = G && G.puzzle;
  if (!pz || !pz.daily) return;
  const marks = (pz.marks || "").split("").map((m) => (m === "y" ? "🟩" : "🟥")).join("") + (pz.revealed && !pz.solved ? "⬛" : "");
  const day = new Date(pz.daily + "T12:00:00").toLocaleDateString([], { day: "numeric", month: "long" });
  const text = "Tilefish " + puzzleName(true, pz.kind).toLowerCase() + ", " + day + " (" + (LEX_NAME[G.lexicon] || G.lexicon) + ")\n" + marks + " " +
    (pz.solved ? "solved " + (pz.tries === 1 ? "first time" : "in " + pz.tries + " tries") : "not solved") + "\n" + location.href.split(/[?#]/)[0];
  if (navigator.share && matchMedia("(pointer: coarse)").matches) {
    try { await navigator.share({ text }); return; } catch (e) { if (e && e.name === "AbortError") return; }
  }
  try { await navigator.clipboard.writeText(text); toast("Result copied.", true); } catch (e) { toast(text, true); }
}
let pastOpen = false;
function renderPuzzle() {
  const box = $("puzzle"), pz = G && G.puzzle;
  if (!pz) { box.hidden = true; return; }
  box.hidden = false;
  const eg = pz.kind === "endgame";
  const streak = puzzleStreak(pz.kind);
  const opp = S && S.bag === 0 ? S.unseen.split("").sort().join("") : "";
  let html = '<div class="panel-head"><span>' + puzzleName(!!pz.daily, pz.kind) + (pz.daily ? " · " + new Date(pz.daily + "T12:00:00").toLocaleDateString([], { day: "numeric", month: "long" }) : "") +
    "</span>" + (streak ? '<span class="streak" title="' + (eg ? "Daily endgames" : "Daily puzzles") + ' solved in a row">' + streak + " day streak</span>" : "") + "</div><div class=\"puzzle-body\">";
  // the line a move ends with, in an endgame: points ahead by the end from here
  const ends = (v) => (num(v.playedValue) != null ? sgn(v.playedValue) : "");
  if (pz.solved) {
    const v = pz.last, cls = classify(v);
    html += '<div class="pz-result ok">' + badge(cls, "lg") + "<div><b>Solved" + (pz.tries > 1 ? " in " + pz.tries + " tries" : " first time") + ".</b> " +
      esc(moveLabel(v.played)) + (eg && v.exact ? " is the best play: " + ends(v) + " by the end of the game, with both sides at their best." :
        " is " + CLASS_INFO[cls].say + (v.same ? ": Tilefish's choice too." : ", within 1% of " + esc(moveLabel(v.best)) + ".")) + "</div></div>";
  } else if (pz.revealed) {
    html += '<div class="pz-result">' + "<div><b>The answer:</b> " + esc(moveLabel(pz.last && !pz.last.same ? pz.last.best : pz.answer)) + ", shown on the board.</div></div>";
  } else {
    html += eg ? '<p class="pz-task">The bag is empty' + (opp ? " and the opponent holds <b>" + esc(opp) + "</b>" : "") +
        ". Find the move that ends the game best: the most points ahead, or the fewest behind, when both sides then play perfectly.</p>"
      : '<p class="pz-task">Find the best move for your rack. Your move is judged by the winning chance it keeps; within 1% of the best solves it.</p>';
    if (pz.last) {
      const v = pz.last, cls = classify(v);
      if (eg && v.exact) {
        html += '<div class="pz-result no">' + badge(cls, "lg") + "<div><b>Not quite.</b> " + esc(moveLabel(v.played)) + " ends " + ends(v) + ", " +
          (+v.valueLoss).toFixed(0) + " point" + (Math.round(+v.valueLoss) === 1 ? "" : "s") + " worse than the best play. " +
          ((v.playedScore || 0) > (v.bestScore || 0) ? "Scoring the most now is not the answer." : "Think about going out, and about what the opponent can still play.") + "</div></div>";
        if (v.playedLine && v.playedLine.length > 1) html += '<p class="coach-line"><span class="lbl">What would follow:</span> ' + lineHtml(v.playedLine, G.human) + "</p>";
      } else {
        html += '<div class="pz-result no">' + badge(cls, "lg") + "<div><b>Not quite.</b> " + esc(moveLabel(v.played)) + " is " + CLASS_INFO[cls].say +
          " (" + pp(Math.max(0, v.winLoss)) + " less winning chance). " + (isBingo(v.best) && !isBingo(v.played) ? "Look for a bingo." :
            (v.bestScore || 0) > (v.playedScore || 0) ? "The best move scores more." : "Think about what you keep.") + "</div></div>";
      }
    }
    html += '<div class="pz-tries">' + [0, 1, 2].map((i) => '<span class="' + (i < pz.tries ? "used" : "") + '"></span>').join("") + "<em>" + (3 - pz.tries) + " tr" + (3 - pz.tries === 1 ? "y" : "ies") + " left</em></div>";
  }
  if ((pz.solved || pz.revealed) && pz.alts.length) {
    const top = pz.alts[0];
    html += '<ol class="alts2 pz-alts">' + pz.alts.slice(0, 4).map((a, i) => '<li><button data-pzmove="' + esc(a.move) + '">' +
      badge(i ? STEP_CLASSES[eg ? stepOf(Math.max(0, top.value - a.value), POINT_STEPS) : stepOf(Math.max(0, top.win - a.win), WIN_STEPS)] : "best", "sm") +
      '<span class="mv">' + esc(moveLabel(a.move)) + '</span><span class="sc">' + (a.score ? "+" + a.score : "") + '</span><span class="wp">' +
      (eg ? sgn(a.value) : isFinite(a.win) ? pct(a.win) : "") + "</span></button></li>").join("") + "</ol>";
    const best = pz.last && pz.last.exact && pz.last.line && pz.last.line.length ? pz.last.line : pz.line;
    if (eg && best && best.length) html += '<p class="coach-line"><span class="lbl">Best play to the end:</span> ' + lineHtml(best, G.human) + "</p>";
    if (eg) html += '<p class="pz-note dim">The last figure is where each move ends: points ahead (or behind) by the end of the game.</p>';
  }
  html += '<div class="coach-acts">' + (!pz.solved && !pz.revealed && pz.tries ? '<button class="ctl" id="pz-answer">Show the answer</button>' : "") +
    (pz.daily && (pz.solved || pz.revealed) ? '<button class="ctl" id="pz-share">Share result</button>' : "") +
    '<span class="ctl-gap"></span><button class="ctl primary" id="pz-next">' + (pz.solved || pz.revealed ? "Next puzzle" : "Skip") + "</button></div>";
  // The last fortnight's daily puzzles, each from its date, so any of them can be played again.
  if (pz.daily) {
    const log = puzzleLog(), days = [];
    for (let i = 1; i <= 14; i++) { const d = new Date(); d.setDate(d.getDate() - i); days.push(d); }
    html += '<details class="pz-past"' + (pastOpen ? " open" : "") + '><summary>' + (eg ? "Earlier daily endgames" : "Earlier daily puzzles") + '</summary><div class="pz-days">' + days.map((d) => {
      const k = dayKey(d), e = log[dailyKey(k, pz.kind)], mark = e ? (e.solved ? "✓" : e.revealed ? "✗" : "·") : "";
      return '<button data-day="' + k + '" class="' + (k === pz.daily ? "on " : "") + (e && e.solved ? "ok" : e && e.revealed ? "no" : "") + '" title="' +
        esc(d.toLocaleDateString([], { weekday: "long", day: "numeric", month: "long" })) + '">' + d.getDate() + '<span>' + mark + "</span></button>";
    }).join("") + "</div></details>";
  }
  html += '<div class="pz-kind"><span class="dim">' + (pz.daily ? "Daily:" : "Puzzles:") + '</span><div class="seg" role="radiogroup" aria-label="Kind of puzzle">' +
    [["move", "Best move"], ["endgame", "Endgame"]].map(([k, l]) => '<button data-kind="' + k + '" role="radio" aria-checked="' + (k === (pz.kind || "move")) + '" class="' + (k === (pz.kind || "move") ? "on" : "") + '">' + l + "</button>").join("") +
    "</div></div></div>";
  box.innerHTML = html;
  if ($("pz-answer")) $("pz-answer").onclick = showAnswer;
  if ($("pz-share")) $("pz-share").onclick = sharePuzzle;
  $("pz-next").onclick = () => openPuzzle(false, pz.kind || "move");
  // in a daily puzzle the switch moves between the day's two puzzles
  box.querySelectorAll(".pz-kind button").forEach((b) => (b.onclick = () => { if (!busy) openPuzzle(!!pz.daily, b.dataset.kind, pz.daily || undefined); }));
  box.querySelectorAll(".pz-days button").forEach((b) => (b.onclick = () => { if (!busy) openPuzzle(true, pz.kind, b.dataset.day); }));
  const past = box.querySelector(".pz-past");
  if (past) past.ontoggle = () => { pastOpen = past.open; };
  box.querySelectorAll("[data-pzmove]").forEach((b) => (b.onclick = async () => {
    if (!puzzleDone()) return;
    const was = [pz.solved, pz.revealed];
    if (pz.solved) return;  // the move stays as played
    pz.revealed = false;
    recall();
    await showHint(b.dataset.pzmove);
    [pz.solved, pz.revealed] = was;
    render();
  }));
}

// ---------- stats ----------
// Every finished game against Tilefish is noted (score, result, bingos, best move), with
// your accuracy once the game has been reviewed, and shown with the puzzles under Games.
function recordStats() {
  if (!G || (G.mode !== "play" && G.mode !== "practice") || !G.result) return;
  const me = G.human, R = G.result;
  const mine = S.history.filter((h) => (h.who === "you" ? S.seat : 1 - S.seat) === me);
  const top = mine.reduce((a, h) => (h.type === "play" && (!a || h.score > a.score) ? h : a), null);
  const list = store.json("stats", []);
  list.push({ id: G.id, d: R.endedAt, mode: G.mode, level: G.level, lex: G.lexicon, me: R.final[me], opp: R.final[1 - me],
    r: R.winner < 0 ? 0.5 : R.winner === me ? 1 : 0, bingos: mine.filter((h) => h.tiles === 7).length, n: mine.length,
    best: top ? { move: top.move, score: top.score } : null, acc: null, assisted: !!G.assisted });
  store.put("stats", list.slice(-500));
}
function statsAccuracy() {
  if (!G || (G.mode !== "play" && G.mode !== "practice")) return;
  const vs = S.history.map((h, k) => k).filter((k) => moverOf(k) === G.human).map(verdictAt).filter(Boolean);
  if (!vs.length) return;
  const list = store.json("stats", []), e = list.find((x) => x.id === G.id);
  if (!e) return;
  e.acc = vs.reduce((a, v) => a + accuracyOf(v), 0) / vs.length;
  store.put("stats", list);
}
function renderStats() {
  const list = store.json("stats", []), box = $("stats-view");
  const log = puzzleLog(), pz = Object.values(log);
  if (!list.length && !pz.length) { box.innerHTML = '<p class="dim">Finish a game against Tilefish, or solve a puzzle, and your stats appear here.</p>'; return; }
  const n = list.length, wins = list.reduce((a, g) => a + g.r, 0);
  const avg = (f) => (n ? list.reduce((a, g) => a + f(g), 0) / n : 0);
  const tile = (v, l) => '<div class="stat"><span class="stat-n">' + v + '</span><span class="stat-l">' + l + "</span></div>";
  let html = '<div class="stat-row">' + tile(n, "games") + tile(n ? Math.round((100 * wins) / n) + "%" : "–", "won") + tile(n ? Math.round(avg((g) => g.me)) : "–", "average score") +
    tile(n ? avg((g) => g.bingos).toFixed(1) : "–", "bingos a game") + "</div>";
  const levels = ["beginner", "easy", "casual", "strong", "champion"].filter((l) => list.some((g) => g.level === l));
  if (levels.length) {
    html += '<table class="stat-table"><thead><tr><th>Against</th><th>Won</th><th>Lost</th><th>Drawn</th><th>Average</th></tr></thead><tbody>';
    for (const l of levels) {
      const gs = list.filter((g) => g.level === l);
      html += "<tr><td>" + capital(l) + "</td><td>" + gs.filter((g) => g.r === 1).length + "</td><td>" + gs.filter((g) => g.r === 0).length + "</td><td>" +
        gs.filter((g) => g.r === 0.5).length + "</td><td>" + Math.round(gs.reduce((a, g) => a + g.me, 0) / gs.length) + "–" + Math.round(gs.reduce((a, g) => a + g.opp, 0) / gs.length) + "</td></tr>";
    }
    html += "</tbody></table>";
  }
  const acc = list.filter((g) => g.acc != null).slice(-20);
  if (acc.length) {
    const W = 560, H = 56, xs = (i) => (acc.length > 1 ? (i / (acc.length - 1)) * (W - 8) + 4 : W / 2), ys = (a) => H - 4 - ((a - 40) / 60) * (H - 8);
    const pts = acc.map((g, i) => xs(i).toFixed(1) + "," + ys(Math.max(40, g.acc)).toFixed(1)).join(" ");
    html += '<div class="stat-acc"><div class="stat-acc-head"><span>Accuracy, last ' + acc.length + ' reviewed games</span><b>' + (acc.reduce((a, g) => a + g.acc, 0) / acc.length).toFixed(1) +
      '</b></div><svg viewBox="0 0 ' + W + " " + H + '" class="spark" role="img" aria-label="Accuracy over recent games"><polyline points="' + pts + '"/>' +
      acc.map((g, i) => '<circle cx="' + xs(i).toFixed(1) + '" cy="' + ys(Math.max(40, g.acc)).toFixed(1) + '" r="2.6"><title>' + g.acc.toFixed(1) + "</title></circle>").join("") + "</svg></div>";
  }
  const bestGame = list.reduce((a, g) => (!a || g.me > a.me ? g : a), null), bestMove = list.reduce((a, g) => (g.best && (!a || g.best.score > a.score) ? g.best : a), null);
  if (bestGame) html += '<p class="stat-line">Highest score <b>' + bestGame.me + "</b>" + (bestMove ? " · best move <b>" + esc(pretty(bestMove.move)) + "</b> for " + bestMove.score : "") + "</p>";
  if (pz.length) {
    const solved = pz.filter((e) => e.solved).length;
    const egs = pz.filter((e) => e.kind === "endgame");
    html += '<p class="stat-line">Puzzles solved <b>' + solved + "</b> of " + pz.length + (egs.length ? " (endgames " + egs.filter((e) => e.solved).length + " of " + egs.length + ")" : "") +
      " · daily streak <b>" + puzzleStreak("move") + "</b>" + (Object.keys(log).some((k) => k.startsWith("eg:")) ? " · daily endgame streak <b>" + puzzleStreak("endgame") + "</b>" : "") + "</p>";
  }
  html += '<button class="link" id="stats-reset">Clear stats</button>';
  box.innerHTML = html;
  let armed = false;
  $("stats-reset").onclick = () => {
    if (!armed) { armed = true; $("stats-reset").textContent = "Clear them? Click again"; return; }
    store.del("stats"); store.del("puzzles"); renderStats();
  };
}

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
  if (shareable(G)) add("Copy game link", () => shareGame(G));
  if (G.mode === "analysis" && S && reviewAt < 0) add("Edit this position", () => openSetup({ board: S.board, rack: S.rack, you: S.you, them: S.bot }));
  if (G.mode === "puzzle") return;
  if (G.mode !== "review") add("Save Tilefish file", () => download("tilefish-" + G.id + ".json", JSON.stringify(snapshotForSave(), null, 1), "application/json"));
  add("Copy position", async () => {
    const r = await ui("cgpout");
    try { await navigator.clipboard.writeText(r.cgp); toast("Position copied (your rack only).", true); } catch (e) { toast(r.cgp); }
  });
  if (reviewable() && reviewAt < 0 && S && S.history.length) add("Game review", () => enterReview(S.history.length, { analyse: G.over || G.mode === "review" }));
}

// ---------- game links ----------
// A finished game travels in the link itself, after #game=: the word list, the seed and
// the moves (or the imported record), deflated and in base64url.  Whoever opens it gets
// the game in the review, replayed and judged by their own browser; nothing is stored
// anywhere else.  What a link carries goes to the engine only as checked moves (the
// engine takes ';' between commands) or as a file to import.
const MOVE_RE = /^(pass|exch [A-Z?]{1,7}|(phony )?(\d{1,2}[A-O]|[A-O]\d{1,2}) [A-Za-z()]{1,30}( bonus \d{1,4})?)$/;
const LINK_MAX = 256 * 1024;  // bytes, unpacked
const shareable = (g) => !!g && (g.over || g.mode === "review") && !g.cgp && (!!g.gcg || g.record.length > 0);
function b64url(u8) {
  let s = "";
  for (let i = 0; i < u8.length; i += 0x8000) s += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000));
  return btoa(s).replace(/\+/g, "-").replace(/\//g, "_").replace(/=+$/, "");
}
function unb64url(s) {
  const b = atob(s.replace(/-/g, "+").replace(/_/g, "/"));
  const u = new Uint8Array(b.length);
  for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i);
  return u;
}
async function readCapped(stream) {
  const reader = stream.getReader(), parts = [];
  let n = 0;
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    n += value.length;
    if (n > LINK_MAX) { reader.cancel(); throw new Error("too long"); }
    parts.push(value);
  }
  const out = new Uint8Array(n);
  let at = 0;
  for (const p of parts) { out.set(p, at); at += p.length; }
  return out;
}
async function packGame(g) {
  const p = { v: 1, l: g.lexicon, m: g.from || g.mode, h: g.human };
  if (g.gcg) p.g = g.gcg;
  else { p.s = g.seed; p.r = g.record.map((e) => e.move); }
  if (g.result) p.x = { f: g.result.final, w: g.result.winner, r: g.result.reason };
  const raw = new TextEncoder().encode(JSON.stringify(p));
  try {
    const z = await readCapped(new Blob([raw]).stream().pipeThrough(new CompressionStream("deflate-raw")));
    return "z" + b64url(z);
  } catch (e) {
    return "j" + b64url(raw);  // no CompressionStream here: the link is just longer
  }
}
async function unpackGame(text) {
  let bytes = unb64url(text.slice(1));
  if (text[0] === "z") bytes = await readCapped(new Blob([bytes]).stream().pipeThrough(new DecompressionStream("deflate-raw")));
  else if (text[0] !== "j" || bytes.length > LINK_MAX) throw new Error("not a game link");
  const p = JSON.parse(new TextDecoder().decode(bytes));
  if (!p || p.v !== 1 || !LEX_NAME[p.l]) throw new Error("not a game link");
  const g = { id: Date.now().toString(36), version: 1, mode: "review", from: ["play", "practice", "duo", "review"].includes(p.m) ? p.m : "review",
    lexicon: p.l, level: "strong", clockMin: 0, overtime: "naspa", think: "", seed: 0, firstSide: 0, human: p.h === 1 ? 1 : 0, record: [],
    used: [0, 0], running: -1, since: 0, paused: false, pauseNote: "", assisted: false, over: true, result: null,
    startedAt: new Date().toISOString(), verdicts: {}, shared: true };
  if (typeof p.g === "string") {
    g.gcg = p.g;
    g.from = "review";
  } else {
    if (!Number.isInteger(p.s) || p.s < 0 || p.s >= 2 ** 53) throw new Error("not a game link");
    if (!Array.isArray(p.r) || !p.r.length || p.r.length > 150 || !p.r.every((m) => typeof m === "string" && MOVE_RE.test(m))) throw new Error("not a game link");
    g.seed = p.s;
    g.record = p.r.map((move, k) => ({ move, side: k % 2, used: [0, 0] }));
  }
  const x = p.x;
  if (x && Array.isArray(x.f) && x.f.length === 2 && x.f.every(Number.isFinite) && [-1, 0, 1].includes(x.w))
    g.result = { final: x.f, winner: x.w, reason: ["out", "resign", "time"].includes(x.r) ? x.r : "out", penalties: [0, 0], endedAt: g.startedAt };
  return g;
}
async function shareGame(g) {
  let url;
  try { url = location.href.split("#")[0] + "#game=" + (await packGame(g)); } catch (e) { toast("This game could not be put in a link."); return; }
  if (navigator.share && matchMedia("(pointer: coarse)").matches) {
    try { await navigator.share({ title: "A game on Tilefish", url }); return; } catch (e) { if (e && e.name === "AbortError") return; }
  }
  try { await navigator.clipboard.writeText(url); }
  catch (e) {
    const t = document.createElement("textarea");
    t.value = url;
    document.body.appendChild(t);
    t.select();
    try { document.execCommand("copy"); } catch (e2) { /* nothing more to try */ }
    t.remove();
  }
  toast("Link copied: whoever opens it sees this game in the review.", true);
}
// Opens a game from the address (#game=...), once: the address is then cleared, so a
// reload or a new game is not taken back to it.  (Only once the game is open: on a first
// visit the page may reload itself to run on every core, and the link must survive that.)
async function openLink() {
  const m = location.hash.match(/^#game=([jz][A-Za-z0-9_-]+)$/);
  if (!m) return;
  const clear = () => { if (location.hash.slice(1) === "game=" + m[1]) history.replaceState(null, "", location.href.split("#")[0]); };
  let g;
  try { g = await unpackGame(m[1]); } catch (e) { clear(); toast("This game link could not be read."); return; }
  await openSaved(g, true);
  clear();
}
window.addEventListener("hashchange", openLink);

// ---------- pointer: tap, drag and drop ----------
let drag = null;
function squareAt(x, y) { const el = document.elementFromPoint(x, y); const sq = el && el.closest(".sq"); return sq ? +sq.dataset.sq : -1; }
function rackSlotAt(x, y) { const el = document.elementFromPoint(x, y); if (!el || !el.closest("#rack")) return -2; const t = el.closest("[data-slot]"); return t ? +t.dataset.slot : -1; }
function startDrag(e, from) {
  drag = { from, x: e.clientX, y: e.clientY, moved: false, ghost: null, t0: performance.now() };
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
  // A quick sideways flick along the rack shuffles it; a slower drag moves one tile.
  if (d.from.slot != null && sq < 0 && Math.abs(e.clientX - d.x) > 70 && Math.abs(e.clientY - d.y) < 45 && performance.now() - d.t0 < 300) {
    render();
    shuffle();
    return;
  }
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
    else if (cursor && !occupied(cursor.sq) && canArrange()) { if (await place(i, cursor.sq)) { advanceCursor(); checkPending(); } }
    else selected = selected === i ? -1 : i;
    render();
    if (selected >= 0 && canArrange()) setStatus("Now click a square for " + (rack[i].ch === "?" ? "the blank" : rack[i].ch) + ".");
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
  if (!sqEl || !canArrange()) return;
  const sq = +sqEl.dataset.sq;
  if (setup) {  // any square, filled or not: typing replaces its tile
    cursor = cursor && cursor.sq === sq ? { sq, down: !cursor.down } : { sq, down: false };
    renderSetup();
    return;
  }
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

// ---------- dialogs and keyboard focus ----------
// A card that opens (start, end, games, help, blank, handover) takes the keyboard focus and
// keeps Tab inside itself while it is open.  Games and Help give the focus back where it was
// when they close; the others lead into the game, so they let it go (Enter must play a word,
// not press the button that opened them).
const FOCUSABLE = 'button:not([disabled]):not([hidden]), input:not([disabled]), textarea, summary, [tabindex]:not([tabindex="-1"])';
function visibleIn(el) { return [...el.querySelectorAll(FOCUSABLE)].filter((x) => x.offsetParent !== null || x === document.activeElement); }
const sheetOpener = new Map();
let focusOutside = null;  // the last element focused outside every card
document.addEventListener("focusin", (e) => { if (!e.target.closest(".sheet")) focusOutside = e.target; });
document.querySelectorAll(".sheet").forEach((sheet) => {
  new MutationObserver(() => {
    if (!sheet.hidden) {
      if (!sheetOpener.has(sheet)) sheetOpener.set(sheet, focusOutside);
      requestAnimationFrame(() => { if (!sheet.hidden && !sheet.contains(document.activeElement)) { const f = visibleIn(sheet)[0]; if (f) f.focus({ preventScroll: true }); } });
    } else {
      const back = sheetOpener.get(sheet);
      sheetOpener.delete(sheet);
      const inside = document.activeElement === document.body || sheet.contains(document.activeElement);
      if (sheet.id === "games" || sheet.id === "help") { if (back && back.isConnected && inside) back.focus({ preventScroll: true }); }
      else if (sheet.contains(document.activeElement)) document.activeElement.blur();
    }
  }).observe(sheet, { attributes: true, attributeFilter: ["hidden"] });
});
document.addEventListener("keydown", (e) => {
  if (e.key !== "Tab") return;
  const sheet = [...document.querySelectorAll(".sheet")].find((x) => !x.hidden);
  if (!sheet) return;
  const items = visibleIn(sheet);
  if (!items.length) return;
  const first = items[0], last = items[items.length - 1];
  if (e.shiftKey && (document.activeElement === first || !sheet.contains(document.activeElement))) { e.preventDefault(); last.focus(); }
  else if (!e.shiftKey && (document.activeElement === last || !sheet.contains(document.activeElement))) { e.preventDefault(); first.focus(); }
}, true);

// ---------- keyboard ----------
const sheetOpen = () => ["start", "end", "blank-picker", "handover", "games", "help"].some((id) => !$(id).hidden);
document.addEventListener("keydown", (e) => {
  if (sheetOpen()) {
    if (e.key === "Escape") {
      for (const id of ["games", "help", "end"]) $(id).hidden = true;
      if (!$("start").hidden && !$("btn-start-close").hidden) $("btn-start-close").click();
    }
    return;
  }
  if (e.ctrlKey || e.metaKey || e.altKey || /input|textarea/i.test(e.target.tagName)) return;
  if (setup) return setupKey(e);
  const k = e.key;
  if (exploring && k === "Escape") { e.preventDefault(); if (pending.size) { recall(); render(); checkPending(); } else exitExplore(); return; }
  if (reviewAt >= 0 && !exploring) {
    if (k === "ArrowLeft") { e.preventDefault(); enterReview(reviewAt - 1); }
    else if (k === "ArrowRight") { e.preventDefault(); enterReview(reviewAt + 1); }
    else if (k === "ArrowUp") { e.preventDefault(); jumpKey(-1); }
    else if (k === "ArrowDown") { e.preventDefault(); jumpKey(1); }
    else if (k === "Home") { e.preventDefault(); enterReview(0); }
    else if (k === "End") { e.preventDefault(); enterReview(S.history.length); }
    else if (k === "b" || k === "B") { const v = shownVerdict(); if (v && !v.same) togglePreview(v.best); }
    else if (k === "Escape") { if (preview) setPreview(null); else leaveReview(); }
    return;
  }
  if (exchanging && humanTurn() && (/^[a-z]$/i.test(k) || k === "?")) { e.preventDefault(); toggleExchange(k); }
  else if (/^[a-z]$/i.test(k)) { e.preventDefault(); typeLetter(k); }
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
// Exchange by keyboard: a letter (or ? for a blank) picks the next such tile on the rack,
// or puts one back when they are all picked.
function toggleExchange(key) {
  const L = key === "?" ? "?" : key.toUpperCase();
  let i = rack.findIndex((r, j) => r && r.ch === L && !xsel.has(j));
  if (i >= 0) xsel.add(i);
  else {
    i = rack.map((r, j) => (r && r.ch === L && xsel.has(j) ? j : -1)).filter((j) => j >= 0).pop();
    if (i == null) { toast("You have no " + (L === "?" ? "blank" : L) + "."); return; }
    xsel.delete(i);
  }
  render();
}
function shuffle() {
  if (!rack.length || (reviewAt >= 0 && !exploring)) return;
  const idx = rack.map((_, i) => i);
  for (let i = idx.length - 1; i > 0; i--) { const j = Math.floor(Math.random() * (i + 1)); [idx[i], idx[j]] = [idx[j], idx[i]]; }
  permuteRack(idx);
  sfx.shuffle();
}
// Sort: alphabetical, then (pressed again) vowels before consonants; blanks last.
let sortMode = "alpha";
function sortRack() {
  if (!rack.length || (reviewAt >= 0 && !exploring)) return;
  const key = (ch) => (ch === "?" ? "~" : sortMode === "vowels" ? ("AEIOU".includes(ch) ? "0" : "1") + ch : ch);
  const idx = rack.map((_, i) => i).sort((a, b) => (key(rack[a].ch) < key(rack[b].ch) ? -1 : key(rack[a].ch) > key(rack[b].ch) ? 1 : a - b));
  permuteRack(idx);
  sortMode = sortMode === "alpha" ? "vowels" : "alpha";
  $("btn-sort").title = sortMode === "alpha" ? "Sort the rack A to Z" : "Sort vowels first";
  sfx.shuffle();
}
function permuteRack(idx) {
  const before = rackPositions();
  const map = {};
  idx.forEach((oldI, newI) => { map[oldI] = newI; });
  rack = idx.map((i) => rack[i]);
  for (const p of pending.values()) p.slot = map[p.slot];
  xsel = new Set([...xsel].map((i) => map[i]));
  selected = -1;
  render();
  animateRack(before);
}
function rackPositions() {
  const m = new Map();
  rackEl.querySelectorAll("[data-id]").forEach((el) => m.set(el.dataset.id, el.getBoundingClientRect().left));
  return m;
}
// Tiles slide to their new places instead of jumping.
function animateRack(before) {
  if (!before.size || !rackEl.animate || matchMedia("(prefers-reduced-motion: reduce)").matches) return;
  rackEl.querySelectorAll("[data-id]").forEach((el, i) => {
    const x = before.get(el.dataset.id);
    if (x == null) return;
    const dx = x - el.getBoundingClientRect().left;
    if (Math.abs(dx) < 1) return;
    el.animate([{ transform: "translateX(" + dx + "px)" }, { transform: "none" }], { duration: 280, delay: i * 12, easing: "cubic-bezier(.2,.75,.25,1)", fill: "backwards" });
  });
}

// ---------- buttons ----------
$("btn-shuffle").onclick = shuffle;
$("btn-recall").onclick = () => { recall(); render(); };
$("btn-play").onclick = submit;
$("btn-hint").onclick = () => hint();
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
$("btn-end-share").onclick = () => shareGame(G);
$("btn-review").onclick = () => { $("end").hidden = true; enterReview(S.history.length, { analyse: true }); };
$("btn-rematch").onclick = () => {
  $("end").hidden = true;
  if (G.mode !== "duo") settings.first = G.human === 0 ? "second" : "first";
  settings.mode = G.mode === "review" || G.mode === "analysis" ? "practice" : G.mode;
  startGame();
};
// Focus lasts for this visit only; a page opened later always shows the side panel.
$("btn-focus").onclick = () => {
  const on = !$("app").classList.contains("focus");
  $("app").classList.toggle("focus", on);
  $("btn-focus").setAttribute("aria-pressed", on);
  $("btn-focus").textContent = on ? "Exit focus" : "Focus";
};
store.del("focus");  // earlier versions kept Focus on from one visit to the next
// Installing the page as an app, where the browser offers it.
let installPrompt = null;
window.addEventListener("beforeinstallprompt", (e) => { e.preventDefault(); installPrompt = e; $("btn-install").hidden = false; });
window.addEventListener("appinstalled", () => { installPrompt = null; $("btn-install").hidden = true; });
$("btn-install").onclick = async () => {
  if (!installPrompt) return;
  const p = installPrompt;
  installPrompt = null;
  $("btn-install").hidden = true;
  p.prompt();
  try { await p.userChoice; } catch (e) { /* dismissed */ }
};
$("btn-help").onclick = () => { $("help").hidden = false; $("btn-help-close").focus(); };
$("btn-help-close").onclick = () => { $("help").hidden = true; };
$("btn-games").onclick = () => openGames();
$("btn-puzzle").onclick = () => openPuzzle(true);
$("btn-start-puzzle").onclick = () => { $("start").hidden = true; openPuzzle(true); };
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

// ---------- backup ----------
// Everything the page keeps (finished games, stats, puzzle results, the unfinished game) in
// one file, and back: a restore adds what the file has to what this browser has.
const BACKUP_KEYS = ["games", "stats", "puzzles", "current"];
$("btn-backup").onclick = () => {
  const data = { app: "tilefish", version: 1, saved: new Date().toISOString() };
  for (const k of BACKUP_KEYS) data[k] = store.json(k, null);
  download("tilefish-backup-" + dayKey(new Date()) + ".json", JSON.stringify(data), "application/json");
};
$("restore-in").onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  let d;
  try { d = JSON.parse(await f.text()); } catch (err) { d = null; }
  if (!d || d.app !== "tilefish" || d.version !== 1) { toast("That file is not a Tilefish backup."); return; }
  const byId = (a, b) => { const m = new Map(); for (const x of [...(a || []), ...(b || [])]) if (x && x.id != null && !m.has(x.id)) m.set(x.id, x); return [...m.values()]; };
  const games = byId(store.json("games", []), Array.isArray(d.games) ? d.games : []).sort((a, b) => String(b.date).localeCompare(String(a.date))).slice(0, 30);
  const stats = byId(store.json("stats", []), Array.isArray(d.stats) ? d.stats : []).sort((a, b) => String(a.d).localeCompare(String(b.d))).slice(-500);
  const pz = store.json("puzzles", {});
  if (d.puzzles && typeof d.puzzles === "object") for (const [k, v] of Object.entries(d.puzzles)) if (v && typeof v === "object") pz[k] = pz[k] ? Object.assign({}, v, pz[k], { solved: !!(pz[k].solved || v.solved) }) : v;
  store.put("games", games);
  store.put("stats", stats);
  store.put("puzzles", pz);
  if (d.current && !store.json("current", null)) store.put("current", d.current);
  const count = (n, one, many) => n + " " + (n === 1 ? one : many);
  toast("Restored. Here now: " + count(games.length, "game", "games") + ", " + count(stats.length, "result", "results") + ", " + count(Object.keys(pz).length, "puzzle", "puzzles") + ".", true);
  openGames($("stats-view").hidden ? "games" : "stats");
};

// ---------- games list ----------
function showGamesTab(tab) {
  document.querySelectorAll("#games .tabs button").forEach((b) => b.classList.toggle("on", b.dataset.tab === tab));
  $("games-view").hidden = tab !== "games";
  $("stats-view").hidden = tab !== "stats";
  if (tab === "stats") renderStats();
}
document.querySelectorAll("#games .tabs button").forEach((b) => (b.onclick = () => showGamesTab(b.dataset.tab)));
function openGames(tab) {
  showGamesTab(typeof tab === "string" ? tab : "games");
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
    const ln = document.createElement("button"); ln.className = "link"; ln.textContent = "Copy link"; ln.hidden = !shareable(g.game);
    ln.onclick = () => shareGame(g.game);
    const ex = document.createElement("button"); ex.className = "link"; ex.textContent = "Save file";
    ex.onclick = () => download("tilefish-" + g.id + ".json", JSON.stringify(g.game, null, 1), "application/json");
    const del = document.createElement("button"); del.className = "link"; del.textContent = "Delete";
    del.onclick = () => { const l = store.json("games", []); l.splice(i, 1); store.put("games", l); openGames(); };
    acts.append(rv, ln, ex, del);
    li.append(info, acts);
    ol.appendChild(li);
  });
  $("games").hidden = false;
  $("btn-games-close").focus();
}

// ---------- start, resume, import ----------
function openStart() {
  $("btn-start-close").hidden = !G && !setup;  // something to go back to
  const cur = store.json("current", null);
  $("resume").hidden = !cur || cur.over;
  if (cur && !cur.over) $("resume-text").textContent = describe(cur) + ", " + cur.record.length + " moves";
  $("start").hidden = false;
  syncStart();
  refreshOnDevice();
}
function lexNote(lex) {
  if (onDevice.has(lex)) return NOTES[lex].replace(/ About \d+ MB, downloaded once and kept by this browser\./, "") + " Kept on this device, so it loads without a connection.";
  return NOTES[lex] + (offline() ? " You are offline, and it is not on this device yet." : "");
}
function syncStart() {
  document.querySelectorAll(".seg").forEach((seg) => {
    const name = seg.dataset.name;
    seg.querySelectorAll("button").forEach((b) => { b.classList.toggle("on", b.dataset.v === settings[name]); b.setAttribute("aria-checked", b.dataset.v === settings[name]); b.setAttribute("role", "radio"); });
  });
  document.querySelectorAll("[data-for]").forEach((el) => { el.hidden = !el.dataset.for.split(" ").includes(settings.mode); });
  $("mode-note").textContent = NOTES[settings.mode];
  $("level-note").textContent = NOTES[settings.level];
  $("lex-note").textContent = lexNote(settings.lexicon);
  document.querySelectorAll('.seg[data-name="lexicon"] button').forEach((b) => b.classList.toggle("off", offline() && !onDevice.has(b.dataset.v)));
  $("ot-note").textContent = NOTES[settings.overtime];
  $("rules-note").textContent = RULES_NOTE[settings.rules] || "";
  $("wpen-row").hidden = settings.rules !== "wespa";
  $("wpen-note").textContent = WPEN_NOTE[settings.wpen] || "";
  $("custom-row").hidden = settings.clock !== "custom";
  $("custom-min").value = settings.customMin;
  $("think").value = settings.think;
  $("sound").checked = settings.sound === "1";
  $("sfx").checked = settings.sfx === "1";
  $("rate").checked = settings.rate !== "0";
  $("btn-start").textContent = settings.mode === "analysis" ? "Open position" : "Start game";
  const fake = { mode: settings.mode, level: settings.level, lexicon: settings.lexicon, clockMin: clockMinutes(), overtime: settings.overtime, rules: settings.rules, wpen: settings.wpen };
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
    if (seg.dataset.name === "lexicon") prefetch(b.dataset.v);
    // Tournament rules come with a tournament's clock: 25 minutes each, and that body's overtime rule.
    if (seg.dataset.name === "rules" && b.dataset.v !== "checked") {
      settings.clock = "25";
      settings.overtime = b.dataset.v;
      store.set("clock", "25");
      store.set("overtime", b.dataset.v);
    }
    syncStart();
  });
});
$("custom-min").onchange = () => { settings.customMin = $("custom-min").value; store.set("customMin", settings.customMin); syncStart(); };
$("think").onchange = () => { settings.think = $("think").value; store.set("think", settings.think); syncStart(); };
$("sfx").onchange = () => { settings.sfx = $("sfx").checked ? "1" : "0"; store.set("sfx", settings.sfx); sfx.place(); };
$("rate").onchange = () => { settings.rate = $("rate").checked ? "1" : "0"; store.set("rate", settings.rate); };
$("sound").onchange = () => { settings.sound = $("sound").checked ? "1" : "0"; store.set("sound", settings.sound); if (settings.sound === "1") beep(); };

// The bar follows the download byte by byte; for the stages that run inside the engine
// it glides towards the stage's end over the time that stage usually takes, so it
// keeps moving until the engine reports back.
function setBar(p, glideMs) {
  const fill = $("loading-fill");
  fill.style.transition = glideMs ? "width " + glideMs + "ms cubic-bezier(.25,.6,.35,1)" : "";
  fill.style.width = (100 * p).toFixed(1) + "%";
}
const mb = (n) => (n / 1e6).toFixed(1);
function showLoadStage(lex, p, m) {
  const name = LEX_NAME[lex] || lex;
  const text = {
    download: m.got >= m.size ? "Getting " + name + " ready" : "Downloading " + name + ": " + mb(m.got) + " of " + mb(m.size) + " MB" + (BUNDLED.has(lex) ? "" : " (once)"),
    engine: "Starting the engine",
    words: "Reading the word list",
    leaves: "Reading the leave values",
    ready: "Ready",
  }[m.stage];
  if (text) $("loading-text").textContent = text;
  if (m.next) {
    setBar(p);
    requestAnimationFrame(() => requestAnimationFrame(() => setBar(p + 0.85 * (m.next - p), m.ms)));
  } else {
    setBar(p);
  }
}
async function withLoading(lex, fn) {
  const btn = $("btn-start");
  btn.disabled = true;
  $("loading").hidden = false;
  $("loading").classList.add("busy");
  setBar(0.02);
  $("loading-text").textContent = "Getting " + (LEX_NAME[lex] || lex) + " ready";
  try {
    if (!worker) startWorker();
    if (loadedLex !== lex) { epoch++; await loadLexicon(lex, (p, m) => showLoadStage(lex, p, m)); }
    setBar(1);
    await fn();
    $("loading").hidden = true;
    $("start").hidden = true;
  } catch (err) {
    setBar(0);
    const kept = [...onDevice].map((l) => LEX_NAME[l] || l);
    $("loading-text").textContent = capital(err.message) + (!ENGINE_OK ? "." : offline() ? (kept.length ? ". Kept on this device: " + kept.join(", ") + "." : ".")
      : !BUNDLED.has(lex) ? ". ENABLE works without a download." : ".");
  } finally {
    $("loading").classList.remove("busy");
    btn.disabled = false;
  }
}
function resetView() {
  if (setup) closeSetup();
  S = null; shownBoard = null; rack = []; pending = new Map(); order = []; last = new Set(); reviewAt = -1;
  rvBoards = null; preview = null; analysing = null; rvFresh = null; exploring = null; hintState = null; inferState = null;
  $("rating").hidden = true;
  $("puzzle").hidden = true;
  $("evalbar").hidden = true;
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
  const wasBusy = busy;
  G = Object.assign({ verdicts: {} }, g);
  resetView();
  await withLoading(G.lexicon, async () => {
    const st = wasBusy ? await restartEngine() : await resumeEngine();
    if (!st) return;
    S = null;
    showState(st, null);
    if (G.over || review) { $("start").hidden = true; await enterReview(S.history.length, { analyse: G.over || G.mode === "review" }); return; }
    // Time while the page was closed is not charged.  In Practice a restored game waits,
    // paused, for its Resume button or the next move; in Play, which has no pause, and for two
    // players, the clock runs on at once.
    if (G.clockMin) {
      if (assistAllowed()) { G.paused = true; G.pauseNote = "restored"; G.since = 0; }
      else { G.paused = false; G.pauseNote = ""; G.since = G.running >= 0 ? Date.now() : 0; }
    }
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
  const wasBusy = busy;
  G = Object.assign(newGameObject(), { mode: "analysis", cgp: text.replace(/\s+lex\s+\S+;?\s*$/i, ""), clockMin: 0, assisted: true });
  resetView();
  await withLoading(G.lexicon, async () => {
    if (wasBusy) await restartEngine();  // as in startGame: nothing still running for the old game carries on
    epoch++;
    const r = await ui("cgp " + G.cgp);
    if (!r.ok) throw new Error(r.error);
    await analysisFrom(r.state);
  });
}
async function analysisFrom(state) {
  G.human = state.turn;
  const st = (await ui("seat " + G.human)).state;
  showState(st, null);
  render();
}

// ---------- setting up a board by hand (Analyse) ----------
// The board is edited in the page: click a square and type, or tap the letters under the
// board (a phone has no keyboard here); Shift or the blank key makes a blank.  The racks and
// scores go with it into a CGP for `ui cgp`, which checks the position.
const TILE_COUNT = { A: 9, B: 2, C: 2, D: 4, E: 12, F: 2, G: 3, H: 2, I: 9, J: 1, K: 1, L: 4, M: 2, N: 6, O: 8, P: 2, Q: 1, R: 6, S: 4, T: 6, U: 4, V: 2, W: 2, X: 1, Y: 2, Z: 1, "?": 2 };
let setup = null;  // {board: 225 squares, "." or a letter (lower case: a blank), blank: next letter is a blank}
function openSetup(from) {
  epoch++;
  resetView();
  G = null;
  setup = { board: new Array(N * N).fill("."), blank: false };
  if (from) for (let r = 0; r < N; r++) for (let c = 0; c < N; c++) setup.board[r * N + c] = from.board[r][c];
  $("setup-rack").value = from ? from.rack : "";
  $("setup-opp").value = "";
  $("setup-you").value = from ? from.you : 0;
  $("setup-them").value = from ? from.them : 0;
  cursor = { sq: 7 * N + 7, down: false };
  $("app").classList.add("setting-up");
  // beside the board where there is room, else under it
  if (matchMedia("(min-width: 901px)").matches) $("side").prepend($("setup")); else $("rack").after($("setup"));
  $("setup").hidden = false;
  $("mini").hidden = true;
  renderSetup();
}
function closeSetup() {
  setup = null;
  cursor = null;
  $("app").classList.remove("setting-up");
  $("setup").hidden = true;
}
function setupRows() {
  const rows = [];
  for (let r = 0; r < N; r++) rows.push(setup.board.slice(r * N, r * N + N).join(""));
  return rows;
}
const rackText = (id) => $(id).value.toUpperCase().replace(/[^A-Z?]/g, "").slice(0, 7);
// Too many of a tile, counting the board and both racks (a blank on the board counts as "?").
function setupProblem() {
  const n = {};
  const add = (ch) => { n[ch] = (n[ch] || 0) + 1; };
  for (const ch of setup.board) if (ch !== ".") add(ch === ch.toLowerCase() ? "?" : ch);
  for (const ch of rackText("setup-rack") + rackText("setup-opp")) add(ch);
  for (const ch of Object.keys(n)) if (n[ch] > TILE_COUNT[ch]) return "Too many " + (ch === "?" ? "blanks" : ch + "'s") + ": " + n[ch] + " of " + TILE_COUNT[ch] + ".";
  return null;
}
function setupCgp() {
  const rows = setupRows().map((row) => row.replace(/\.+/g, (m) => String(m.length)));
  const score = (id) => Math.max(0, Math.min(2000, Math.round(+$(id).value || 0)));
  return rows.join("/") + " " + rackText("setup-rack") + "/" + rackText("setup-opp") + " " + score("setup-you") + "/" + score("setup-them") + " 0";
}
function renderSetup() {
  shownBoard = setupRows();
  renderBoard();
  const keys = $("setup-keys");
  if (!keys.firstChild) {
    for (const L of "ABCDEFGHIJKLMNOPQRSTUVWXYZ") {
      const b = document.createElement("button");
      b.className = "skey";
      b.textContent = L;
      b.dataset.key = L;
      keys.appendChild(b);
    }
    for (const [k, label, title] of [["blank", "Blank", "The next letter is a blank (or hold Shift)"], ["back", "⌫", "Step back and clear"], ["del", "Clear", "Clear this square"]]) {
      const b = document.createElement("button");
      b.className = "skey wide";
      b.textContent = label;
      b.title = title;
      b.dataset.key = k;
      keys.appendChild(b);
    }
    keys.addEventListener("click", (e) => {
      const b = e.target.closest("[data-key]");
      if (!b || !setup) return;
      const k = b.dataset.key;
      if (k === "blank") setup.blank = !setup.blank;
      else if (k === "back") setupBack();
      else if (k === "del") setupSet(cursor ? cursor.sq : -1, ".");
      else setupType(k, false);
      renderSetup();
    });
  }
  keys.querySelector('[data-key="blank"]').classList.toggle("on", setup.blank);
  const tiles = setup.board.filter((ch) => ch !== ".").length, problem = setupProblem();
  $("setup-check").className = "setup-check" + (problem ? " bad" : "");
  $("setup-check").textContent = problem || tiles + " tile" + (tiles === 1 ? "" : "s") + " on the board; " + (100 - tiles) + " in the bag and on the racks." +
    (rackText("setup-rack").length ? "" : " Give your rack to analyse.");
  $("setup-go").disabled = !!problem || !rackText("setup-rack").length;
}
function setupSet(sq, ch) {
  if (sq < 0) return;
  setup.board[sq] = ch;
}
function setupStep(dir) {
  if (!cursor) return;
  const r = Math.floor(cursor.sq / N), c = cursor.sq % N;
  if (cursor.down ? r + dir >= 0 && r + dir < N : c + dir >= 0 && c + dir < N) cursor.sq += dir * (cursor.down ? N : 1);
}
function setupType(L, shift) {
  if (!cursor) return;
  const blank = setup.blank || shift;
  setupSet(cursor.sq, blank ? L.toLowerCase() : L.toUpperCase());
  setup.blank = false;
  sfx.place();
  setupStep(1);
}
function setupBack() {
  if (!cursor) return;
  if (setup.board[cursor.sq] === ".") setupStep(-1);
  setupSet(cursor.sq, ".");
}
function setupKey(e) {
  const k = e.key;
  if (/^[a-z]$/i.test(k)) { e.preventDefault(); setupType(k, e.shiftKey); }
  else if (k === "Backspace") { e.preventDefault(); setupBack(); }
  else if (k === "Delete") { e.preventDefault(); if (cursor) setupSet(cursor.sq, "."); }
  else if (k === "Escape") { cursor = null; }
  else if (k === "Enter" && e.target.tagName !== "BUTTON") { e.preventDefault(); setupGo(); return; }
  else if (k.startsWith("Arrow") && cursor) {
    e.preventDefault();
    const r = Math.floor(cursor.sq / N), c = cursor.sq % N;
    if (k === "ArrowRight" && c < N - 1) cursor = { sq: cursor.sq + 1, down: false };
    if (k === "ArrowLeft" && c > 0) cursor = { sq: cursor.sq - 1, down: false };
    if (k === "ArrowDown" && r < N - 1) cursor = { sq: cursor.sq + N, down: true };
    if (k === "ArrowUp" && r > 0) cursor = { sq: cursor.sq - N, down: true };
  } else return;
  renderSetup();
}
async function setupGo() {
  if (!setup || setupProblem() || !rackText("setup-rack").length) return;
  const cgp = setupCgp(), lex = loadedLex || settings.lexicon;
  $("setup-go").disabled = true;
  $("setup-check").textContent = "Checking the position…";
  try {
    if (!worker) startWorker();
    await loadLexicon(lex);
    epoch++;
    const r = await ui("cgp " + cgp);
    if (!r.ok) throw new Error(r.error);
    closeSetup();
    G = Object.assign(newGameObject(), { mode: "analysis", lexicon: lex, cgp, clockMin: 0, assisted: true });
    await analysisFrom(r.state);
  } catch (err) {
    if (!setup) return;
    $("setup-check").className = "setup-check bad";
    $("setup-check").textContent = capital(err.message) + ".";
    $("setup-go").disabled = false;
  }
}
["setup-rack", "setup-opp", "setup-you", "setup-them"].forEach((id) => {
  $(id).addEventListener("input", () => { if (setup) renderSetup(); });
  $(id).addEventListener("keydown", (e) => { if (e.key === "Enter") { e.preventDefault(); setupGo(); } });
});
$("setup-go").onclick = setupGo;
$("setup-clear").onclick = () => { if (!setup) return; setup.board.fill("."); cursor = { sq: 7 * N + 7, down: false }; renderSetup(); };
$("setup-cancel").onclick = () => { closeSetup(); render(); openStart(); };
$("btn-setup").onclick = async () => {
  const lex = settings.lexicon;
  await withLoading(lex, async () => {
    if (busy) await restartEngine();
  });
  if (loadedLex === lex) openSetup(null);
};
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
  const wasBusy = busy;
  G = Object.assign(newGameObject(), { mode: "review", gcg: text, clockMin: 0, record: [], over: true });
  const lexLine = text.match(/^#lexicon\s+(\S+)/m);
  if (lexLine && /^(CSW|NWL)/i.test(lexLine[1])) G.lexicon = /^CSW/i.test(lexLine[1]) ? "CSW24" : "NWL23";
  else if (lexLine && /^OXENDICT/i.test(lexLine[1])) G.lexicon = "OXENDICT";
  else if (lexLine && /^(CEL|ECWL)/i.test(lexLine[1])) G.lexicon = "CEL";
  resetView();
  await withLoading(G.lexicon, async () => {
    if (wasBusy) await restartEngine();  // as in startGame: nothing still running for the old game carries on
    epoch++;
    await call({ write: { path: "/data/import.gcg", text } });
    const r = await ui("import /data/import.gcg");
    if (!r.ok) throw new Error("this record could not be read: " + r.error);
    G.human = 0;
    showState(r.state, null);
    await enterReview(r.state.history.length, { analyse: true });
  });
};
$("btn-start").onclick = startGame;
$("btn-start-close").onclick = () => { $("start").hidden = true; $("loading").hidden = true; };

// Zoom (phones): the board grows inside a window you can pan with a finger, and follows
// the cursor as you type.  The magnifier button, or a double tap on the board.
let zoomed = false;
function setZoom(on, sq) {
  zoomed = on;
  $("board-area").classList.toggle("zoomed", on);
  $("btn-zoom").setAttribute("aria-pressed", on);
  $("btn-zoom").title = on ? "Show the whole board" : "Zoom in";
  if (on) requestAnimationFrame(() => centerOn(sq != null ? sq : cursor ? cursor.sq : pending.size ? [...pending.keys()][0] : last.size ? [...last][0] : 7 * N + 7, false));
}
function centerOn(sq, smooth) {
  const sc = $("board-scroll"), el = squares[sq];
  if (!el) return;
  const r = el.getBoundingClientRect(), b = sc.getBoundingClientRect();
  sc.scrollBy({ left: r.left + r.width / 2 - (b.left + b.width / 2), top: r.top + r.height / 2 - (b.top + b.height / 2), behavior: smooth === false ? "auto" : "smooth" });
}
function keepVisible(sq) {
  const sc = $("board-scroll"), el = squares[sq];
  if (!el) return;
  const r = el.getBoundingClientRect(), b = sc.getBoundingClientRect(), m = r.width * 1.5;
  if (r.left < b.left + m || r.right > b.right - m || r.top < b.top + m || r.bottom > b.bottom - m) centerOn(sq);
}
$("btn-zoom").onclick = () => setZoom(!zoomed);
boardEl.addEventListener("dblclick", (e) => {
  const sqEl = e.target.closest(".sq");
  if (sqEl && matchMedia("(max-width: 700px)").matches) setZoom(!zoomed, +sqEl.dataset.sq);
});
$("btn-sort").onclick = sortRack;
buildBoard();
if (typeof ResizeObserver === "function") new ResizeObserver(() => boardEl.style.setProperty("--sqw", squares[0].offsetWidth + "px")).observe(boardEl);
openStart();
render();
// The engine starts compiling at once, and the word list chosen last time (or one that
// comes with the page) starts downloading, so Start has little left to wait for.
if (ENGINE_OK) {
  if (!worker) startWorker();
  if (store.get("lexicon", null) || BUNDLED.has(settings.lexicon)) prefetch(settings.lexicon);
  openLink();
}
