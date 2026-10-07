# Working on Tilefish

Tilefish is a Scrabble engine in one C++ file (`tilefish.cpp`) with a browser version
(`web/`) published on GitHub Pages at https://leo-y-zhang.github.io/ScrabbleEngine/.
Read `docs/HANDOVER.md` before engine or experiment work.

## How the owner wants changes shipped
- Work on a branch, open a pull request, and as soon as CI is green **merge it into
  `main` and confirm the Pages deploy** (`web.yml` on `main`) succeeded. Do not wait to
  be asked: merged and live is the definition of done.
- Before pushing, run the checks below; one validated push beats several speculative ones.
- The site must stay **free**: no accounts, servers, trackers or paid services. Everything
  runs in the visitor's browser.
- Keep the word lists: Collins 2024 and NWL 2023 (fetched from MAGPIE-DATA, never hosted
  here), ENABLE, and British English in Oxford spelling (OXENDICT), which must stay.
- No word lookup in the app: it would be a cheat during play.

## Build and check
- Native: `g++ -O2 -std=c++17 -pthread -Wall -Werror tilefish.cpp -o tilefish` (CI also
  builds with clang++ and MSVC), then `./tilefish --threads 2 "selftest quick"`.
- Fixed-work checksum: `./tilefish --lexicon ENABLE.txt "benchsim 0 1 3000"` must print
  `checksum 349.2837`, and the browser build must print the same (CI compares them). A
  speed-up that changes it changes the search, and needs a registered experiment.
- Browser: `sh web/build.sh` (Emscripten 6.0.11; install with emsdk if missing), then
  `node web/smoke.js 2`, then try it: `python3 -m http.server -d web/dist` and open
  http://localhost:8000 (Playwright with the preinstalled Chromium works headless).
- `web/build.sh` versions every file the page loads; never hand-edit `web/dist`. It makes
  two engines: `tilefish.js` (one core) and `tilefish-mt.js` (a search thread per core,
  up to 8). `web/coi-sw.js` makes the page cross-origin isolated so the second can run;
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
