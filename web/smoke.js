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
  for (const f of ["ENABLE.txt", "ENABLE.klv2", "ENABLE.win"]) m.FS.writeFile("/data/" + f, fs.readFileSync(path.join(data, f)));
  const run = (c) => m.ccall("tf_run", "string", ["string"], [c]);
  const ui = (c) => JSON.parse(run("ui " + c).trim().split("\n").pop());
  check(/168551 words/.test(run("lexicon /data/ENABLE.txt")), "ENABLE loads");
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
    console.log("game " + (g + 1) + ": " + s.you + "-" + s.bot + " in " + turns + " turns; hint methods: " + [...phases].join(", "));
  }
  console.log("ok");
});
