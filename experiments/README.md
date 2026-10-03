# Experiments

Every engine change is screened against the frozen Tilefish 2.1 (tag `v2.1`) before it is
kept. Failed experiments stay here with the successful ones. Each entry gives the
hypothesis, the setup, the result and the decision. The raw logs sit next to this file:
one JSON line per game, after a header line per job that names both builds. Recompute any
number with `python3 tools/analyze.py FILE.jsonl --bootstrap 10000`.

Matches run with `.github/workflows/experiment.yml` on GitHub's 4-vCPU runners. Both
engines of a job share one machine, so each deal pair is matched. Development screens use
deal seeds 1000–8999. Seeds from 9000 up are kept for confirmation matches, so no
confirmation deal is ever used for tuning.

## 1. Deeper simulation (3 or 4 plies instead of 2), 3 October 2026

**Hypothesis.** Tilefish's lead shrinks as thinking time grows: about 57% at 1–10 s a move
against MAGPIE, and 54–55% at 20 s. Macondo (BestBot's settings) simulates 5 plies, while
Tilefish simulates 2. Deeper rollouts cost samples but see further, so they may pay off as
the budget grows.

**The setting acts.** On three midgame positions searched for 2 s each, the top candidate
received 22,322 rollouts at 2 plies, 10,502 at 3 plies and 5,992 at 4 plies (browser
build, one thread).

**Setup.** Engine A is `main` at `5555811` with `champion:plies=N`. Engine B is `v2.1` at
`704e9cb` with `champion` (2 plies). Both used ENABLE with the trained leaves, one thread
each, and 10 jobs of 20 deal pairs. Seed 1001 was used at 5 s a move and seed 1002 at 20 s.

| Candidate | Budget | Pairs | A: W–D–L | Score | Elo (Bonferroni-corrected interval: 97.5% for the two 5 s screens, 98.3% over all three) | Spread a game |
|---|---|---|---|---|---|---|
| 3 plies | 5 s | 200 | 201–2–197 | 50.50% | +3 (−31 to +38) | +2.8 (−5.3 to +10.8) |
| 4 plies | 5 s | 200 | 202–2–196 | 50.75% | +5 (−29 to +39) | +1.7 (−6.7 to +10.1) |
| 4 plies | 20 s | 200 | 188–0–212 | 47.00% | −21 (−54 to +12) | −7.5 (−14.3 to −0.8 uncorrected) |

Both engines used 4.45 s of the 5 s a move, and 17.7 s of the 20 s. There were no illegal moves, crashes or slow
moves. Games averaged 24.7 turns, and the SD of pair scores was 0.311. Raw logs:
`plies3-vs-v2.1-5s.jsonl`, `plies4-vs-v2.1-5s.jsonl`, `plies4-vs-v2.1-20s.jsonl`.

**Decision: rejected.** At 5 s a move, deeper rollouts make no measurable difference.
At 20 s, where the hypothesis predicted they would help most, 4 plies scored *lower*: 47%
and 7.5 points a game behind. The spread loss is outside chance before the correction for
three screens, and the win-rate loss is not. Fewer samples at more depth is a bad trade for
Tilefish's rollout policy, so depth does not explain why its lead over other engines shrinks
with time. The default stays at 2 plies. Next in line: the static prior's weight (`tau`) and
the number of candidates (`cands`) at 20 s, then fresh, versioned matches against MAGPIE
and Macondo on CSW24.

**Limits.** These matches used ENABLE, not CSW24, because the referee needed a plain word
list. `savewords` now exports one from any `.kwg`, and `experiment.yml -f lexicon=CSW24` uses
it. The opponent is Tilefish's own baseline, not
MAGPIE or Macondo, and each engine had one thread.

## How long a confirmation match must be

The 5 s screens give a pair-score SD of 0.311. With two-sided α = 0.05 and power 0.8, a
true edge of +30 Elo needs **409 deal pairs**, and +20 Elo needs **916**
(`tools/analyze.py LOG --plan 20 30`). A screened candidate's result must also be corrected
for the number of candidates tried (`--candidates K`).

At 5 s a move a game took 1.9 minutes. At 60 s a move with 4 threads per engine, a game
takes about 25 minutes and fills one 4-vCPU runner, so a 6-hour job holds 6 pairs. 409
pairs would then take 69 jobs, about 345 runner-hours, or roughly 18 hours with 20 jobs
running at once. Only a candidate that passed a screen should get that match:

```sh
gh workflow run experiment.yml -f name=confirm-60s -f a_ref=CANDIDATE -f a_spec=champion \
  -f b_ref=v2.1 -f b_spec=champion -f movetime=60000 -f threads=4 -f shards=69 -f pairs=6 -f seed=9001
```

It can be resumed: each job owns its own range of deal pairs, so "Re-run failed jobs"
replays only the missing ones, with the same deals.

## 2. Weight of the static prior, and candidate selection (registered 3 October 2026, before any game)

**How the ranking works (read from the code, §11 `rank`).** Each candidate's simulated
results are compared, rollout by rollout, with the most-sampled candidate's. The mean
paired difference is then combined with a prior centred on the static-equity difference,
whose variance is 2(scale × tau)². The combination is the normal-normal posterior mean,
(m/se² + prior/var) / (1/se² + 1/var). Because se² shrinks as 1/n, the prior's weight
falls as the rollouts accumulate, so it cannot dominate a long search. The static value is
used once, as the prior; the rollouts end in static evaluations of the *resulting*
positions, which is a different quantity, so nothing is counted twice. What is open is
whether tau = 4 (static equity "right to within about 4 points") is the right strength,
and whether 30 candidates pruned at z = 2.4 spend the time well.

**Hypotheses** (each a single change to `champion`, all existing options):

| Candidate | Change | Hypothesis |
|---|---|---|
| `tau=2` | stronger prior | at 5 s, rollouts are still noisy, so trusting the static ranking more helps |
| `tau=8` | weaker prior | the static evaluation misjudges more than 4 points, so the rollouts should count sooner |
| `tau=16` | much weaker prior | as above, more so |
| `cands=15` | half the candidates | moves ranked 16 to 30 by static equity rarely win, and their samples are better spent on the top 15 |
| `z=1.8` | prune sooner | clearly worse moves currently use too much time |
| `z=3.2` | prune later | moves that start badly are dropped before they can recover |

**Protocol.** CSW24 (kwg and klv2 from MAGPIE-DATA at the pinned commit, SHA-256
checked by `get-lexicon.sh`; win model `CSW24.win` from this repository). 5 s a move, one
thread each, engine B is frozen `v2.1` with `champion`. 200 deal pairs per candidate,
10 jobs of 20, seed 1003 for every candidate (the same deals). Selection: the candidate
with the highest score, if its Bonferroni-corrected interval (6 candidates, 99.2%)
excludes 50% on the positive side, or failing that the highest score above 51.5%,
which goes forward only as a lead. **Confirmation of the selected candidate:** fresh
seed 2001, 300 deal pairs at 5 s and 100 pairs at 20 s, both against v2.1. It is
promoted to the default only if the 5 s confirmation's 95% interval lies above 50%
and the 20 s result's point estimate is not below 50%.

**Results** (run 3 October 2026 on `main` at `5555811`, the same search as 2.2; engine B is
`v2.1`; CSW24, 5 s a move, one thread each, 200 deal pairs, seed 1003; intervals are
99.2%, Bonferroni over the six candidates):

| Candidate | A: W–D–L | Score | Elo (99.2%) | Spread a game (99.2%) |
|---|---|---|---|---|
| `tau=2` | 190–2–208 | 47.75% | −16 (−46 to +14) | −4.6 (−12.1 to +2.9) |
| `tau=8` | 202–5–193 | 51.12% | +8 (−22 to +38) | +0.5 (−6.8 to +7.8) |
| `tau=16` | 203–1–196 | 50.88% | +6 (−21 to +33) | −1.0 (−7.9 to +5.9) |
| `cands=15` | 193–0–207 | 48.25% | −12 (−41 to +16) | +2.1 (−5.1 to +9.4) |
| `z=1.8` | 200–1–199 | 50.12% | +1 (−29 to +30) | +1.0 (−6.4 to +8.4) |
| `z=3.2` | 205–2–193 | 51.50% | +10 (−14 to +35) | +1.3 (−5.0 to +7.7) |

Each engine used about 4.5 s of the 5 s a move, with no illegal moves, crashes or slow
moves. Raw logs: `csw-*-vs-v2.1-5s.jsonl`.

**Decision: nothing promoted; the defaults stay.** No interval excludes 50%. The
registered rule sends forward the best candidate scoring *above* 51.5%. The best,
`z=3.2`, scored exactly 51.50%, so under the rule as written none goes to confirmation.
The rule is not relaxed after the fact. The pattern fits small or zero effects:
stronger trust in the static prior (`tau=2`) and fewer candidates (`cands=15`) lean
negative, while weaker trust (`tau=8`, `16`) and later pruning (`z=3.2`) lean positive by
less than 10 Elo. Detecting 10 Elo needs roughly 3,700 deal pairs at this variance, so the
screens rule out large effects (beyond about ±35 Elo) and nothing more. If they are
revisited, the natural single candidate is `tau=8,z=3.2`, registered as a new
hypothesis and run on fresh seeds.
## 3. Fresh, versioned baselines against current MAGPIE and Macondo (registered 3 October 2026, before any game)

The published matches against MAGPIE and Macondo did not record which versions were
played. These matches measure Tilefish 2.2 (`main`, the same search as 2.1) against
**pinned current builds**, made by `experiment.yml` itself:

- MAGPIE `375ad953e20d` (2026-10-01), built with `make magpie BUILD=no_pgo_release`, its own
  data (`download_data.sh`, DATA_VERSION 20260925), a CSW24 word map made with its
  `convert text2wordmap`, and its full PlayChooser (`full`: simulation, pre-endgame,
  endgame).
- Macondo `14c080b57608` (2026-09-22), BestBot's bot type at 5 plies (`simming 5`), and
  separately its inference bot (`infer 5`).

Protocol: CSW24, one thread each on the same 4-vCPU runner, 5 s a move, 200 deal pairs
(10 jobs of 20), fresh seed 3001 for all three. This is a measurement, not a test of a
change. It is reported with 95% intervals and Bonferroni over the three opponents
(98.3%). Tilefish's leave values are the CSW24 `.klv2` from `get-lexicon`, MAGPIE uses
its own data, and Macondo uses its own strategy files with the same `.klv2`.
