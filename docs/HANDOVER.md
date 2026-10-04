# Handover (state at 4 October 2026, 15:30 BST)

Read this first in a new session, then `experiments/README.md` sections 7–10.

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
1. Download the artifacts (`gh run download RUN -n NAME`), gzip them into `experiments/`,
   and run `python3 tools/analyze.py LOG --bootstrap 10000`. For the Macondo screen, also
   pair it by deal with `experiments/fresh-9200.jsonl.gz` (same seed and pair numbers).
2. Append the results under sections 9 and 10 of `experiments/README.md` with the decision
   each rule gives, and update the summary paragraph near the top of `README.md`.
3. If section 10's screen passes: register a confirmation (fresh seeds, about 400 pairs at
   20 s, plus 60 s with 4 threads) before running it. Nothing becomes the default from a
   screen. If it fails, record it and keep the default.
4. Never relax a rule after seeing data; never claim "strongest engine" beyond what section 9
   and the other registered matches show.
