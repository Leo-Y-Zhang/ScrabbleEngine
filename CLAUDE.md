# Working on Tilefish

Tilefish is a Scrabble engine in one C++ file (`tilefish.cpp`) with a browser version
(`web/`) published on GitHub Pages at https://leo-y-zhang.github.io/TileFish/ (the repository,
now Leo-Y-Zhang/TileFish, was called ScrabbleEngine until 13 October 2026).
Read `docs/HANDOVER.md` before engine or experiment work.

## How the owner wants changes shipped
- Work on a branch, open a pull request, and as soon as CI is green **merge it into
  `main` and confirm the Pages deploy** (`web.yml` on `main`) succeeded. Do not wait to
  be asked: merged and live is the definition of done.
- Before pushing, run the checks below; one validated push beats several speculative ones.
- The site must stay **free**: no accounts, servers, trackers or paid services. Everything
  runs in the visitor's browser.
- **No risk of trouble** (the owner's rule since 13 October 2026; `docs/DISTRIBUTING.md`):
  Tilefish never hosts or fetches a word list it has no right to. Collins 2024 and NWL 2023
  stay on offer as the player's own copy, loaded from their device, with Tilefish's own leave
  values and win models; ENABLE, British English in Oxford spelling (OXENDICT) and the Common
  English Lexicon (CEL) come with it and must stay. Build Collins or NWL in only under a
  licence from HarperCollins or NASPA. SCRABBLE® only says whom Tilefish is for, never names
  it; the board keeps its own colours. A lawyer reviews any paid or app-store version first.
  The strength tests may still fetch the lists onto GitHub's machines for a job
  (`tools/research-lexicon.sh`); nothing from them is published.
- No word lookup in the app: it would be a cheat during play.

## Build and check
- Native: `g++ -O2 -std=c++17 -pthread -Wall -Werror tilefish.cpp -o tilefish` (CI also
  builds with clang++ and MSVC), then `./tilefish --threads 2 "selftest quick"`.
- Sanitizers: CI builds with `-fsanitize=address,undefined` and runs the self-test and
  `tools/fuzz_ui.py` (the `ui` commands the page sends, fuzzed; it found an out-of-bounds read
  in the review of imported records). Run it longer with other seeds after changing a `ui` command.
- Fixed-work checksum: `./tilefish --lexicon ENABLE.txt "benchsim 0 1 3000"` must print
  `checksum 349.2837`, and the browser build must print the same (CI compares them). A
  speed-up that changes it changes the search, and needs a registered experiment.
- Browser: `sh web/build.sh` (Emscripten 6.0.11; install with emsdk if missing), then
  `node web/smoke.js 2`, then `node web/ui-test.js` (drives the page in a browser through a
  game, the review, a game link, the puzzles and the board editor; needs
  `npm install --no-save playwright@1.56.1`, or the preinstalled one with
  `NODE_PATH=/opt/node22/lib/node_modules`; CI runs it with the runner's Chrome), then try it: `python3 -m http.server -d web/dist` and open
  http://localhost:8000 (Playwright with the preinstalled Chromium works headless).
- `web/build.sh` versions every file the page loads; never hand-edit `web/dist`. It makes
  two engines: `tilefish.js` (one core) and `tilefish-mt.js` (a search thread per core,
  up to 8). `web/coi-sw.js` makes the page cross-origin isolated so the second can run (and keeps
  a copy of each file, so the page opens offline);
  `web/worker.js` falls back to the first wherever it cannot. The page shortens timed
  searches on several cores (`speed()` in `app.js`), so levels, hints and reviews answer
  sooner while still searching more than one core did.
- Speed work must keep the checksum: profile-guided builds and WebAssembly SIMD were
  measured and gave nothing; WebAssembly exceptions (`-fwasm-exceptions`) and threads did.

## Engine rules (from the experiments)
- The default search changes only through a registered experiment in
  `experiments/README.md`: rule, seed and size fixed before any game, never relaxed after
  seeing data. Screens never promote a default on their own.
- Strength claims stay within what the registered matches show (README "How strong is
  it?"; section 9 allows only "level within the interval" against Macondo at 60 s).

## Layout
- `tilefish.cpp`: lexicon (KWG), move generator, leave values, simulation, endgame and
  pre-endgame solvers, inference, GCG/CGP, the CLI, and the `ui` commands the page uses
  (`cmd_ui`: new, move, check, bot, hint, review, judge, moves, rewind, import, ...).
- `web/app.js`: the page (game record + clocks; replays through `ui force`), review,
  puzzles, stats; `web/worker.js` runs the engine off the main thread; `web/style.css`
  holds the one palette (paper, felt, terracotta, maple) for light and dark.
- `tools/`: referee, analysis and experiment scripts; `experiments/`: registered
  experiments and raw logs.

## Style
- Commit messages: a short summary line, then plain sentences on what changed and why.
- Prose in docs and UI text: British spelling, plain words, no hype.
