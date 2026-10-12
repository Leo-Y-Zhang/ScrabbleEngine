# Handover (state at 12 October 2026)

Read this first in a new session, then `experiments/README.md` sections 9–13.

**11–12 October: work on the browser version and the engine's interfaces (PRs #35–#50).**
No registered experiment ran and **the default search is unchanged** (fixed-work checksum
`349.2837`, natively and in the browser build). What changed:

- *Engine, without changing a decision:* word lists load about twice as fast (#37: a leave
  file loads into an empty table instead of over the built-in model, the `.klv2` reader builds
  no strings, the anagram indexes use a radix sort; saved leave tables byte-identical, games
  identical). `ui hint`, `ui review` and `go … json` give a solved endgame's best line
  (`line`, and `playedLine` in a review), and `ui hint`/`ui review` make the values of the five
  moves they show exact (#40: the solver proves only the best move's value; the others were
  upper bounds, 57 of 100 too high). New `ui infer [SECS]` reports what the opponent probably
  kept, by the inference the simulation already uses (#41). None of these paths is used by the
  engine's own choice of move.
- *Browser:* works offline once opened and installs as an app (#36, `web/coi-sw.js` keeps a
  copy of each file; `web/manifest.webmanifest`, `web/icons/`); finished games as links that
  open in the review (#38, `#game=` holds the moves, checked before they reach the engine,
  which takes `;` between commands); endgame puzzles solved by the exact solver, preferring a
  single answer (#39, #43); the best play to the end of a solved endgame in reviews, hints and
  puzzles (#40); "Estimate Tilefish's rack" in Practice (#41); a board editor in Analyse and
  "Analyse from here" in the review (#42, #44); back up and restore of games and stats (#44);
  the last fortnight's daily puzzles and a shareable result (#43, #45); exchange by keyboard
  (#45); the cards (start, end, games, help) take the keyboard focus and keep Tab inside
  (#48); fixes: a Practice game's review now judges both sides (#35), clocks run again after a
  reload in Play and the start screen can be closed (#46), and an endgame puzzle move that goes
  out no longer ends a game (#48: a wrong one locked the puzzle's remaining tries), and an
  imported record's moves are judged only from the racks it gives (#49: a record without
  racks sent the review's search an impossible rack, out of bounds in the engine; such moves
  now say why they are not judged).
- *Tests:* CI now drives the page itself (#47, `web/ui-test.js`: Playwright with the runner's
  Chrome plays a Practice game, reviews it, opens its link, solves both kinds of puzzle and
  analyses a board set up by hand, failing on any page error). The sanitizer job also fuzzes
  the `ui` commands (#50, `tools/fuzz_ui.py`: moves, positions and game records, legal,
  mutated and random), which is how #49's out-of-bounds read was found. The smoke test also
  checks endgame lines, `ui infer` and the review of imported records; the self-test checks
  the radix sort and the leave table's shortcuts.

Next steps are unchanged (below); for the browser, one idea not done: a daily endgame puzzle
(it needs a solve that finishes the same on slow devices).

**Added later on 4 October (section 11, no new games):** a reanalysis with the new
`tools/latebias.py` found that Macondo's own late-game estimates run as high as
Tilefish's (+15.9 against +12.9 points), that the "empties the bag" effect was the bag
size, that Tilefish's extra points against Macondo come partly after a game is settled
(games still open late are level, 50.3%), and that section 4's 41–100 conversion gap
shrinks from 10.8 to 3.0 points on fresh games. No late-game deficit against Macondo is
established. README notices were also tightened (not affiliated with Hasbro, Mattel,
HarperCollins, NASPA or Woogles; the stale 55.0% headline replaced by the current
level result; PRs #23 and #24 put those notices and self-hosted web fonts on main).

**Added later still (section 12):** Tilefish now accepts the referee's game history, so its
opponent-rack inference can run in matches for the first time (every earlier match measured
it without inference). Two screens were dispatched; see the table below and section 12.

**Added on 5 October (section 13, no new games):** a replay study (`tools/allocstudy.cpp`)
found that the middle-game search already picks the move a much longer 2-ply search would
pick from about 1.5 s on one thread (ENABLE, 60 positions), that its early pruning dropped no
better move there (1 of 1,053 in the late phase), and that no candidate ranked 31–60 beat the
top 30 (30 positions). So more samples or more candidates cannot improve the middle-game
choice after that; a gain at 20–60 s has to come from spending the time on a different
question (section 13 lists two; none is registered yet). Also fixed:
opponent-rack inference cut short by the clock weighed the first leaves in letter order
(mostly blanks and A's), and now weighs a fair random sample; a self-test checks it. A
complete pass gives the same model as before, and matches without history never infer, so
the default search is unchanged (an always-zero term was also removed from the static
evaluation; the fixed-work checksums are identical).

**Added on 6 October (section 9's result):** the tournament-budget match is complete. Job
36's re-run finished the run, `report.yml` recorded all 416 pairs, and section 9 now has its
results subsection. Tilefish (default, `iters=100000000`) against Macondo `simming 5`, CSW24,
60 s a move, four threads each: **48.62% (45.79% to 51.44%), −10 Elo (−29 to +10), +3.2
points a game (−2.2 to +8.7): level within the interval** under the registered rule. The
descriptive analyses registered in section 11 show no Tilefish advantage after a game is
settled at this budget (−0.5 points), level open games (46.7% of 287), Macondo converting
small late leads a little more often (no interval excludes zero), and both engines' late
estimates running high by similar amounts (+16.0 and +17.2).

## Defaults
The engine's default search is unchanged since v2.2.1 (fixed-work checksum `349.2837`). The
live app (GitHub Pages) plays the default. Options added and tested, all off by default:
`keep` (opponent-rack prior, no effect), `egk` (endgame look-ahead in late play-outs: +1.7
points a game against Macondo, proven, but no extra wins, so not promoted), `deep` (below).

## Runs of 4 October and how they ended
All on `experiment.yml`; `report.yml` recorded each one under "Results recorded automatically"
at the end of `experiments/README.md`, and sections 9, 10 and 12 now carry the prose.

| Run | What | Outcome |
|---|---|---|
| [37208427774](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37208427774) `tournament-60s-4t` | default (`iters=100000000`) vs Macondo `simming 5`, 60 s, 4 threads, 416 pairs, seed 9300 | 416 of 416 pairs (job 36 re-run): 48.62% (45.79% to 51.44%), +3.2: **level within the interval** (section 9) |
| [37209468373](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209468373) `deep-screen-frozen` | `deep=3,deepplies=4,deepfrac=0.5` vs frozen `v2.2.1`, 20 s, 200 pairs | 51.38%, +5.3: leg met |
| [37209469683](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209469683) `deep-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 50.12% < 52.25%: **section 10's screen fails** |
| [37219855057](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37219855057) `infer-screen-frozen` | inference with the game's history vs frozen `v2.2.1`, 20 s, 200 pairs | 49.12%: leg not met |
| [37219857657](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37219857657) `infer-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 49.88% < 52.25%: **section 12's screen fails** |

## Exact next steps
1. Section 13's proposals (a different use of the time after the middle-game search settles)
   are not registered. Register one before running any screen; nothing becomes the default
   from a screen. Section 9's result (level at 60 s with four threads, with the 20 s lead in
   points gone) is the baseline any such change has to beat at that budget.
2. MAGPIE has not been measured at 60 s with four threads; a match there would need its own
   registered section (rule, seed and size fixed before any game).
3. Never relax a rule after seeing data; never claim "strongest engine" beyond what section 9
   and the other registered matches show (section 9 allows only "level within the interval").
