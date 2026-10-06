# Handover (state at 6 October 2026)

Read this first in a new session, then `experiments/README.md` sections 9–13.

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
