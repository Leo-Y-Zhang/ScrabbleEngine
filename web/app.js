"use strict";
// Tilefish in the browser: the board, the rack and the game flow.  The engine runs in
// worker.js and answers the `ui` commands of tilefish.cpp with one line of JSON.

const N = 15;
const COLS = "ABCDEFGHIJKLMNO";
const VALUES = { A: 1, B: 3, C: 3, D: 2, E: 1, F: 4, G: 2, H: 4, I: 1, J: 8, K: 5, L: 1, M: 3, N: 1, O: 1, P: 3, Q: 10, R: 1, S: 1, T: 1, U: 1, V: 4, W: 4, X: 8, Y: 4, Z: 10 };
const LEVELS = { casual: "static+", strong: "sim:time=3", champion: "champion:time=10" };
const NOTES = {
  CSW24: "Collins Scrabble Words, the World Championship list. About 10 MB, downloaded once.",
  NWL23: "The North American tournament list. About 8 MB, downloaded once.",
  ENABLE: "A free public-domain list that comes with Tilefish.",
  casual: "Plays at once, from its evaluation of each move.",
  strong: "Simulates the best candidates for about 3 seconds.",
  champion: "Full search, about 10 seconds a move.",
};

// Premium squares of the standard board, by row.
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
  get(k, d) { try { return localStorage.getItem("tilefish." + k) || d; } catch (e) { return d; } },
  set(k, v) { try { localStorage.setItem("tilefish." + k, v); } catch (e) { /* private mode */ } },
};

// ---------- engine ----------
const worker = new Worker("worker.js");
let nextId = 1;
const waiting = new Map();
worker.onmessage = (e) => {
  const m = e.data;
  const w = waiting.get(m.id);
  if (!w) return;
  if (m.progress != null) { if (w.progress) w.progress(m.progress); return; }
  waiting.delete(m.id);
  if (m.error) w.reject(new Error(m.error)); else w.resolve(m.out);
};
function call(msg, progress) {
  return new Promise((resolve, reject) => {
    const id = nextId++;
    waiting.set(id, { resolve, reject, progress });
    worker.postMessage(Object.assign({ id }, msg));
  });
}
async function ui(cmd) {
  const out = await call({ run: "ui " + cmd });
  const line = out.trim().split("\n").pop();
  return JSON.parse(line);
}

// ---------- game state ----------
const settings = { lexicon: store.get("lexicon", "CSW24"), level: store.get("level", "strong"), first: store.get("first", "first") };
let S = null;              // the engine's state: board, rack, scores, history...
let rack = [];             // [{ch, used}] in the order the player arranged them
let pending = new Map();   // square -> {ch, slot}: tiles placed this turn ("a" = blank as A)
let order = [];            // squares in the order they were placed (for Backspace)
let cursor = null;         // {sq, down}
let selected = -1;         // rack slot picked up by a tap
let exchanging = false;
let xsel = new Set();
let busy = false;          // the engine is thinking
let last = new Set();      // squares of the latest move
let lastMine = false;
let checkSeq = 0;
let checked = null;        // {text, ok, score, error} for the current pending tiles

const boardEl = $("board"), rackEl = $("rack");
const squares = [];

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
  return t;
}

function buildBoard() {
  for (let s = 0; s < N * N; s++) {
    const d = document.createElement("div");
    d.className = "sq" + (PREMIUM[s] ? " " + PREMIUM[s] : "");
    d.dataset.sq = s;
    d.setAttribute("role", "gridcell");
    d.setAttribute("aria-label", (Math.floor(s / N) + 1) + COLS[s % N]);
    squares.push(d);
    boardEl.appendChild(d);
  }
}

const at = (s) => (S ? S.board[Math.floor(s / N)][s % N] : ".");
const filled = (s) => at(s) !== ".";
const occupied = (s) => filled(s) || pending.has(s);

function renderBoard(fresh) {
  for (let s = 0; s < N * N; s++) {
    const d = squares[s];
    d.textContent = "";
    d.classList.toggle("cursor", !!cursor && cursor.sq === s && !occupied(s));
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
  for (let i = 0; i < 7; i++) {
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
    rackEl.appendChild(t);
  }
}

function renderSide() {
  $("pts-you").textContent = S.you;
  $("pts-bot").textContent = S.bot;
  const mini = $("mini");
  mini.hidden = false;
  mini.innerHTML = '<span class="y">' + S.you + '</span><span class="sep">–</span><span class="b">' + S.bot + "</span>";
  $("score-you").classList.toggle("turn", !S.over && S.yourTurn);
  $("score-bot").classList.toggle("turn", !S.over && !S.yourTurn);
  $("bag").textContent = S.bag ? S.bag + " in the bag" : "bag empty";
  if (S.win != null) {
    $("chance").hidden = false;
    $("chance-fill").style.width = (100 * S.win).toFixed(1) + "%";
    $("chance-label").textContent = "Tilefish gives you a " + Math.round(100 * S.win) + "% chance of winning";
  }
  const log = $("log");
  log.textContent = "";
  if (!S.history.length) {
    const li = document.createElement("li");
    li.className = "empty";
    li.textContent = "No moves yet.";
    log.appendChild(li);
  }
  for (const h of S.history) {
    const li = document.createElement("li");
    li.className = h.who;
    const mv = document.createElement("span");
    mv.className = "mv";
    mv.textContent = h.type === "pass" ? "Pass" : h.type === "exchange" ? "Exchange " + h.move.replace(/^exch\s*/i, "") : h.move;
    const sc = document.createElement("span");
    sc.className = "sc";
    sc.textContent = h.type === "play" ? "+" + h.score : "";
    const tot = document.createElement("span");
    tot.className = "tot";
    tot.textContent = h.total;
    li.append(mv, sc, tot);
    log.appendChild(li);
  }
  log.scrollTop = log.scrollHeight;
  const counts = {};
  for (const ch of S.unseen) counts[ch] = (counts[ch] || 0) + 1;
  const un = $("unseen");
  un.textContent = "";
  for (const ch of Object.keys(counts).sort()) {
    const sp = document.createElement("span");
    sp.textContent = ch === "?" ? "Blank" : ch;
    if (counts[ch] > 1) {
      const n = document.createElement("span");
      n.className = "n";
      n.textContent = counts[ch];
      sp.appendChild(n);
    }
    un.appendChild(sp);
  }
  $("unseen-count").textContent = S.unseen.length;
}

function renderControls() {
  const mine = S && S.yourTurn && !busy && !S.over;
  $("btn-shuffle").disabled = !S;
  $("btn-recall").disabled = !mine || !pending.size;
  $("btn-exchange").disabled = !mine || S.bag < 7;
  $("btn-exchange").textContent = exchanging ? "Cancel" : "Exchange";
  $("btn-pass").disabled = !mine || exchanging;
  $("btn-hint").disabled = !mine || exchanging;
  const play = $("btn-play");
  if (exchanging) {
    play.textContent = xsel.size ? "Exchange " + xsel.size : "Exchange";
    play.disabled = !mine || !xsel.size;
  } else {
    const ready = mine && checked && checked.ok && checked.text === (buildMove() || {}).text;
    play.textContent = ready ? "Play " + checked.score : "Play";
    play.disabled = !ready;
  }
}

function setStatus(html, cls) {
  const el = $("status");
  el.className = "status" + (cls ? " " + cls : "");
  el.innerHTML = html;
}

function statusForTurn() {
  if (!S) return;
  if (S.over) return setStatus("Game over.");
  if (busy) return setStatus("Tilefish is thinking", "thinking");
  if (exchanging) return setStatus("Pick the tiles to put back in the bag.");
  if (!pending.size) return setStatus("Your move.");
  const mv = buildMove();
  if (mv && mv.error) return setStatus('<span class="bad">' + mv.error + "</span>");
  if (!checked || checked.text !== mv.text) return setStatus("&nbsp;");
  if (checked.ok) return setStatus("<b>" + esc(prettyMove(mv.text)) + "</b> scores " + checked.score + ".");
  setStatus('<span class="bad">' + esc(capital(checked.error)) + "</span>");
}

let toastTimer = 0;
function toast(text, ok) {
  const t = $("toast");
  t.textContent = text;
  t.className = "toast show" + (ok ? " ok" : "");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => { t.className = "toast" + (ok ? " ok" : ""); }, 2600);
}

const esc = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
const capital = (s) => s.charAt(0).toUpperCase() + s.slice(1);
const prettyMove = (t) => t.replace(/\./g, "·");

function render(fresh) {
  renderBoard(fresh);
  renderRack();
  if (S) renderSide();
  renderControls();
  statusForTurn();
}

// ---------- building a move from the tiles on the board ----------
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
  } else {
    down = oneCol;
  }
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
  if (!mv || mv.error || !S || !S.yourTurn || busy) { renderControls(); statusForTurn(); return; }
  const seq = ++checkSeq;
  const r = await ui("check " + mv.text);
  if (seq !== checkSeq) return;
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
    const done = (v) => {
      sheet.hidden = true;
      document.removeEventListener("keydown", onKey, true);
      resolve(v);
    };
    const onKey = (e) => {
      if (/^[a-z]$/i.test(e.key)) { e.preventDefault(); e.stopPropagation(); done(e.key.toLowerCase()); }
      else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); done(null); }
    };
    for (const L of "ABCDEFGHIJKLMNOPQRSTUVWXYZ") {
      const b = document.createElement("button");
      b.textContent = L;
      b.onclick = () => done(L.toLowerCase());
      box.appendChild(b);
    }
    sheet.onclick = (e) => { if (e.target === sheet) done(null); };
    document.addEventListener("keydown", onKey, true);
    sheet.hidden = false;
  });
}

async function place(slot, sq, letter) {
  if (!S || !S.yourTurn || busy || exchanging || occupied(sq) || !rack[slot] || rack[slot].used) return false;
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
  if (!cursor || !S || !S.yourTurn || busy || exchanging) return;
  advanceCursor();
  if (!cursor) return;
  const L = key.toUpperCase();
  let slot = freeSlot(L), letter = null;
  if (slot < 0) { slot = freeSlot("?"); letter = L; }
  if (slot < 0) { toast("You have no " + L + " and no blank."); return; }
  if (await place(slot, cursor.sq, letter)) {
    advanceCursor();
    render();
    checkPending();
  }
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
  // Keep the player's arrangement for tiles they still hold; new tiles go at the end.
  const left = state.rack.split("");
  const out = [];
  for (const r of keep) {
    const i = left.indexOf(r.ch);
    if (i >= 0) { out.push({ ch: r.ch, used: false }); left.splice(i, 1); }
  }
  for (const ch of left) out.push({ ch, used: false });
  return out;
}

function applyState(state, mine) {
  const before = S ? S.board : null;
  const fresh = new Set();
  if (before) {
    for (let s = 0; s < N * N; s++)
      if (before[Math.floor(s / N)][s % N] === "." && state.board[Math.floor(s / N)][s % N] !== ".") fresh.add(s);
  }
  const kept = rack.filter((r) => r && !r.used && !xsel.has(rack.indexOf(r)));
  S = state;
  rack = rackFrom(state, kept);
  pending = new Map();
  order = [];
  checked = null;
  selected = -1;
  exchanging = false;
  xsel = new Set();
  if (fresh.size) { last = fresh; lastMine = mine; }
  if (cursor && occupied(cursor.sq)) cursor = null;
  render(mine ? null : fresh);
}

async function submit() {
  if (!S || !S.yourTurn || busy) return;
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
  busy = true;
  renderControls();
  const r = await ui("move " + text);
  busy = false;
  if (!r.ok) { toast(capital(r.error)); render(); return; }
  hideHints();
  applyState(r.state, true);
  await botTurn();
}

async function botTurn() {
  if (!S || S.over) return finish();
  if (S.yourTurn) return;
  busy = true;
  render();
  const r = await ui("bot " + LEVELS[settings.level]);
  busy = false;
  if (!r.ok) { toast(capital(r.error)); render(); return; }
  applyState(r.state, false);
  const h = S.history[S.history.length - 1];
  if (h) toast(h.type === "play" ? "Tilefish played " + prettyMove(h.move) + " for " + h.score
    : h.type === "exchange" ? "Tilefish exchanged " + h.move.replace(/^exch\s*/i, "") + " tiles" : "Tilefish passed", true);
  if (S.over) finish();
}

function finish() {
  if (!S || !S.over) return;
  render();
  const diff = S.you - S.bot;
  $("end-title").textContent = diff > 0 ? "You won" : diff < 0 ? "Tilefish won" : "A tie";
  $("end-score").textContent = S.you + " – " + S.bot;
  const parts = [];
  if (S.endYou > 0) parts.push("You went out first and took " + S.endYou + " from Tilefish's rack.");
  else if (S.endBot > 0) parts.push("Tilefish went out first and took " + S.endBot + " from your rack.");
  else parts.push("The game ended after six scoreless turns.");
  if (S.botRack) parts.push("Tilefish was left with " + S.botRack + ".");
  $("end-detail").textContent = parts.join(" ");
  setTimeout(() => { $("end").hidden = false; }, 700);
}

// ---------- hints ----------
function hideHints() { $("hints").hidden = true; }

async function hint() {
  if (!S || !S.yourTurn || busy) return;
  busy = true;
  render();
  setStatus("Tilefish is looking at your rack", "thinking");
  const r = await ui("hint 1.5");
  busy = false;
  render();
  if (!r.ok) { toast(capital(r.error)); return; }
  const box = $("hints");
  box.textContent = "";
  const head = document.createElement("div");
  head.className = "panel-head";
  head.innerHTML = "<span>Tilefish suggests</span><span class=\"bag\">win chance</span>";
  box.appendChild(head);
  for (const m of r.hint.moves.slice(0, 5)) {
    const b = document.createElement("button");
    b.className = "hint-row";
    const exch = /^exch/i.test(m.move), pass = /^pass/i.test(m.move);
    b.innerHTML = "<span>" + esc(pass ? "Pass" : exch ? "Exchange " + m.move.replace(/^exch\s*/i, "") : m.move) + "</span><span class=\"sc\">" +
      (m.score ? "+" + m.score : "") + "</span><span class=\"wp\">" + (isFinite(m.win) ? Math.round(100 * m.win) + "%" : "") + "</span>";
    b.onclick = () => showHint(m.move);
    box.appendChild(b);
  }
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
  if (m) { r = +m[1] - 1; c = COLS.indexOf(m[2].toUpperCase()); }
  else { m = coord.match(/^([A-O])(\d+)$/i); down = true; c = COLS.indexOf(m[1].toUpperCase()); r = +m[2] - 1; }
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

// ---------- pointer: tap, drag and drop ----------
let drag = null;

function squareAt(x, y) {
  const el = document.elementFromPoint(x, y);
  const sq = el && el.closest(".sq");
  return sq ? +sq.dataset.sq : -1;
}
function rackSlotAt(x, y) {
  const el = document.elementFromPoint(x, y);
  if (!el || !el.closest("#rack")) return -2;
  const t = el.closest("[data-slot]");
  return t ? +t.dataset.slot : -1;
}

function startDrag(e, from) {
  drag = { from, x: e.clientX, y: e.clientY, moved: false, ghost: null, src: e.currentTarget || e.target };
  document.addEventListener("pointermove", moveDrag);
  document.addEventListener("pointerup", endDrag, { once: true });
}

function moveDrag(e) {
  if (!drag) return;
  if (!drag.moved && Math.hypot(e.clientX - drag.x, e.clientY - drag.y) < 6) return;
  if (!drag.moved) {
    drag.moved = true;
    const src = drag.from.slot != null ? rackEl.querySelector('[data-slot="' + drag.from.slot + '"]') : squares[drag.from.sq].firstChild;
    const box = src.getBoundingClientRect();
    drag.ghost = src.cloneNode(true);
    drag.ghost.classList.add("ghost");
    drag.ghost.classList.remove("sel", "pending");
    drag.ghost.style.width = box.width + "px";
    drag.ghost.style.height = box.height + "px";
    document.body.appendChild(drag.ghost);
    src.style.visibility = "hidden";
  }
  drag.ghost.style.left = e.clientX + "px";
  drag.ghost.style.top = e.clientY + "px";
  squares.forEach((d) => d.classList.remove("drop"));
  const sq = squareAt(e.clientX, e.clientY);
  if (sq >= 0 && !occupied(sq)) squares[sq].classList.add("drop");
}

async function endDrag(e) {
  document.removeEventListener("pointermove", moveDrag);
  const d = drag;
  drag = null;
  if (!d) return;
  squares.forEach((q) => q.classList.remove("drop"));
  if (d.ghost) d.ghost.remove();
  if (!d.moved) return tap(d.from);
  const sq = squareAt(e.clientX, e.clientY);
  const slotTo = rackSlotAt(e.clientX, e.clientY);
  if (d.from.slot != null) {
    const from = d.from.slot;
    if (sq >= 0 && !occupied(sq)) await place(from, sq);
    else if (slotTo >= -1) reorder(from, slotTo);
  } else {
    const p = pending.get(d.from.sq);
    if (sq >= 0 && !occupied(sq) && p) {
      pending.delete(d.from.sq);
      pending.set(sq, p);
      order = order.map((s) => (s === d.from.sq ? sq : s));
    } else if (sq !== d.from.sq) {
      unplace(d.from.sq);
    }
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
    if (exchanging) {
      if (xsel.has(i)) xsel.delete(i); else xsel.add(i);
    } else if (cursor && !occupied(cursor.sq) && S && S.yourTurn && !busy) {
      if (await place(i, cursor.sq)) { advanceCursor(); checkPending(); }
    } else {
      selected = selected === i ? -1 : i;
    }
    render();
    return;
  }
  // a tile placed this turn: back to the rack
  unplace(from.sq);
  render();
  checkPending();
}

boardEl.addEventListener("pointerdown", (e) => {
  const sqEl = e.target.closest(".sq");
  if (!sqEl) return;
  const sq = +sqEl.dataset.sq;
  if (pending.has(sq)) { e.preventDefault(); startDrag(e, { sq }); return; }
});
boardEl.addEventListener("click", async (e) => {
  const sqEl = e.target.closest(".sq");
  if (!sqEl || !S || S.over) return;
  const sq = +sqEl.dataset.sq;
  if (occupied(sq)) return;
  if (selected >= 0) {
    if (await place(selected, sq)) { render(); checkPending(); }
    return;
  }
  cursor = cursor && cursor.sq === sq ? { sq, down: !cursor.down } : { sq, down: false };
  hideHints();
  render();
});
rackEl.addEventListener("pointerdown", (e) => {
  const t = e.target.closest("[data-slot]");
  if (!t || !rack[+t.dataset.slot] || rack[+t.dataset.slot].used) return;
  e.preventDefault();
  startDrag(e, { slot: +t.dataset.slot });
});

// ---------- keyboard ----------
document.addEventListener("keydown", (e) => {
  if (!$("start").hidden || !$("end").hidden || !$("blank-picker").hidden) {
    if (e.key === "Enter" && !$("start").hidden) $("btn-start").click();
    return;
  }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  const k = e.key;
  if (/^[a-z]$/i.test(k)) { e.preventDefault(); typeLetter(k); }
  else if (k === "Backspace") { e.preventDefault(); backspace(); }
  else if (k === "Enter") { e.preventDefault(); submit(); }
  else if (k === "Escape") { recall(); hideHints(); exchanging = false; xsel = new Set(); cursor = null; render(); }
  else if (k === " ") { e.preventDefault(); shuffle(); }
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
  if (!rack.length) return;
  const idx = rack.map((_, i) => i);
  for (let i = idx.length - 1; i > 0; i--) {
    const j = Math.floor(Math.random() * (i + 1));
    [idx[i], idx[j]] = [idx[j], idx[i]];
  }
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
$("btn-hint").onclick = hint;
$("btn-exchange").onclick = () => {
  recall();
  hideHints();
  exchanging = !exchanging;
  xsel = new Set();
  cursor = null;
  render();
};
let passArmed = 0;
$("btn-pass").onclick = () => {
  const b = $("btn-pass");
  if (!passArmed) {
    b.textContent = "Confirm pass";
    b.classList.add("armed");
    passArmed = setTimeout(() => { passArmed = 0; b.textContent = "Pass"; b.classList.remove("armed"); }, 3000);
    return;
  }
  clearTimeout(passArmed);
  passArmed = 0;
  b.textContent = "Pass";
  b.classList.remove("armed");
  recall();
  playMove("pass");
};
$("btn-new").onclick = () => { $("start").hidden = false; };
$("btn-again").onclick = () => { $("end").hidden = true; $("start").hidden = false; };

// ---------- start ----------
function initSegments() {
  document.querySelectorAll(".seg").forEach((seg) => {
    const name = seg.dataset.name;
    const sync = () => {
      seg.querySelectorAll("button").forEach((b) => b.classList.toggle("on", b.dataset.v === settings[name]));
      if (name === "lexicon") $("lex-note").textContent = NOTES[settings.lexicon];
      if (name === "level") $("level-note").textContent = NOTES[settings.level];
    };
    seg.addEventListener("click", (e) => {
      const b = e.target.closest("button");
      if (!b) return;
      settings[name] = b.dataset.v;
      store.set(name, b.dataset.v);
      sync();
    });
    sync();
  });
}

$("btn-start").onclick = async () => {
  const btn = $("btn-start");
  btn.disabled = true;
  $("loading").hidden = false;
  $("loading-fill").style.width = "4%";
  $("loading-text").textContent = settings.lexicon === "ENABLE" ? "Loading the word list" : "Downloading " + settings.lexicon + " (once)";
  try {
    await call({ load: settings.lexicon }, (p) => { $("loading-fill").style.width = (100 * p).toFixed(0) + "%"; });
    $("loading-fill").style.width = "100%";
    const first = settings.first === "random" ? (Math.random() < 0.5 ? "first" : "second") : settings.first;
    const seed = new URLSearchParams(location.search).get("seed");  // ?seed=N replays one deal
    const r = await ui("new " + first + (/^\d+$/.test(seed || "") ? " " + seed : ""));
    if (!r.ok) throw new Error(r.error);
    S = null;
    rack = [];
    last = new Set();
    cursor = first === "first" ? { sq: 7 * N + 7, down: false } : null;
    $("chance").hidden = true;
    hideHints();
    applyState(r.state, false);
    $("start").hidden = true;
    $("btn-new").hidden = false;
    $("loading").hidden = true;
    botTurn();
  } catch (err) {
    $("loading-fill").style.width = "0";
    $("loading-text").textContent = capital(err.message) + (settings.lexicon !== "ENABLE" ? ". ENABLE works without a download." : ".");
  } finally {
    btn.disabled = false;
  }
};

buildBoard();
initSegments();
render();
