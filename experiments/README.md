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

**Added before any 20 s game (same day):** the same measurement at **20 s a move**
against MAGPIE (`full`) and Macondo with BestBot's settings (`simming 5`): 200 deal
pairs each, one thread, CSW24, fresh seed 3002. These are the conditions of the
published 55.0% against BestBot's settings, now with recorded versions.

**Results at 5 s a move** (CSW24, one thread each, 200 deal pairs, seed 3001; Tilefish
2.2 is engine A; intervals are 98.3%, Bonferroni over the three opponents):

| Opponent (pinned build) | A: W–D–L | Score | Elo (98.3%) | Spread a game (98.3%) |
|---|---|---|---|---|
| MAGPIE `375ad953e20d`, `full` | 223–0–177 | 55.75% | +40 (+4 to +78) | +15.5 (+4.3 to +26.7) |
| Macondo `14c080b57608`, `simming 5` (BestBot's settings) | 239–2–159 | 60.00% | +70 (+31 to +112) | +42.3 (+30.2 to +54.5) |
| Macondo `14c080b57608`, `infer 5` | 251–1–148 | 62.88% | +92 (+53 to +132) | +48.9 (+37.1 to +60.7) |

There were no illegal moves, crashes or forfeits. The engines used about 4.5 s a move on
average; Macondo's longest move took 7.5 s of a 5 s budget. Raw logs:
`base-v2.2-vs-*-5s.jsonl`, each opening with the builds' provenance.

**What this shows.** At 5 s a move on one core, Tilefish 2.2 is ahead of all three pinned
opponents, and every interval excludes zero. **What it does not show:** anything about
longer thinking. Macondo simulates 5 plies deep, so in 5 s it gets few samples per
candidate, and in production BestBot thinks for up to three minutes on three or four
cores. The 20 s matches below are the closer test.

**Results at 20 s a move** (CSW24, one thread each, 200 deal pairs, seed 3002; 98.3%
intervals as above):

| Opponent (pinned build) | A: W–D–L | Score | Elo (98.3%) | Spread a game (98.3%) |
|---|---|---|---|---|
| MAGPIE `375ad953e20d`, `full` | 237–3–160 | 59.62% | +68 (+29 to +108) | +20.9 (+9.8 to +32.0) |
| Macondo `14c080b57608`, `simming 5` (BestBot's settings) | 198–3–199 | 49.88% | −1 (−39 to +38) | +15.4 (+3.8 to +27.0) |

The engines used about 17–18 s of the 20 s a move, with no illegal moves, crashes or
forfeits. Raw logs: `base-v2.2-vs-*-20s.jsonl`.

**What the four matches show together.** Against current MAGPIE, Tilefish 2.2 is ahead at
both 5 s and 20 s a move on one core, and its lead does not shrink with time (55.75% →
59.62%). Against current Macondo with BestBot's settings, it is clearly ahead at 5 s
(60.00%) but **level on wins at 20 s** (49.88%, interval −39 to +38 Elo), while still
winning more points a game. **The earlier 55.0% against BestBot's settings at 20 s does
not reproduce against today's pinned Macondo.** No claim that Tilefish is the strongest
engine follows from this. At tournament-length budgets, with four cores (BestBot's
production setting), the comparison is open, and the deciding match would be the
60–180 s, four-core profile planned above.

## 4. Why level on wins but ahead on points against Macondo at 20 s? (registered 4 October 2026, before any game)

**What the existing logs can and cannot show.** The logs of section 3 record each game's
final score, time and errors, **but not its moves**, so no position from those games can
be reconstructed. What they do show (`python3 tools/margins.py
experiments/base-v2.2-vs-macondo-simming-20s.jsonl`):

| 20 s against Macondo `simming 5` (400 games) | Value |
|---|---|
| Tilefish's wins: count, mean margin, median | 198, +103.4, +84.0 |
| Tilefish's losses: count, mean margin, median | 199, −72.0, −59.0 |
| Games decided by 25 points or less: share, Tilefish's score in them | 19.2%, 39.6% |
| Games decided by 50 points or less: share, Tilefish's score in them (95% bootstrap interval) | 35.2%, 43.6% (36.1% to 51.6%) |
| Games decided by more than 100: share, Tilefish's score in them | 33.8%, 62.2% |

The positive average spread comes from larger wins, not more of them: Tilefish's wins are
bigger than its losses, and it loses more of the close games. Against MAGPIE at 20 s the
same table gives 58.3% in games decided by 25 or less; against Macondo at 5 s, 52.0%.
This locates the question (close games, so probably the late game, where margins are
decided) without answering it. A win-probability error is one explanation, not the only
one: weaker pre-endgame or endgame play, or worse decisions in tight positions, would look
the same.

**Configuration facts checked in the code before running** (Macondo `14c080b57608`):
- Each move, both engines get only a CGP, so neither has the game's history. Macondo's
  inference needs it (`rangefinder/inference.go`, `PrepareFinder` returns `ErrNoEvents`
  for a game without events), and so does Tilefish's (`Position::has_opp_last`). **So
  `infer 5` has played as `simming 5` in every match so far, and Tilefish has never
  inferred either.** The 62.88% against `infer 5` measured the same opponent as the
  60.00% against `simming 5`.
- Macondo's phases (`ai/bot/elite.go`): endgame with the bag empty; pre-endgame solver
  with 1 tile in the bag; with 2 to 7 in the bag, a simulation to the end of the game
  over its top 100 moves; otherwise 5 plies over its top 100 moves, stopping early at 99%
  confidence. Tilefish: endgame; pre-endgame with 1 in the bag; with 2 to 7, simulation
  played out to the end over its top 30 moves; otherwise 2 plies over its top 30.

**New instrumentation** (this branch): the referee now stores each game's full record:
the position each mover saw (CGP, opponent's rack hidden while the bag has tiles), the
move, score, time, CPU seconds used by the engine's processes, engine start-up time, and
an `info` line from each engine. Tilefish's info gives, per move: phase, legal moves
generated, candidates kept, iterations, positions, candidates pruned, the iterations
each candidate received, the top six with static rank, simulated win rate and equity,
posterior, simulated difference and **the share of the posterior precision that comes
from the static prior**, snapshots of the leading move at 1/64 … 1/2 of the search, and
the time spent generating, inferring and simulating. Macondo's info gives set-up and
search time, whether inference ran, and Macondo's own summary of its search. None of
this changes what either engine plays.

**Runs (measurements, not tests of a change).** CSW24, one thread each, Tilefish `main`
search (= 2.2) against Macondo `14c080b57608` `simming 5`:

| Run | Budget | Pairs | Seed | Purpose |
|---|---|---|---|---|
| `audit-20s` | 20 s | 200 (10 × 20) | 3002 | the section 3 match replayed with records: a replication of 49.88%, and the games to audit |
| `profile-5s` | 5 s | 40 (2 × 20) | 3101 | where computation goes at 5 s |
| `profile-60s` | 60 s | 16 (4 × 4) | 3102 | pilot: where computation goes at 60 s |

**Added before any of its games (4 October 2026): an inference-mode pilot.** The referee
can now send an engine the game so far as the mover knows it (`--b-history`: a GCG with
the mover's own racks and only the tiles the opponent played; placeholder tiles from the
mover's unseen pool for the opponent's exchanges and passes). The Macondo wrapper rebuilds
the game from it with Macondo's own `gcgio` and `NewFromHistory`, as Macondo's bot worker
(`bot/bot.go`) does, checks the board, scores and rack against the CGP, and falls back to
the CGP if they differ. In a local test (2 pairs, 1.5 s) all 50 of Macondo's moves used
the history and inference found up to 57,120 possible racks. Tilefish gets no history
here, so it plays exactly as in the runs above; only Macondo's configuration changes.

| Run | Budget | Pairs | Seed | Macondo |
|---|---|---|---|---|
| `infer-20s` | 20 s | 40 (2 × 20) | 3002 (pairs 0–39, the same deals as `audit-20s`) | `infer 5` with history |
| `infer-60s` | 60 s | 16 (4 × 4) | 3102 (the same deals as `profile-60s`) | `infer 5` with history |

This is a pilot for choosing Macondo's configuration for the final comparison, not a
test: 40 pairs measure a score to about ±9 percentage points. Rule, fixed now: the final
comparison uses `infer 5` with history if, at 20 s, Tilefish's score against it is lower
than against `simming 5` on the same 40 deal pairs (the stronger opponent is the one to
measure against), and `simming 5` otherwise.

**Added before any of its games (4 October 2026): a four-thread pilot at 60 s.** To check
the configuration and measure throughput before planning a long match: Tilefish (`main`
search, `champion`) and Macondo (`simming 5`) with **4 threads each**, 60 s a move, one game
at a time on each 4-vCPU runner (`--parallel 1`, so only the engine on move is computing),
4 jobs × 2 deal pairs, seed 3201, CSW24. Recorded: CPU seconds per move for each engine
(cores actually used), Tilefish's iterations, Macondo's own summary, game length in
minutes, and the runner's CPU topology (virtual CPUs, cores, threads per core). It is not
a test and its score is not reported as a result (8 pairs).

The audit itself (positions, decisions, reanalysis) is described with its results.

### Results of section 4 (4 October 2026)

Logs: `audit-v2.2-vs-macondo-simming-20s.jsonl.gz`, `profile-v2.2-vs-macondo-simming-5s.jsonl.gz`,
`profile-v2.2-vs-macondo-simming-60s.jsonl.gz`, `infer-v2.2-vs-macondo-infer-history-20s.jsonl.gz`,
`infer-v2.2-vs-macondo-infer-history-60s.jsonl.gz` (every game with its full record). Tilefish
is engine A, built at `d18fd01` (the 2.2 search plus telemetry); Macondo `14c080b57608`.
Every number below comes from `tools/analyze.py`, `margins.py`, `audit.py` and
`profile.py` on these files. There were no illegal moves, crashes or forfeits in any run.

**The replay reproduces the original 20 s result.**

| 20 s, `simming 5`, seed 3002, 200 pairs | Section 3 (no records) | Replay with records |
|---|---|---|
| Tilefish's score (95%) | 49.88% | 49.75% (45.1% to 54.4%), W–D–L 198–2–200 |
| Spread a game (95%) | +15.4 | +13.8 (+4.1 to +23.4) |
| Score in games decided by ≤ 50 (95%) | 43.6% (36.1% to 51.6%) | 41.0% (33.9% to 48.6%) |
| Mean margin of Tilefish's wins / losses | +103.4 / −72.0 | +101.0 / −72.4 |

**Where the points are won and lost** (`audit.py`; tiles in the bag before the move;
95% intervals by bootstrap over pairs):

| Points Tilefish gains a game | Opening 61+ | Middle 15–60 | Approach 8–14 | Late 2–7 | Last tile | Endgame | Rack adj. | Total |
|---|---|---|---|---|---|---|---|---|
| 5 s, 40 pairs | +11.9 (+3.9 to +20.4) | +21.2 (+7.0 to +36.1) | +6.7 (−4.3 to +16.9) | −0.1 (−6.9 to +7.5) | +4.2 | −0.2 | +0.7 | +44.5 |
| 20 s, 200 pairs | +2.8 (−1.0 to +6.8) | +2.3 (−4.8 to +9.7) | +6.9 (+2.4 to +11.6) | +1.1 (−2.7 to +4.6) | +0.0 | −0.5 | +1.1 | +13.8 |
| 60 s, 16 pairs | −6.9 | +14.7 | −2.1 | +4.9 | +1.5 | +9.4 | +3.4 | +24.9 |

Between 5 s and 20 s, Tilefish's gain in the opening and middle game falls from +33.1 to
+5.1 points a game; the later phases hardly change. (The 5 s figures come from 40 pairs
on other deals, so the comparison is indicative.)

**Converting the late game** (spread when the bag first holds 7 or fewer, 20 s):

| Lead at that point | Tilefish ahead: games, Tilefish wins | Macondo ahead: games, Macondo wins |
|---|---|---|
| 1–40 | 67, 58.2% | 63, 65.1% |
| 41–100 | 65, **83.1%** | 81, **93.8%** |
| 101+ | 78, 98.7% | 46, 97.8% |

For 41–100 the difference is 10.7 points (1.1 to 20.7; not corrected for the several
cuts examined). **In all 11 games that Tilefish lost from a 41–100 lead, Macondo made a
bingo after the bag reached 7 tiles** (for example `.ANGRELS` +83, `.tECHNIC` +104,
`BUNcHED` +105, `sIR.AMED` +140); in 9 of the 11 it had first made a smaller play in
that phase. Where Tilefish had simulated its last move before the bingo (8 games), its
estimate of its winning chance was 0.22 to 1.00, and 0.82 or more in four of them.

**Tilefish's estimated winning chances** (its chosen move's simulated win rate against
the game's result, 20 s): in the 2-ply phase, estimates between 0.1 and 0.9 are 2.3 points
too high on average (−2.2 to +6.9; 2,982 moves), so they are calibrated. **In the late
phase (2–7 in the bag) they are 14.7 points too high (+7.0 to +22.6; 112 moves).** The
late rollouts play both sides greedily to the end of the game. Greedy play never fishes
or sets up a bingo, so the estimates miss exactly how Macondo came back.

**Where the computation goes** (Tilefish, one thread; `profile.py`):

| | 5 s | 20 s | 60 s (pilot) |
|---|---|---|---|
| Moves profiled (2-ply phase / late playout phase) | 769 / 64 | 3,749 / 325 | 307 / 26 |
| Legal moves generated, median; candidates kept | 689; 30 | 673; 30 | 577; 30 |
| Iterations of the most-simulated candidate, median | 13,871 | 55,166 | 196,542 |
| Candidates pruned by the end, mean | 28.0 of 30 | 28.0 of 30 | 28.0 of 30 |
| Share of iterations to the top two candidates | 87.8% | 96.3% | 98.7% |
| Leader at 1/16 of the search ≠ final choice | 12.9% | 6.4% | 5.2% |
| Leader at 1/4 of the search ≠ final choice | 3.5% | 2.3% | 2.3% |
| Leader at 1/2 of the search ≠ final choice | 3.8% | 1.3% | 1.0% |
| Prior's share of the posterior precision, competitive candidates (posterior within 0.02): median, p90, share > 0.5 | 0.027, 0.148, 1.4% | 0.030, 0.180, 2.0% | 0.038, 0.223, 5.2% |
| Same, late playout phase: median, p90, share > 0.5 | 0.019, 0.227, 8.4% | 0.021, 0.876, 18.3% | 0.023, 0.886, 20.5% |
| Chosen move's static rank #1 (2-ply / late) | 66.7% / 35.9% | 65.2% / 24.6% | 62.5% / 19.2% |
| Chosen move from static ranks 20–29 (2-ply / late) | 0.4% / 9.3% | 0.7% / 14.8% | not computed |
| Wall time a move (simulated moves); CPU / wall | 5.00 s; 1.00 | 20.01 s; 1.00 | 60.03 s; 1.00 |
| Time generating / inferring / simulating | 0.001 / 0 / 5.00 s | 0.001 / 0 / 20.01 s | 0.001 / 0 / 60.03 s |
| Endgames solved exactly | 82.2% | 89.9% | 93.3% |
| Pre-endgames (1 in the bag): candidates evaluated, of them exactly | 16, 0 | 16, 0 | 16, 0 |

What this shows: once its 2-ply search has settled, which by 1/8 of a 20 s move it has in
96% of cases, nearly all further work refines the duel between the final two candidates.
That rarely changes the move. The prior is a small part of the posterior for
competitive candidates in the 2-ply phase. In the late phase it dominates for about one
competitive candidate in five, because those candidates are pruned after about 100–150
playouts. Its asymptotic decay does not make it negligible there.

**The opponent's resources and configuration.**

| Macondo `simming 5`, one thread | 5 s | 20 s | 60 s |
|---|---|---|---|
| Wall time a move; CPU / wall | 4.59 s; 0.99 | 17.32 s; 1.00 | 43.16 s; 1.00 |
| Iterations a move (its own summary), bag 15+: median (p10–p90) | not computed | 193 (117–471) | 1,030 (388–5,248) |
| Moves stopped early by its 99% rule, bag 15+ | not computed | 7% (stopped before 18 s) | 43% (before 55 s) |
| Set-up a move (parsing the position, building the bot) | 0.001 s | 0.001 s | 0.001 s |

Engine start-up (process start to `readyok`) is outside the move clocks: Tilefish 1.3–1.4
s; Macondo answers at once and loads its data during its first move, once per worker
process (one move in 40 games). Both engines used one core each throughout (CPU/wall 0.99–1.00).
The runner's 4 virtual CPUs ran 4 games at once, so each engine's thread shared the
machine with three other games' threads.

**Inference pilot** (Macondo `infer 5` with the game's history; Tilefish unchanged).
Macondo rebuilt the game from history on every move (942 at 20 s, 373 at 60 s). Its
inference produced possible racks on 588 of 830 moves with tiles in the bag at 20 s
(median 1,774 racks) and 236 of 329 at 60 s.

| Same deal pairs | Tilefish vs `simming 5` | Tilefish vs `infer 5` + history | Difference (95%) |
|---|---|---|---|
| 20 s, pairs 0–39 of seed 3002 | 45.62% | 52.50% | +6.9 (−8.8 to +22.5) |
| 60 s, pairs 0–15 of seed 3102 | 53.12% | 50.00% | −3.1 (16 pairs; not computed) |

Under the rule fixed beforehand, Tilefish did not score lower against `infer 5` at 20 s,
so **the final comparison uses `simming 5`**. Inference spends a quarter of each move
(`InferenceTimeSecs` = move time / 4 in the wrapper), which is one reason it may not
help at these budgets; the pilot cannot separate that from noise.

## 5. More candidates near the end of the game (registered 4 October 2026, before any game)

**What the audit found first** (section 4's runs; full tables with the results there).
The replayed 20 s match reproduces the original: 49.75% (45.1% to 54.4%), +13.8 points a
game (+4.1 to +23.4). Tilefish scores 41.0% (33.9% to 48.6%) in games decided by 50 or
less. Two things stand out:

- *Where the 5 s lead goes.* Points Tilefish gains in the opening and middle game (bag 15
  or more): +33.1 a game at 5 s (40 pairs), +5.1 at 20 s (200 pairs). Its search barely
  changes its mind with more time: at 20 s the leader after 1/8 of the search is the final
  choice 96.2% of the time, and the top two candidates take 96.3% of all iterations.
  Macondo, at a median 193 iterations of 5 plies a move, is far from that point.
- *The end of the game.* When the bag first holds 7 or fewer, Tilefish 41 to 100 points
  ahead wins 83.1% (65 games); Macondo as far ahead wins 93.8% (81 games); difference
  10.7 points (1.1 to 20.7, bootstrap over pairs, not corrected for the several cuts
  examined). In that phase (2 to 7 in the bag) Tilefish's chosen move is outside its top
  5 by static equity 47.4% of the time, and **14.8% of its choices come from static ranks
  20–29, with more in 25–29 (8.0%) than in 20–24 (6.8%)**: the density is still rising at
  the cut of 30, so good moves are very likely being cut. In the 2-ply phase ranks 20–29
  hold 0.7% of choices. Macondo keeps 100 candidates in this phase.

The opening/middle-game time scaling is the larger effect in points, but its remedy is
not obvious (deeper rollouts lost earlier in section 1). The late-game cut is the
weakness with the most specific evidence and the most specific remedy, so it is tested
first; the time scaling is the next hypothesis.

**Hypothesis.** With 2 to 7 tiles in the bag, keeping 100 candidates instead of 30
(`champion:latecands=100`; everything else unchanged, including the 1-tile pre-endgame
and the endgame) finds better late-game moves.

**Design.** To measure late-game play on its own, both games of a deal pair are played
by a deterministic static Tilefish until the bag holds 7 tiles or fewer
(`prefix_until_bag=7`); the two engines then take over from the same position with
seats swapped. Primary measure: points engine A gains on B from the handover to the end
(per pair, bootstrap over pairs). Secondary: A's score (wins) in those games. CSW24, one
thread each, 20 s a move unless stated.

| Run | A | B | Pairs | Seed | Role |
|---|---|---|---|---|---|
| `late-screen` | `latecands=100` | frozen `v2.2.1` `champion` | 200 | 4001 | screen against frozen Tilefish |
| `late-vs-macondo` | `latecands=100` | Macondo `simming 5` | 200 | 4002 | screen against Macondo |
| `late-vs-macondo-control` | `champion` (default) | Macondo `simming 5` | 200 | 4002 | the same positions with the default |
| `late-confirm` | `latecands=100` | frozen `v2.2.1` | 300 | 9001 | confirmation, fresh seeds |
| `late-confirm-60s` | `latecands=100` vs Macondo, and `champion` vs Macondo | | 100 each | 9002 | longer budget, 60 s |

**Rules, fixed now.** Seeds 4001–4002 are tuning seeds; 9001–9002 are used once, for
confirmation. Every run is played to its full size (no interim looks, no early stop) and
reported whatever the outcome.
- *Screen passes* if in `late-screen` the 95% interval for points gained after the
  handover lies above 0, **or** the point estimate is positive and A scores at least 50%
  after the handover; **and** in the Macondo pair of runs the candidate gains at least as
  many points after the handover as the control (point estimates).
- *Confirmation* is run only if the screen passes. **Promotion to the default** requires
  `late-confirm`'s 95% interval for points after the handover to lie above 0 with A's
  score at least 50%, and the 60 s candidate not to gain fewer points than the 60 s control
  (point estimates). Otherwise the default stays at 30 candidates and the result is
  recorded as failed or inconclusive, with its numbers.

### Screen results (4 October 2026)

Engine A built at `aab79c8`; handover when the bag first holds 7 or fewer (mean 4.9–5.1
tiles); CSW24, 20 s a move, one thread each, 200 pairs a run; `tools/handover.py`.
No illegal moves, crashes or forfeits.

| Run | Points A gains after the handover (95%) | A's score (95%) |
|---|---|---|
| `late-screen`: `latecands=100` vs frozen `v2.2.1` (seed 4001) | +0.93 (−0.37 to +2.26) | 50.50% (50.00% to 51.25%) |
| `late-vs-macondo`: `latecands=100` vs Macondo `simming 5` (seed 4002) | +4.46 (+2.51 to +6.46) | 51.38% (50.12% to 52.75%) |
| `late-vs-macondo-control`: default vs Macondo, same 200 positions | +3.37 (+0.91 to +5.75) | 49.12% (47.38% to 50.75%) |
| Candidate − control, paired by deal (all 200 handovers identical) | +1.09 (−1.46 to +3.67) | +2.25 points (+0.50 to +4.00) |

The change did what it was meant to do mechanically: in `late-screen`, 51 of Tilefish's
304 late-phase choices (16.8%) were moves ranked 31st to 100th by static equity, which
the default can never play. **The screen passes under the registered rule** (positive
point estimate with a score of at least 50% against frozen Tilefish, and at least the
control's points against Macondo). On its own, no interval for points gained excludes
zero, so this is a lead for the confirmation, not a result.

Logs: `late-screen-latecands100-vs-v2.2.1-20s.jsonl.gz`, `late-latecands100-vs-macondo-20s.jsonl.gz`,
`late-control-vs-macondo-20s.jsonl.gz`.

### Confirmation result, 20 s (4 October 2026): fails the promotion rule

Fresh seed 9001, used once; 300 pairs; `latecands=100` (built at `aab79c8`) against frozen
`v2.2.1`; handover at a mean of 5.0 tiles; no illegal moves, crashes or forfeits. Log:
`late-confirm-latecands100-vs-v2.2.1-20s.jsonl.gz`.

| | Points A gains after the handover (95%) | A's score (95%) | Late choices from static ranks 31–100 |
|---|---|---|---|
| `late-screen` (seed 4001, 200 pairs) | +0.93 (−0.37 to +2.26) | 50.50% (50.00% to 51.25%) | 51 of 304 (16.8%) |
| `late-confirm` (seed 9001, 300 pairs) | **−0.52 (−1.86 to +0.75)** | 50.75% (49.83% to 51.75%) | 63 of 468 (13.5%) |

**Decision: not promoted. The default stays at 30 candidates.** Promotion required the
confirmation's interval for points after the handover to lie above zero; it contains
zero and the point estimate is negative.

**What the experiment established.** The change works as intended: one late-game
decision in seven picks a move the default can never play. Against Tilefish's own
late-game play this is worth nothing measurable: screen and confirmation together put it
within about ±1.5 points a game from the handover. The one positive signal was against
Macondo: +2.25 percentage points of score on the same 200 positions (+0.50 to +4.00),
and +1.09 points (−1.46 to +3.67). That is consistent with the reanalysis below, but it
was a screening measurement, and the promotion test was fixed in advance. It is
reported as a lead only (the 60 s runs against Macondo, also registered above, are
reported below).

### Reanalysis of critical decisions (4 October 2026)

`tools/reanalyse.py` and `reanalyse.yml` on the replayed 20 s match: 20 games, 5 from
each stratum (Tilefish lost by 1–50, won by 1–50, lost by 51+, won by 51+; sample seed
1), every decision of both engines, 485 decisions. Each judge saw only the position the
mover saw (the CGP in the record: the opponent's rack hidden while the bag has tiles)
and searched 40 s with 4 threads, 8 times the compute of a 20 s one-thread move. Logs:
`judge-tilefish-4t-40s.jsonl.gz`, `judge-macondo-4t-40s.jsonl.gz`.

A stronger search is not an oracle, and each judge shares one engine's evaluation. The
Tilefish judge agrees with Tilefish's own moves 96.3% of the time (all but endgame) and
with Macondo's 55.8%. That mostly shows that more time rarely changes Tilefish's mind, as
the profile above found; it says little about who is right. The Macondo judge is the
informative one for Tilefish's decisions:

| Macondo's judge agrees with… | Opening 61+ | Middle 15–60 | Approach 8–14 | Late 2–7 | Last tile | Endgame | All but endgame |
|---|---|---|---|---|---|---|---|
| Tilefish's 20 s moves | 77.9% (68) | 75.9% (108) | 63.2% (19) | **38.9% (18)** | 33.3% (3) | 81.5% (27) | 71.8% (216) |
| Macondo's own 20 s moves | 69.1% (68) | 60.6% (109) | 66.7% (15) | **76.5% (17)** | 87.5% (8) | 72.0% (25) | 65.9% (217) |

By Macondo's own longer search, Tilefish's opening and middle-game moves are closer to
right than Macondo's 20 s moves. In the late phase the judge rejected 11 of Tilefish's 18
decisions:
- **In 8 of the 11 it preferred a move that Tilefish ranks 37th to 66th by static
  equity** (ranks 37, 44, 53, 55, 56, 61, 64, 66). Those are outside the cut of 30, so
  Tilefish could not play them.
- The other 3 are the same placement with the blank named differently, or moves the
  judge rates equal.
- By the judge's own estimates most disagreements are small (0.5 to 4 points of winning
  chance). One is large: pair 71, bag 3, `15H (D)EI` 49.0% against the played `M12 HE`
  28.9%, static rank 53 against 7.

This is the clearest evidence for the late-game cut, and it is what `latecands=100`
tested. Against Tilefish itself the wider list did not pay; against Macondo the signal
is positive but unconfirmed.

### Four-thread, 60 s pilot: configuration and throughput (4 October 2026)

Engine A at `dd3dd86` (the 2.2 search), Macondo `simming 5`, both with 4 threads, one game
at a time per runner, 8 pairs (seed 3201). Log: `pilot-4t-v2.2-vs-macondo-simming-60s.jsonl.gz`.
The score (50.00%, 31.5% to 68.5%, 8 pairs) is not a result.

| Measured | Value |
|---|---|
| Runner CPU (`lscpu`) | 4 virtual CPUs = 2 physical cores × 2 hardware threads (AMD EPYC 7763 or 9V74) |
| Cores used while thinking (CPU s / wall s) | Tilefish 3.98, Macondo 3.97 |
| Tilefish time a move: all moves / simulated moves | 50.0 s / 58.6 s (median 60.05 s) |
| Tilefish iterations, most-simulated candidate: median, p90 | 763,611, **1,000,000 (its per-candidate cap in `champion`)** |
| Tilefish share of iterations to the top two candidates | 99.7% |
| Macondo time a move | **22.7 s of 60 s** |
| Macondo iterations a move, bag 15+: median, p90 | 5,250, 5,252 |
| Game length (both engines thinking) | mean 14.8 min (10.8 to 18.7) |

Two configuration facts follow:

- **Macondo with BestBot's settings stops by itself.** Its 99% stopping rule also ends
  every simulation after 2,000 + 625 × plies = 5,125 iterations
  (`montecarlo/stopping_condition.go`, lines 29–30 and 113). With 4 threads it gets
  there in about 20 s in the middle game. **At 60 s or more on 4 threads, these settings
  are at full strength already;** a longer budget changes only Tilefish.
- **Tilefish hits its own cap.** `champion` stops at 1,000,000 iterations per candidate,
  which binds on at least 10% of moves at 60 s with 4 threads. Longer budgets need a
  higher `iters`, though the profile suggests the extra iterations would mostly refine
  the final duel.

**Plan for a larger four-thread match against Macondo**, from these measurements
(pair-score SD 0.311 from section 1, assumed to carry over): one game takes about 15
minutes of a runner's time, so a 340-minute job holds 11 pairs.

| Target (80% power, two-sided 5%) | Pairs | Games | Runner-hours | vCPU-hours | Jobs of 11 pairs | Elapsed at 20 runners |
|---|---|---|---|---|---|---|
| +30 Elo | 409 | 818 | 202 | 808 | 38 | about 10 h |
| +20 Elo | 916 | 1,832 | 452 | 1,808 | 84 | about 23 h |

A runner-hour here is one 4-vCPU machine (2 physical cores) for an hour. The match
should use `simming 5` (inference pilot, above), `iters` raised for Tilefish, and fresh
seeds from 9100.

## 6. More late candidates against Macondo specifically (registered 4 October 2026, before any game, and before the 60 s results of section 5 were seen)

**Why a new test.** Section 5's promotion test (against frozen Tilefish) failed and stays
failed. The evidence that remains points at Macondo specifically: on the same 200
positions, `latecands=100` scored +2.25 percentage points more than the default against
Macondo (+0.50 to +4.00, a screen); and Macondo's judge preferred a move outside
Tilefish's top 30 in 8 of the 11 late decisions it rejected. Against an opponent that plays the late
game like Tilefish's own greedy rollouts, the extra candidates change little. Against
one that fishes and sets up bingos, the moves that are good in reply (often blocks, which
score little) may be exactly the ones ranked low by static equity.

**Hypothesis.** Against Macondo `simming 5`, Tilefish with `latecands=100` wins more
often from the same late-game positions than Tilefish with the default.

**Design.** As in section 5 (static prefix until the bag holds 7 or fewer, 20 s a move,
one thread, CSW24, engine A built at `aab79c8`), with **fresh seed 9003, 300 pairs**, two
runs on identical positions: `late-macondo-confirm` (A = `latecands=100`) and
`late-macondo-confirm-control` (A = `champion`). Primary measure: the difference in A's
score, candidate − control, paired by deal pair (bootstrap over pairs, 10,000
resamples). Secondary: the same for points gained after the handover.

**Rule, fixed now.** Played to full size, no interim looks. `latecands=100` becomes the
default only if the primary measure's 95% interval lies above zero **and** the
secondary's point estimate is not negative. Otherwise the default stays and the result
is reported with its numbers. A pass would support only this claim: better late-game
results against this Macondo configuration, at 20 s.

### Results of sections 5 (60 s) and 6 (4 October 2026): no effect; the default stays

Engine A built at `aab79c8`; Macondo `simming 5`; handover when the bag first holds 7 or
fewer; one thread each; no illegal moves, crashes or forfeits. Candidate and control
played identical handover positions in every pair (checked by `tools/handover.py`); the
candidate used 100 candidates on its late moves (454 in section 6) and the control 30
(468). Logs: `late-confirm-60s-latecands100-vs-macondo.jsonl.gz`,
`late-confirm-60s-control-vs-macondo.jsonl.gz`, `late-macondo-confirm-latecands100.jsonl.gz`,
`late-macondo-confirm-control.jsonl.gz`.

| Run (fresh seeds) | Candidate − control: A's score (95%) | Candidate − control: points after the handover (95%) | Candidate / control: points after the handover |
|---|---|---|---|
| Section 5, 60 s, seed 9002, 100 pairs | −1.00 percentage points (−2.50 to 0.00) | +0.43 (−1.97 to +2.91) | +4.64 / +4.21 |
| **Section 6, 20 s, seed 9003, 300 pairs (primary)** | **+0.00 (−1.17 to +1.17)** | −0.00 (−1.93 to +1.92) | +4.30 / +4.30 |

In section 6 the two runs differ in 202 of 600 games. That the totals agree (303.5 wins
each; 2,579 and 2,581 points) is coincidence; the logs show different settings and
different games.

**Decision: section 6 fails its rule (the interval contains zero), so `latecands=100` is
not adopted, and the +2.25-point screen signal against Macondo did not replicate.** The
60 s condition of section 5 held on its point estimate, but section 5 had already
failed at 20 s.

**What the late-game experiments established, together.**
1. The cut of 30 candidates does bind in the late game. Macondo's judge prefers moves
   ranked 37 to 66 in 8 of 11 rejected decisions, and with 100 candidates Tilefish picks
   a move ranked beyond 30 in about one late decision in seven.
2. Widening the list does not change results, against frozen Tilefish (−0.52, −1.86 to
   +0.75) or against Macondo (±1.2 percentage points of score at 20 s, 300 pairs). The new
   moves are chosen by the same late-game evaluation, which section 4 found 14.7 points
   too optimistic in undecided positions. So the binding constraint is that evaluation
   (greedy play-outs that never fish or set up a bingo), not the candidate list.
3. Against Macondo from the same late positions, Tilefish gains about 4 points a game
   after the handover with either setting (+4.30, 95% +2.4 to +6.2, 300 pairs). The 20 s
   match's late-game conversion deficit (section 4) therefore probably depends on the
   positions real games reach (for example, open boards with bingo lanes) more than on
   late-game skill measured from these static-prefix positions. The prefix positions
   come from a static player and may not be typical.

**Next hypothesis, not yet tested:** late-game evaluation rather than breadth. Either
play-outs in which the side to move may fish (keep a bingo-prone leave with a small
play) when the bag is low, or an exact pre-endgame for 2 tiles in the bag, as Tilefish
already has for 1. Either should be screened first from real-game late positions
(sampled from the 20 s audit records), not from static-prefix positions, since point 3
suggests the two differ.

## 7. Why the late-game estimates run high, and endgame look-ahead in the play-outs (registered 4 October 2026, before any game)

**Diagnosis, from the existing logs** (`tools/latecheck.py`, `tools/keepfit.py` and the scripts
quoted below; "late" means 2 to 7 tiles in the bag before the move, the phase that plays
out to the end; intervals are 95%, bootstrap over deal pairs, since moves of one game share
its result).

| Tilefish's chosen-move estimate minus the result, estimates 0.1–0.9 | Over-estimate (95%) | Moves, pairs |
|---|---|---|
| Real games against Macondo, 20 s (`audit-…-20s`) | +14.7 points (+7.0 to +22.6) | 112, 93 |
| Static-prefix handovers against Macondo, 20 s (seeds 4002, 9003) | +17.5 (+12.0 to +23.1); +10.7 (+5.4 to +15.7) | 88, 64; 159, 119 |
| Static-prefix handovers, **Tilefish against Tilefish**, both sides (seeds 4001, 9001) | +12.8 (+6.0 to +19.5); +13.4 (+7.4 to +19.1) | 95, 75; 157, 119 |
| All five logs: the chosen move empties the bag | **+18.7 (+14.0 to +23.6)** | 212, 210 |
| All five logs: the chosen move leaves tiles in the bag | +10.6 (+6.7 to +14.4) | 399, 343 |
| All five logs, by tiles in the bag: 2 / 3 / 4 / 5 / 6 / 7 | +21.8 / +17.0 / +11.7 / +11.8 / +5.5 / +13.3 | 108 / 101 / 76 / 101 / 120 / 105 |

- **The bias is not specific to Macondo.** Tilefish over-rates its own late moves by the same
  amount against itself, where both sides' estimates cannot be right at once.
- **It is not sampling error.** In the 20 s audit, the chosen late move had a median of 48,171
  play-outs (standard error of its estimate: median 0.0009); only 20 of 325 late choices had
  fewer than 1,000. A move's estimate converges, so more time does not remove the bias, which
  explains most of "search settles early": the leader at 1/16, 1/4 and 1/2 of a late search
  is replaced in 14.8%, 4.3% and 2.8% of moves. Every late search runs to its clock (the
  closest challenger is never pruned); 28 of 30 candidates are pruned after a median of 98
  play-outs, and the prior is a large share of the posterior only for those.
- **It is largest where the play-outs' endgame matters most**: when the move empties the bag
  (the opponent then moves first in a perfect-information endgame) and with 2 or 3 tiles in
  the bag. The play-outs play every endgame with the static evaluator; in real games both
  engines solve endgames exactly. The chosen move is the best of about 30 by a play-out
  model with errors of its own, so its estimate inherits the most favourable error (the
  optimiser's curse); a monotonic correction of all estimates would not change any choice.
- **The opponent's rack is not uniform, but that is not the cause.** At Tilefish's decisions
  with 1–7 tiles in the bag, opponents held blanks 1.13–1.18 times and S 1.06–1.12 times as
  often as a uniform draw from the unseen tiles assumes (more in the middle game: 1.32 and
  1.24 for Macondo). A one-parameter model (each tile's odds exp(0.015 × its one-tile leave
  value); fitted on the development third of the deals, better than uniform on validation:
  −4.3935 against −4.4093 nats a decision; 27 free letter odds overfit) is now the option
  `keep`. On 139 development positions at 20 s it moved the chosen move's estimate by −0.6
  points (−0.9 to −0.3) and chose the control's move in 91.4% of positions, against 87.1%
  between two runs of the control itself. No measurable effect, so it goes no further.

**Candidate.** `champion:egk=6` (new option, off by default; the default is unchanged and
the fixed-work checksum is still 349.2837). In late play-outs, once the bag is empty, each
side plays the best of its 6 best moves by static equity, judged by playing each out
greedily (both sides) to the end, instead of the static best. This sees one move ahead in
the endgame: two-move outs and blocks of the opponent's out. On the same 139 development
positions it chose a different move from the control in 36.7% of positions (control against
itself: 12.9%), with an average estimate change of −0.1 points (−0.8 to +0.4). It costs
samples: the chosen move received a median of 320–1,108 play-outs by bag size instead of
9,527–24,822. Whether better endgames in the play-outs are worth 20 times fewer of them is
what the matches decide.

**Design.** Matches start from the late positions of real games (`tools/positions.py`: the
first decision with 7 or fewer tiles in the bag in each game of the six real-game logs with
move records, with both racks as they were and the bag reshuffled per deal pair by the
referee's seed; the actual draw order is never used). The 640 positions are split by deal
(sha256 of "seed:pair" mod 3) into `positions-late-dev.jsonl` (222), `-val` (222) and
`-test` (196). CSW24, 20 s a move, one thread each, Macondo `14c080b57608` `simming 5`,
frozen Tilefish `v2.2.1`.

| Run | A | B | Positions | Seed | Role |
|---|---|---|---|---|---|
| `egk-screen-macondo` | `champion:egk=6` | Macondo `simming 5` | dev, 222 pairs | 4101 | screen |
| `egk-screen-control` | `champion` | Macondo `simming 5` | dev, 222 pairs | 4101 | the same positions and bags with the default |
| `egk-screen-frozen` | `champion:egk=6` | `v2.2.1` `champion` | dev, 222 pairs | 4102 | screen against frozen Tilefish |
| `egk-confirm-macondo`, `-control`, `-frozen` | as above | as above | test, 196 pairs | 9101, 9101, 9102 | confirmation, only if the screen passes |

**Rules, fixed now.** Every run is played to full size and reported. Primary measure: A's
score from the handover, candidate minus control against Macondo, paired by deal pair
(bootstrap, 10,000 resamples); secondary: points gained after the handover, likewise.
- *The screen passes* if the primary point estimate is above zero, the secondary is not
  negative, and against frozen `v2.2.1` the candidate scores at least 50% and gains points
  after the handover (point estimates).
- *Promotion to the default* requires, in the confirmation, the primary measure's 95%
  interval above zero, the secondary point estimate not negative, and a score of at least
  50% against frozen `v2.2.1`. Otherwise the default stays and the result is reported with
  its numbers. A pass supports only this claim: better results from real late-game positions
  against this Macondo configuration at 20 s on one thread; longer budgets, four threads and
  MAGPIE would follow before any wider claim.

### Screen results (4 October 2026): the screen passes

Engine A built at `9e5154e` (the registered build); CSW24, 20 s a move, one thread each,
222 deal pairs from `positions-late-dev.jsonl` (mean 5.3 tiles in the bag at the handover);
candidate and control played identical handover positions and bags in all 222 pairs
(`tools/handover.py`). No illegal moves, crashes or forfeits in 1,332 games. Logs:
`egk-screen-macondo.jsonl.gz`, `egk-screen-control.jsonl.gz`, `egk-screen-frozen.jsonl.gz`.

| Run | Points A gains after the handover (95%) | A's score (95%) |
|---|---|---|
| `egk=6` vs Macondo `simming 5` (seed 4101) | +4.72 (+2.32 to +7.18) | 49.32% (47.75% to 50.90%) |
| default vs Macondo, same positions and bags | +2.87 (+0.47 to +5.31) | 48.99% (47.52% to 50.45%) |
| **Candidate − control, paired by deal pair** | **+1.84 (−0.64 to +4.34)** | **+0.34 points (−0.68 to +1.46)** |
| `egk=6` vs frozen `v2.2.1` (seed 4102) | +1.43 (+0.08 to +2.90) | 50.45% (49.55% to 51.35%) |

Under the registered rule the screen passes: the primary point estimate is above zero, the
secondary is not negative, and against frozen Tilefish the candidate scores at least 50%
and gains points. No interval for the primary measure excludes zero, so this is a lead for
the confirmation, not a result. (From these real-game positions the default itself gains
points on Macondo but scores slightly under 50%.)

**A defect found before the confirmation.** A second-model review of the candidate found
that the look-ahead's greedy play-out took one more turn when entered after five scoreless
turns plus a scoreless placement; it is fixed in `daaf441`, which the confirmation uses. The
case needs six scoreless turns in a row inside a play-out, so it cannot have affected the
screen materially, but the confirmation is the first test of the corrected build.

### Confirmation result (4 October 2026): not promoted; the default stays

Engine A built at `daaf441` (the candidate with the scoreless-turn fix); 196 deal pairs from
`positions-late-test.jsonl`, used once; seeds 9101 (both Macondo runs) and 9102; mean 5.1
tiles in the bag at the handover; identical handovers in all 196 pairs; no illegal moves,
crashes or forfeits in 1,176 games. Logs: `egk-confirm-macondo.jsonl.gz`,
`egk-confirm-control.jsonl.gz`, `egk-confirm-frozen.jsonl.gz`.

| Run | Points A gains after the handover (95%) | A's score (95%) |
|---|---|---|
| `egk=6` vs Macondo `simming 5` | +5.13 (+2.60 to +7.74) | 50.13% (48.34% to 51.79%) |
| default vs Macondo, same positions and bags | +3.85 (+1.59 to +6.25) | 49.87% (48.34% to 51.40%) |
| **Candidate − control, paired by deal pair** | **+1.28 (−0.56 to +3.14)** | **+0.26 points (−1.28 to +1.79)** |
| `egk=6` vs frozen `v2.2.1` | +2.30 (+0.46 to +4.21) | 52.04% (50.51% to 53.57%) |

**Decision: not promoted.** Promotion required the primary measure's 95% interval to lie above
zero; it contains zero. The secondary measure is positive and the frozen-Tilefish condition
holds, but the rule is not relaxed after the fact. `egk` stays an option, off by default.

**What it established.** In both the screen and the confirmation, on different real-game
positions, the look-ahead pointed the same way: +0.34 and +0.26 points of score and +1.84 and
+1.28 points a game against Macondo relative to the default, and 50.45% and 52.04% with +1.43
and +2.30 points a game against frozen Tilefish (the last interval excludes zero). That is the
most consistent late-game signal so far, but effects of this size (about one or two points a
game, a fraction of a percentage point of score) need roughly 600 deal pairs to resolve; the
pair-level standard deviation of the points difference is about 13. It is a lead, not a
result.

**A near-exact check of the mechanism** (`tools/exactpeg.py`, log `exact-dev-bag2.jsonl.gz`).
With exactly 2 tiles in the bag, a move that places two or more tiles empties the bag, so its
true value is the average over every possible draw of the solved endgame. On the development
positions (3 s per endgame; 45% of draws proven exact, the rest the solver's best line), the
play-outs' estimate for the chosen move exceeded this reference by **+4.1 points (+1.1 to +8.3;
27 positions)** with the default and **+2.8 (+1.0 to +4.7; 23)** with `egk=6`; the chosen move
was the reference's best among the evaluated moves in 25 of 27 and 21 of 23. So the static
endgame in the play-outs is a real but small part of the bias. Against game results the
over-estimate at bag 2 is about +22 points, so most of it comes from elsewhere: candidates are
the actual opponent racks (not uniform; the measured effect of the `keep` prior was small but
was averaged over bags 2–7) and the play after the move in real games, which neither the
play-outs nor this reference model.

**Next step, registered here but not run.** A powered test of `egk=6`: about 600 deal pairs of
real-game late positions never used before (the 222 of `positions-late-val.jsonl` plus about
400 extracted from fresh full games on seeds from 9200), primary measure the paired difference
in points after the handover against Macondo `simming 5` at 20 s (candidate − control), promoted
only if its 95% interval lies above zero and the score difference is not negative; then 60 s,
four threads and MAGPIE before any wider claim.

## 8. A powered test of the endgame look-ahead (registered 4 October 2026, before any game)

The next step of section 7, with its details fixed now. **Positions:** the 222 unused positions
of `positions-late-val.jsonl`, plus the late positions (`tools/positions.py`, first decision
with 7 or fewer in the bag, every game) of a fresh full-game match, `fresh-9200`: Tilefish
`main` at `df845b8` (`champion`, the default) against Macondo `14c080b57608` `simming 5`,
CSW24, 20 s a move, one thread, 200 deal pairs, seed 9200. That match is also reported as a
replication of section 3's 20 s result. The combined file, `positions-late-powered.jsonl`
(val first, then fresh-9200 in file order), is used once.

| Run | A | B | Seed | Role |
|---|---|---|---|---|
| `powered-macondo` | `champion:egk=6` (built at `df845b8`) | Macondo `simming 5` | 9201 | candidate |
| `powered-control` | `champion` (built at `df845b8`) | Macondo `simming 5` | 9201 | control: the same positions and bags |

CSW24, 20 s a move, one thread each, every pair of the file, no interim looks.
**Primary measure:** points A gains after the handover, candidate − control, paired by deal
pair (bootstrap over pairs, 10,000 resamples). **Secondary:** the same for A's score.
**Rule:** `egk=6` becomes the default only if the primary measure's 95% interval lies above
zero **and** the secondary point estimate is not negative; otherwise the default stays and
the result is reported with its numbers. Expected precision from section 7 (pair SD about 13
points): about ±1.0 points a game with 600 pairs.

**Addendum before any position game (same day).** `fresh-9200` played its 400 games with no
illegal moves, crashes or forfeits (log `fresh-9200.jsonl.gz`) and gave 400 positions, so
`positions-late-powered.jsonl` has 622. The workflow gives every job the same number of
pairs, so 620 = 20 jobs × 31 pairs are played: the file's first 620 lines; the last two (from
`fresh-9200`) are not used. Nothing else changes.
