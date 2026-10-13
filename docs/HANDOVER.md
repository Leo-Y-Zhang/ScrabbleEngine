# Handover (state at 12 October 2026)

Read this first in a new session, then `experiments/README.md` sections 9–14.

**13 October, later: no risk (the owner left the decisions to the project).** Tilefish no
longer fetches Collins or NWL from anywhere: a player loads their own copy (the start card's
"Load my copy", kept on the device by `web/worker.js`, or `--lexicon` / `CSW24.txt` beside the
program), and copies earlier versions fetched are deleted from browsers. Tilefish's own leave
values for both lists (`CSW24.klv2`: 800,000 self-play games, `NWL23.klv2`: 400,000, both from
ENABLE's values) come with the site and the downloads. Against MAGPIE's in 40,000-game static
matches: 49.6% and 49.8% (both +/- 0.5), level; registered matches still use MAGPIE's. The
`get-lexicon` scripts and their Windows CI job are gone; the strength tests fetch the lists
and MAGPIE's leave values with `tools/research-lexicon.sh`, so registered matches keep their
conditions. The board's bonus squares have Tilefish's own colours. The default list for new
visitors is ENABLE. **Owner's step outstanding:** renaming the repository to Tilefish (the
proxy refuses settings changes from a session); then change the site's address everywhere
(CLAUDE.md lists where). A lawyer reviews any paid or app-store version first.

**13 October: word lists, trademarks and tournament rules (PRs #54 and #55).** No
registered experiment ran, and **the default search is unchanged** (checksum `349.2837`).

- *Word lists:* CEL, the Common English Lexicon (68,135 everyday words, MIT licence), comes
  with Tilefish as a fourth free list (`CEL.txt` from `tools/make_cel.py`; leave values and
  win model learned from 800,000 self-play games, which beat ENABLE's on CEL: 51.6%, +3.5).
  Collins 2024 and NWL2023 were checked and are the current editions; MAGPIE-DATA has nothing
  newer. Older editions are left out on purpose.
- *Trademarks:* titles call Tilefish a crossword game engine; SCRABBLE® appears only to say
  whom it is for, with the right owners (Hasbro in the USA and Canada, J.W. Spear & Sons, a
  Mattel company, elsewhere). `docs/DISTRIBUTING.md` says what may be shared, what needs
  permission (Collins and NWL: only a licence makes them risk-free) and what an app-store or
  paid version needs. The owner asked for no risk; two open choices are theirs: licences from
  NASPA and HarperCollins, and renaming the repository (its name and the site's address
  contain the mark; renaming moves the site and breaks old links).
- *Tournament rules* (Play and Practice): Casual (words checked, as before), NASPA (double
  challenge with NWL and the free lists, 5 points a word with Collins) and WESPA / ABSP (5 or
  10 points a word, or a free challenge). Words are not marked as they are placed; a phony is
  played with `ui phony` and comes off (Tilefish always challenges, as it knows every word);
  the player's challenge of Tilefish's play uses `ui lastwords`, then `ui bonus N` or a pass
  (double). The record keeps both (`phony MOVE`, `MOVE bonus N`), so take-backs, reloads,
  links and GCG export and import carry them. WESPA overtime: 10 points a minute or part, and
  10 minutes over forfeits with a margin of at least 100. Sources: WESPA rules (via search;
  the association sites are blocked from the cloud environment), Macondo's challenge code.
  Not done: Tilefish never plays phonies, the last play of a game cannot be challenged, and
  Two players keeps words checked.

**11–12 October: work on the browser version and the engine's interfaces (PRs #35–#51).**
No registered experiment ran in this work (section 14's screen came after it, below), and
**the default search is unchanged** (fixed-work checksum `349.2837`, natively and in the
browser build). What changed:

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

The daily endgame (#51) is chosen the same way on every device: its seeds are tried in a fixed
order with no clock, and each position is solved with a budget in search nodes, not seconds
(`ui hint 0 nodes=N`: one thread, a table emptied for each solve, a fixed salt; N nodes for
the solve and N more shared by the exact values shown, N = 100,000 in `app.js`), so which
positions are solved, and their values, depend on the position alone. A time limit could not
do this: some 10-tile endgames take over a minute on one thread, so a slow device would pass
over positions a fast one solves. Once chosen, a day's endgame is kept on the device
(`DAILY_EG_VERSION` in its key: change it whenever the rule changes). The self-test and the
smoke test check that such a solve gives the same answer on one thread and several, after
other solves.

Next steps are unchanged (below).

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

**Added on 12 October (section 14):** a registered screen of section 13's first proposal, the
top three finalists played out to the end of the game for the last three quarters of a move
(`champion:deep=3,deepplies=40,deepfrac=0.25`, no code change). It **fails**: 47.88%, −1.8
points a game against frozen `v2.2.1` (needs > 50% and a positive spread); 54.12% against
Macondo on the `fresh-9200` deals (needs ≥ 52.25%: met, +1.88 points over the default on the
same deals, −4.25 to +8.12). The play-outs changed the choice in about 21% of middle-game
moves, but no effect shows either way. The default stays.

## Defaults
The engine's default search is unchanged since v2.2.1 (fixed-work checksum `349.2837`). The
live app (GitHub Pages) plays the default. Options added and tested, all off by default:
`keep` (opponent-rack prior, no effect), `egk` (endgame look-ahead in late play-outs: +1.7
points a game against Macondo, proven, but no extra wins, so not promoted), `deep` (below).

## Registered runs and how they ended
All on `experiment.yml`; `report.yml` recorded each one under "Results recorded automatically"
at the end of `experiments/README.md`, and sections 9, 10, 12 and 14 now carry the prose.

| Run | What | Outcome |
|---|---|---|
| [37208427774](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37208427774) `tournament-60s-4t` | default (`iters=100000000`) vs Macondo `simming 5`, 60 s, 4 threads, 416 pairs, seed 9300 | 416 of 416 pairs (job 36 re-run): 48.62% (45.79% to 51.44%), +3.2: **level within the interval** (section 9) |
| [37209468373](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209468373) `deep-screen-frozen` | `deep=3,deepplies=4,deepfrac=0.5` vs frozen `v2.2.1`, 20 s, 200 pairs | 51.38%, +5.3: leg met |
| [37209469683](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209469683) `deep-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 50.12% < 52.25%: **section 10's screen fails** |
| [37219855057](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37219855057) `infer-screen-frozen` | inference with the game's history vs frozen `v2.2.1`, 20 s, 200 pairs | 49.12%: leg not met |
| [37219857657](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37219857657) `infer-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 49.88% < 52.25%: **section 12's screen fails** |
| [38309759460](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/38309759460) `playout-screen-frozen` | `deep=3,deepplies=40,deepfrac=0.25` vs frozen `v2.2.1`, 20 s, 200 pairs, seed 9600 (12 October) | 47.88%, −1.8: leg not met |
| [38309764633](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/38309764633) `playout-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 54.12% ≥ 52.25%: leg met; **section 14's screen fails** |

## Exact next steps
1. Section 13's first proposal, a different evaluation of the finalists, has failed two
   screens (section 10's 4-ply look and section 14's play-outs to the end). Its other proposal,
   stopping once the leader has held under a game clock and keeping the time for harder moves,
   is not registered; fixed-time matches cannot measure it, so it needs a registered match
   under a clock. Section 9's result (level at 60 s with four threads, with the 20 s lead in
   points gone) is the baseline any change has to beat at that budget.
2. MAGPIE has not been measured at 60 s with four threads; a match there would need its own
   registered section (rule, seed and size fixed before any game).
3. Never relax a rule after seeing data; never claim "strongest engine" beyond what section 9
   and the other registered matches show (section 9 allows only "level within the interval").
