// CI check for the browser build: plays whole games through the `ui` commands under
// Node, the human side taking the engine's own hints, so every phase runs: simulation,
// the one-tile pre-endgame and the endgame solver.   node web/smoke.js [GAMES]
"use strict";
const fs = require("fs");
const path = require("path");
const Tilefish = require("./dist/tilefish.js");

function check(cond, what) {
  if (!cond) {
    console.error("FAIL: " + what);
    process.exit(1);
  }
}

Tilefish().then((m) => {
  const data = path.join(__dirname, "dist", "data");
  m.FS.mkdir("/data");
  // The gzipped copies the page downloads unpack to exactly the files themselves.
  for (const f of ["ENABLE.kwg", "ENABLE.klv2", "OXENDICT.kwg", "OXENDICT.klv2"])
    check(Buffer.compare(require("zlib").gunzipSync(fs.readFileSync(path.join(data, f + ".gz"))), fs.readFileSync(path.join(data, f))) === 0, f + ".gz unpacks to " + f);
  // The files the page loads: the free lists compiled to .kwg by web/build.sh.
  for (const f of ["ENABLE.kwg", "ENABLE.klv2", "ENABLE.win"]) m.FS.writeFile("/data/" + f, fs.readFileSync(path.join(data, f)));
  const run = (c) => m.ccall("tf_run", "string", ["string"], [c]);
  const ui = (c) => JSON.parse(run("ui " + c).trim().split("\n").pop());
  check(/ENABLE: 168551 words/.test(run("lexicon /data/ENABLE.kwg")), "ENABLE loads");
  // The one-tile pre-endgame searches on the calling thread in this single-threaded
  // build.  A deterministic static player plays both sides until one tile is left.
  run("player static");
  let peg = null;
  for (let seed = 1; seed <= 40 && !peg; seed++) {
    run("new " + seed);
    for (let k = 0; k < 60; k++) {
      const st = ui("state").state;
      if (st.over) break;
      if (st.bag === 1) {
        run("player static+");  // static play plus the endgame and pre-endgame solvers
        peg =JSON.parse(run("go 0.3 json").trim().split("\n").pop());
        break;
      }
      run("auto 1");
    }
  }
  check(peg && /pre-endgame/.test(peg.method), "one tile in the bag runs the pre-endgame solver");
  console.log("pre-endgame: " + peg.best + " (" + peg.method + ")");
  run("player champion");
  const games = +(process.argv[2] || 2);
  for (let g = 0; g < games; g++) {
    let r = ui("new " + (g % 2 ? "second" : "first") + " " + (11 + g));
    check(r.ok && r.state.rack.length === 7 && r.state.bag === 86, "new game deals 7 tiles each");
    let turns = 0, phases = new Set();
    while (!r.state.over && turns < 80) {
      if (r.state.yourTurn) {
        const h = ui("hint 0.2");
        check(h.ok && h.hint.moves.length > 0, "hint returns candidates");
        phases.add(h.hint.method);
        const c = ui("check " + h.hint.best);
        check(c.ok || /^(exch|pass)/.test(h.hint.best), "the engine's own hint passes check: " + h.hint.best + " " + c.error);
        r = ui("move " + h.hint.best);
        check(r.ok, "hint move is accepted: " + h.hint.best + " " + r.error);
      } else {
        r = ui("bot sim:time=0.2");
        check(r.ok, "engine move: " + r.error);
      }
      turns++;
    }
    const s = r.state;
    check(s.over, "game ends within 80 turns");
    check(typeof s.botRack === "string", "engine rack revealed at the end");
    const onBoard = s.board.join("").replace(/\./g, "").length;
    check(onBoard + s.rack.length + s.botRack.length + s.bag === 100, "100 tiles accounted for");
    // The last mover's total includes the end-of-game rack adjustment.
    const last = s.history[s.history.length - 1];
    check(last.total === (last.who === "you" ? s.you : s.bot), "the last total matches the score");
    check(!ui("move pass").ok && !ui("bot").ok, "no moves after the end");
    // The game review's verdicts: the move's own estimate, the scores and the alternatives,
    // in the simulated middle game and in the solved endgame alike.
    for (const n of [0, s.history.length - 1]) {
      const v = ui("review " + n + " 0.2");
      check(v.ok && typeof v.bestScore === "number" && typeof v.playedScore === "number" && v.alts.length > 0 && typeof v.playedWin === "number",
        "review of move " + (n + 1) + " has the played move's estimate and alternatives: " + JSON.stringify(v).slice(0, 300));
    }
    console.log("game " + (g + 1) + ": " + s.you + "-" + s.bot + " in " + turns + " turns; hint methods: " + [...phases].join(", "));
    // Take-back and replay: the exact move record rebuilds the identical game (the seed fixes
    // every draw), and taking two moves back then replaying them changes nothing.
    const game = (st) => JSON.stringify(Object.assign({}, st, { win: undefined }));  // the estimate is not part of the game
    const rec = ui("record").moves;
    check(rec.length === s.history.length, "the record has every move");
    check(ui("undo").ok && ui("undo").ok, "take back two moves");
    for (const mv of rec.slice(-2)) check(ui("force " + mv).ok, "replay " + mv);
    check(game(ui("state").state) === game(s), "take-back then replay restores the identical game");
    ui("new " + (g % 2 ? "second" : "first") + " " + (11 + g));
    for (const mv of rec) check(ui("force " + mv).ok, "replay from the seed: " + mv);
    check(game(ui("state").state) === game(s), "seed plus record rebuilds the identical game");
    const imp = ui("gcg");
    m.FS.writeFile("/data/g.gcg", imp.gcg);
    const back = ui("import /data/g.gcg");
    check(back.ok && JSON.stringify(back.state.board) === JSON.stringify(s.board) && back.state.moves === s.history.length, "GCG export then import gives the same board and moves");
  }
  // The Oxford-spelling list loads with its own leave values and win model, and plays.
  for (const f of ["OXENDICT.kwg", "OXENDICT.klv2", "OXENDICT.win"]) m.FS.writeFile("/data/" + f, fs.readFileSync(path.join(data, f)));
  const ox = run("lexicon /data/OXENDICT.kwg");
  check(/OXENDICT: 188980 words/.test(ox) && /OXENDICT\.klv2/.test(ox) && /OXENDICT\.win/.test(ox), "OXENDICT loads with its leaves and win model");
  let r = ui("new first 5");
  for (let k = 0; k < 4 && !r.state.over; k++) {
    const h = ui("hint 0.2");
    check(h.ok && ui("move " + h.hint.best).ok, "OXENDICT: hint move is accepted");
    r = ui("bot sim:time=0.2");
    check(r.ok, "OXENDICT: engine move: " + r.error);
  }
  console.log("OXENDICT: " + r.state.you + "-" + r.state.bot + " after 4 moves each");
  console.log("ok");
});
