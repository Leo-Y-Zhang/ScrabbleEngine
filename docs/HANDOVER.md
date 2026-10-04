# Handover (state at 4 October 2026, 15:30 BST)

Read this first in a new session, then `experiments/README.md` sections 7–11.

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

## Defaults
The engine's default search is unchanged since v2.2.1 (fixed-work checksum `349.2837`). The
live app (GitHub Pages) plays the default. Options added and tested, all off by default:
`keep` (opponent-rack prior, no effect), `egk` (endgame look-ahead in late play-outs: +1.7
points a game against Macondo, proven, but no extra wins, so not promoted), `deep` (below).

## Runs started on 4 October, results pending
All on `experiment.yml`; each run's page shows the pooled analysis in its summary, and the
pooled log is the run's artifact (named after the run; GitHub keeps artifacts 90 days).

| Run | What | Rule (fixed before any game) |
|---|---|---|
| [37208427774](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37208427774) `tournament-60s-4t` | default (`iters=100000000`) vs Macondo `simming 5`, 60 s, 4 threads, 416 pairs, seed 9300 | section 9: "ahead"/"behind" only if the 95% score interval is entirely above/below 50%, else "level" |
| [37209468373](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209468373) `deep-screen-frozen` | `deep=3,deepplies=4,deepfrac=0.5` vs frozen `v2.2.1`, 20 s, 200 pairs, seed 9400 | section 10: passes if score > 50% and spread > 0 here ... |
| [37209469683](https://github.com/Leo-Y-Zhang/ScrabbleEngine/actions/runs/37209469683) `deep-screen-macondo` | the same candidate vs Macondo, 20 s, 200 pairs, seed 9200 (the `fresh-9200` deals) | ... and its score here is not below the default's 52.25% on the same deals |

## Exact next steps
0. This file and sections 9–10 live on branch `tournament-match` (PR #22). Its CI was queued
   behind the experiment runs, which take all 20 of the account's job slots for about 12 hours;
   merge PR #22 once its checks are green (the experiment runs build from fixed commits, so
   merging does not affect them). The option values are clamped to sensible ranges since
   `tournament-match`'s last commit (a review finding; the registered settings are inside them).
1. Download the artifacts (`gh run download RUN -n NAME`), gzip them into `experiments/`,
   and run `python3 tools/analyze.py LOG --bootstrap 10000`. For the Macondo screen, also
   pair it by deal with `experiments/fresh-9200.jsonl.gz` (same seed and pair numbers).
   For the tournament, also report `python3 tools/latebias.py decided LOG --by either`,
   `convert LOG` and `calib LOG` (descriptive, registered in section 11 before its results;
   section 9's rule is unchanged).
2. Append the results under sections 9 and 10 of `experiments/README.md` with the decision
   each rule gives, and update the summary paragraph near the top of `README.md`.
3. If section 10's screen passes: register a confirmation (fresh seeds, about 400 pairs at
   20 s, plus 60 s with 4 threads) before running it. Nothing becomes the default from a
   screen. If it fails, record it and keep the default.
4. Never relax a rule after seeing data; never claim "strongest engine" beyond what section 9
   and the other registered matches show.
