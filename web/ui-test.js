// CI check of the page itself, not only the engine: drives the built page (web/dist) in a
// real browser through what a visitor does, and fails on any page error.  A Practice game
// is played with the page's own controls (a hint, a take-back), then to the end; the review
// judges both sides; the game's link opens in a fresh browser; the daily puzzle, the day's
// endgame and another endgame puzzle are solved; the cards keep the keyboard focus; a board
// set up by hand is analysed.
//   npm install --no-save playwright@1.56.1   (once; it can use the system's Chrome)
//   node web/ui-test.js
"use strict";
const http = require("http");
const fs = require("fs");
const path = require("path");
let chromium;
try {
  ({ chromium } = require("playwright"));
} catch (e) {
  console.error("web/ui-test.js needs Playwright: npm install --no-save playwright@1.56.1");
  process.exit(2);
}

const DIST = path.join(__dirname, "dist");
const TYPES = { ".html": "text/html", ".js": "text/javascript", ".css": "text/css", ".wasm": "application/wasm", ".json": "application/json",
  ".webmanifest": "application/manifest+json", ".svg": "image/svg+xml", ".png": "image/png", ".woff2": "font/woff2", ".txt": "text/plain" };
function serve() {
  const server = http.createServer((req, res) => {
    let p = decodeURIComponent(new URL(req.url, "http://x").pathname);
    if (p.endsWith("/")) p += "index.html";
    const file = path.join(DIST, path.normalize(p));
    if (!file.startsWith(DIST) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end(); return; }
    res.writeHead(200, { "Content-Type": TYPES[path.extname(file)] || "application/octet-stream" });
    fs.createReadStream(file).pipe(res);
  });
  return new Promise((resolve) => server.listen(0, "127.0.0.1", () => resolve(server)));
}

const errors = [];
function check(cond, what) {
  if (!cond) throw new Error("FAIL: " + what);
  console.log("  ok    " + what);
}
function watch(page, tag) {
  page.on("pageerror", (e) => errors.push(tag + " page error: " + e.message));
  page.on("console", (m) => { if (m.type() === "error") errors.push(tag + " console error: " + m.text()); });
}
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

async function launch() {
  try { return await chromium.launch(); } catch (e) { return chromium.launch({ channel: "chrome" }); }  // Playwright's own browser, else the system's Chrome
}

(async () => {
  const server = await serve();
  const base = "http://localhost:" + server.address().port + "/";
  const browser = await launch();
  const ctx = await browser.newContext({ viewport: { width: 1280, height: 900 } });
  await ctx.grantPermissions(["clipboard-read", "clipboard-write"], { origin: base.slice(0, -1) });
  const p = await ctx.newPage();
  watch(p, "page");
  try {
    // The page makes itself cross-origin isolated on the first visit (one reload).
    await p.goto(base + "?seed=2026");
    await p.waitForFunction(() => self.crossOriginIsolated, null, { timeout: 30000 });
    check(true, "the page is cross-origin isolated (service worker)");
    await sleep(500);
    await p.evaluate(() => localStorage.setItem("tilefish.reviewSecs", "0.3"));
    await p.click('#start [data-v="practice"]');
    await p.click('#start [data-v="casual"]');
    await p.click('#start [data-v="ENABLE"]');
    await p.click('#start [data-name="clock"] [data-v="0"]');
    await p.click("#btn-start");
    const myTurn = () => p.waitForFunction(() => S && !busy && (humanTurn() || G.over), null, { timeout: 60000 });
    await myTurn();
    check(await p.evaluate(() => S.rack.length === 7 && S.bag === 86), "a Practice game starts with seven tiles each");

    // A hint, its third step, its first move put on the board, and Play.
    await p.click("#btn-hint");
    await p.waitForFunction(() => !busy && !document.getElementById("hints").hidden, null, { timeout: 30000 });
    await p.click("#hint-more");
    await p.click("#hint-more");
    await p.click("#hints [data-move]");
    await p.waitForFunction(() => !document.getElementById("btn-play").disabled, null, { timeout: 10000 });
    await p.click("#btn-play");
    await myTurn();
    check(await p.evaluate(() => S.history.length === 2), "a hinted move is played and Tilefish answers");
    await p.click("#btn-takeback");
    await p.waitForFunction(() => S && !busy && G.record.length === 0, null, { timeout: 30000 });
    check(true, "take-back returns to the start");

    // To the end with the best static move each turn, through the page's own playMove.
    for (let i = 0; i < 80; i++) {
      await myTurn();
      if (await p.evaluate(() => G.over)) break;
      const mv = await p.evaluate(async () => (await ui("moves 1")).moves[0].move);
      await p.evaluate((m) => playMove(m), mv);
    }
    await p.waitForSelector("#end:not([hidden])", { timeout: 30000 });
    check(true, "the game ends with its card: " + (await p.textContent("#end-title")) + " " + (await p.textContent("#end-score")));

    // The review judges both sides (a Practice game arrives with its own moves rated).
    await p.click("#btn-review");
    await p.waitForFunction(() => !analysing && unjudged().length === 0, null, { timeout: 240000 });
    const judged = await p.evaluate(() => [Object.keys(G.verdicts).length, S.history.length, document.querySelectorAll(".acc-n").length]);
    check(judged[0] === judged[1] && judged[2] === 2, "the review judges all " + judged[1] + " moves and gives both accuracies");

    // The game's link, opened in a fresh browser, shows the same game in the review.
    await p.click('#exports button:has-text("Copy game link")');
    await sleep(300);
    const link = await p.evaluate(() => navigator.clipboard.readText());
    check(/#game=z[A-Za-z0-9_-]+$/.test(link), "Copy game link gives a #game= link (" + link.length + " characters)");
    const ctx2 = await browser.newContext({ viewport: { width: 1280, height: 900 } });
    const q = await ctx2.newPage();
    watch(q, "link page");
    await q.goto(base);
    await q.waitForFunction(() => self.crossOriginIsolated, null, { timeout: 30000 });
    await q.evaluate(() => localStorage.setItem("tilefish.reviewSecs", "0.3"));
    await q.goto(link);
    await q.waitForFunction(() => G && G.mode === "review" && S && S.history.length > 0 && !analysing && unjudged().length === 0, null, { timeout: 240000 });
    check(await q.evaluate(() => S.history.length) === judged[1] && (await q.evaluate(() => location.hash)) === "", "the link opens the same game, judged, and clears the address");

    // "Analyse from here", then a hint in the analysis.
    await q.evaluate(() => enterReview(6));
    await sleep(200);
    await q.click("#rv-analyse");
    await q.click("#setup-go");
    await q.waitForFunction(() => G && G.mode === "analysis" && S && !busy, null, { timeout: 60000 });
    await q.click("#btn-hint");
    await q.waitForFunction(() => !busy && !document.getElementById("hints").hidden, null, { timeout: 30000 });
    check(true, "Analyse from here opens the position, and a hint answers in it");
    await ctx2.close();

    // The daily puzzle: a weak move is refused, the answer solves it.
    await p.click("#btn-puzzle");
    await p.waitForFunction(() => G && G.mode === "puzzle" && G.puzzle.answer && !busy && document.querySelector(".pz-task"), null, { timeout: 60000 });
    const worst = await p.evaluate(async () => { const r = await ui("moves 40"); const ps = r.moves.filter((m) => !/^(exch|pass)/i.test(m.move)); return ps[ps.length - 1].move; });
    for (const mv of [worst, await p.evaluate(() => G.puzzle.answer)]) {
      await p.evaluate((m) => showHint(m), mv);
      await p.waitForFunction(() => !document.getElementById("btn-play").disabled, null, { timeout: 10000 });
      await p.click("#btn-play");
      await p.waitForFunction((n) => !busy && G.puzzle.tries === n, mv === worst ? 1 : 2, { timeout: 60000 });
    }
    check(await p.evaluate(() => G.puzzle.solved && G.puzzle.tries === 2), "the daily puzzle refuses a weak move and takes the answer");

    // The day's endgame, from the daily puzzle's switch: solved by its answer, logged apart.
    await p.click('.pz-kind button[data-kind="endgame"]');
    await p.waitForFunction(() => G.puzzle.kind === "endgame" && G.puzzle.daily && G.puzzle.answer && !busy && document.querySelector(".pz-task"), null, { timeout: 300000 });
    await p.evaluate((m) => showHint(m), await p.evaluate(() => G.puzzle.answer));
    await p.waitForFunction(() => !document.getElementById("btn-play").disabled, null, { timeout: 10000 });
    await p.click("#btn-play");
    await p.waitForFunction(() => !busy && (G.puzzle.solved || G.puzzle.tries > 0), null, { timeout: 60000 });
    check(await p.evaluate(() => G.puzzle.solved && !!store.json("puzzles", {})["eg:" + G.puzzle.daily] && /Daily endgame/.test(document.querySelector("#puzzle .panel-head").textContent)),
      "the day's endgame opens from the daily puzzle, is solved by its answer and logged apart");

    // An endgame puzzle (the next one, not a daily one), solved exactly, with the best line shown.
    await p.click("#pz-next");
    await p.waitForFunction(() => G.puzzle.kind === "endgame" && !G.puzzle.daily && G.puzzle.answer && !busy && document.querySelector(".pz-task"), null, { timeout: 90000 });
    await p.evaluate((m) => showHint(m), await p.evaluate(() => G.puzzle.answer));
    await p.waitForFunction(() => !document.getElementById("btn-play").disabled, null, { timeout: 10000 });
    await p.click("#btn-play");
    await p.waitForFunction(() => !busy && (G.puzzle.solved || G.puzzle.tries > 0), null, { timeout: 60000 });
    check(await p.evaluate(() => G.puzzle.solved && !!document.querySelector("#puzzle .coach-line")), "an endgame puzzle is solved by its answer, and its best line is shown");
    // Even when the answer goes out, a puzzle is not a game: no end card, nothing filed.
    await sleep(700);
    check(await p.evaluate(() => !G.over && document.getElementById("end").hidden && !store.json("games", []).some((g) => g.game.mode === "puzzle")),
      "a solved puzzle opens no end card and is not filed under Games");

    // A card takes the keyboard focus and keeps Tab inside itself; Help gives it back.
    await p.click("#btn-help");
    await sleep(100);
    await p.keyboard.press("Escape");
    await sleep(100);
    check(await p.evaluate(() => document.activeElement.id === "btn-help"), "Help, closed with Escape, gives the focus back to its button");
    await p.click("#btn-new");
    await sleep(100);
    for (let i = 0; i < 30; i++) await p.keyboard.press("Tab");
    check(await p.evaluate(() => document.getElementById("start").contains(document.activeElement)), "the start card keeps the keyboard focus inside itself");

    // A board set up by hand: a word, a rack, the analysis.
    await p.click('#start [data-v="analysis"]');
    await p.click("#btn-setup");
    await p.waitForSelector("#setup:not([hidden])", { timeout: 60000 });
    await p.click('.sq[data-sq="' + (7 * 15 + 6) + '"]');
    await p.keyboard.type("QUIZ");
    await p.fill("#setup-rack", "aeinrst");
    await p.click("#setup-go");
    await p.waitForFunction(() => G && G.mode === "analysis" && S && !busy, null, { timeout: 60000 });
    check(await p.evaluate(() => S.board[7].includes("QUIZ") && S.rack === "AEINRST"), "a board set up by hand is analysed as entered");

    check(errors.length === 0, "no page or console errors" + (errors.length ? ":\n    " + errors.join("\n    ") : ""));
    console.log("ok");
    await browser.close();
    server.close();
    process.exit(0);
  } catch (e) {
    console.error(e.message);
    if (errors.length) console.error(errors.join("\n"));
    await browser.close();
    server.close();
    process.exit(1);
  }
})();
