# Handover (state at 5 October 2026)

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

## Defaults
The engine's default search is unchanged since v2.2.1 (fixed-work checksum `349.2837`). The
live app (GitHub Pages) plays the default. Options added and tested, all off by default:
`keep` (opponent-rack prior, no effect), `egk` (endgame look-ahead in late play-outs: +1.7
points a game against Macondo, proven, but no extra wins, so not promoted), `deep` (below).

## Runs of 4 October and where they stand
All on `experiment.yml`; `report.yml` recorded each one under "Results recorded automatically"
at the end of `experiments/README.md`, and sections 10 and 12 now carry the prose.

| Run | What | Outcome |
|---|---|---|
| [37208427774](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37208427774) `tournament-60s-4t` | default (`iters=100000000`) vs Macondo `simming 5`, 60 s, 4 threads, 416 pairs, seed 9300 | **incomplete** (408 of 416: job 36 lost its runner); job 36 re-run on 5 October, see below |
| [37209468373](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209468373) `deep-screen-frozen` | `deep=3,deepplies=4,deepfrac=0.5` vs frozen `v2.2.1`, 20 s, 200 pairs | 51.38%, +5.3: leg met |
| [37209469683](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209469683) `deep-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 50.12% < 52.25%: **section 10's screen fails** |
| [37219855057](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37219855057) `infer-screen-frozen` | inference with the game's history vs frozen `v2.2.1`, 20 s, 200 pairs | 49.12%: leg not met |
| [37219857657](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37219857657) `infer-screen-macondo` | the same vs Macondo, `fresh-9200` deals | 49.88% < 52.25%: **section 12's screen fails** |

## Exact next steps
1. **The tournament (section 9).** "Re-run failed jobs" replays only job 36 (pairs 288–295)
   with the same deals and builds, about 4 hours on one runner. When the run finishes,
   `report.yml` pools all 52 jobs' logs itself and records the run again with section 9's
   verdict, since its log has changed (it no longer relies on the summary job's pooled
   artifact, whose upload may clash with the first attempt's). If it does not, record it by
   hand: `gh workflow run report.yml -f run_id=37208427774`.
2. Then write section 9's results under its "Status" paragraph in `experiments/README.md`:
   the registered verdict, plus `python3 tools/latebias.py decided LOG --by either`, `convert
   LOG` and `calib LOG` (descriptive, registered in section 11), and update the summary
   paragraph near the top of `README.md`.
3. Section 13's proposals (a different use of the time after the middle-game search settles)
   are not registered. Register one before running any screen; nothing becomes the default
   from a screen.
4. Never relax a rule after seeing data; never claim "strongest engine" beyond what section 9
   and the other registered matches show.
