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

### Results of section 8 (4 October 2026): more points, not more wins; not promoted

Engine A built at `df845b8`; 620 deal pairs, identical handover positions and bags in all
620 (mean 5.1 tiles in the bag); no illegal moves, crashes or forfeits in 2,480 games. Logs:
`powered-macondo.jsonl.gz`, `powered-control.jsonl.gz`.

| | Points A gains after the handover (95%) | A's score (95%) |
|---|---|---|
| `egk=6` vs Macondo `simming 5` | +6.58 (+5.19 to +7.99) | 50.73% (49.72% to 51.73%) |
| default vs Macondo, same positions and bags | +4.89 (+3.52 to +6.30) | 50.89% (49.88% to 51.90%) |
| **Candidate − control, paired by deal pair** | **+1.69 (+0.39 to +3.02)** | **−0.16 points (−1.01 to +0.69)** |

**Decision: not promoted; the default stays.** The primary measure's interval lies above
zero, but the rule also required the score difference not to be negative, and its point
estimate is −0.16. **What it shows:** the one-move endgame look-ahead reliably gains about
1.7 points a game from real late-game positions against Macondo at 20 s (consistent with
+1.84 and +1.28 in section 7), but those points do not turn into more wins; the score
interval rules out a gain of more than about 0.7 percentage points. Tilefish ranks moves by
winning chance, so a spread gain alone does not justify changing the default. `egk` remains
an option for analysis.

**Fresh full games (`fresh-9200`, the default against Macondo `simming 5`, 20 s, 200 pairs,
seed 9200):** 52.25% (47.96% to 56.54%), +16 Elo (−14 to +46), +21.8 points a game (+12.8 to
+30.8): level on wins and ahead on points, as in sections 3 and 4.

## 9. The tournament-budget match: four threads, 60 s a move, against BestBot's settings (registered 4 October 2026, before any game)

**Question.** At 20 s a move on one core, Tilefish is level on wins with Macondo using
BestBot's settings (sections 3, 4 and 8). BestBot plays in production with more time and three
or four cores. This match measures the two engines at a budget close to that, as planned in
section 5. It is a measurement, not a test of a change.

**Setup.** Tilefish `main` at `10deb30` with `champion:iters=100000000` (the per-candidate cap
of 1,000,000 bound on at least 10% of moves in the section 5 pilot; the default search is
otherwise unchanged) against Macondo `14c080b57608` `simming 5`. CSW24, **60 s a move, 4
threads each**, one game at a time on each 4-vCPU runner (only the engine on move computes),
seed 9300 (never used), **416 deal pairs** (52 jobs of 8 pairs; the section 5 plan asked for
409, and 8 pairs a job keeps each job inside the runners' time limit). No interim looks: the
match is played to full size and reported whatever the outcome. Recorded as in section 4:
CPU seconds per move for each engine, game length, Tilefish's iterations.

**Primary measure:** Tilefish's score (draws half) with a 95% interval from a bootstrap over
deal pairs, and the Elo difference. **Secondary:** points a game. **What may be claimed:**
"ahead at this budget" only if the score interval lies entirely above 50%, "behind" only if
entirely below, otherwise "level within the interval". Nothing beyond this opponent, this
configuration, CSW24 and this budget follows; no claim of the strongest engine follows from
one match.

**Status (5 October 2026): incomplete, being completed.** The run played 408 of the 416
registered pairs: 51 of its 52 jobs finished, and job 36 (pairs 288–295) lost its runner three
hours into play and uploaded nothing. Under the rule above, 408 pairs are not a result (the
record under "Results recorded automatically" says so). Only that job is being re-run ("Re-run
failed jobs" replays the same deals with the same builds); `report.yml` now pools every job's
log itself and records a run again when its log changes, so the completed run is recorded
with the rule's verdict when it finishes.

## 10. Deeper simulation of the finalists in the middle game (registered 4 October 2026, before any game)

**Why.** Tilefish's lead over Macondo in the opening and middle game falls from +33 points a
game at 5 s to +5 at 20 s (section 4), and its 2-ply search has settled on its final choice
by about 1/8 of a 20 s move; the rest of the time refines the duel of the top two candidates
(96% of iterations). Section 1 found 4-ply simulation of all 30 candidates worse at 20 s,
because every candidate then gets fewer samples. The candidate spends the settled half of the
time differently: **2-ply simulation of all candidates for half the move, then 4-ply
simulation of the top 3 for the rest**, choosing by the second stage. It targets exactly the
time that more thinking currently wastes.

**Candidate:** `champion:deep=3,deepplies=4,deepfrac=0.5` (new option, off by default; the
fixed-work checksum is unchanged at 349.2837; a self-test checks that the choice is a legal
first-stage finalist). Built at the commit that adds this section.

| Run | A | B | Pairs | Seed | Role |
|---|---|---|---|---|---|
| `deep-screen-frozen` | candidate | frozen `v2.2.1` `champion` | 200 | 9400 | screen against frozen Tilefish |
| `deep-screen-macondo` | candidate | Macondo `14c080b57608` `simming 5` | 200 | 9200 | the same deals as `fresh-9200` (section 8), where the default scored 52.25% |

CSW24, 20 s a move, one thread each, full games, played to full size.
**Screen rule, fixed now:** it passes if against frozen Tilefish the candidate scores above
50% with a positive spread, **and** against Macondo its score on the `fresh-9200` deals is
not below the default's 52.25% (point estimates; the paired difference by deal is also
reported). A pass leads only to a registered confirmation on fresh seeds with more pairs, at
20 s and at 60 s with four threads; nothing becomes the default from a screen.

### Results of section 10 (4 October 2026): the screen fails; the default stays

Both runs played their 200 registered pairs (records under "Results recorded automatically";
logs `deep-screen-frozen.jsonl.gz`, `deep-screen-macondo.jsonl.gz`; `tools/screen.py`).

| Run, 200 deal pairs | Candidate's score (95%) | Spread a game (95%) | Rule |
|---|---|---|---|
| against frozen `v2.2.1` | 51.38% (48.00% to 54.87%) | +5.3 (−1.1 to +11.7) | met (score > 50%, spread > 0) |
| against Macondo, `fresh-9200` deals | 50.12% (45.62% to 54.62%) | +13.1 (+4.8 to +21.4) | **not met** (needs ≥ 52.25%) |
| candidate − default on the same Macondo deals | −2.12 points (−8.12 to +4.00) | −8.7 (−20.6 to +3.1) | |

**Decision: the screen fails and the default stays,** as the rule fixed beforehand requires.
The 4-ply second stage changed the first stage's choice in 12.0% of 2-ply moves against frozen
Tilefish and 12.9% against Macondo, so it acted; against Macondo the changes did not help, and
the paired difference leans the other way on both measures. Section 13 later found why more
2-ply time is wasted; it does not rescue this candidate. `deep` stays an option.

## 11. Late estimates, close games and where the points come from: a reanalysis of the logs (4 October 2026)

No new games. Every number below comes from `tools/latebias.py` on the logs already in this
folder that have move records (95% intervals by bootstrap over deal pairs). It revisits two
explanations given in sections 4 and 7: that Tilefish's late-game estimates run high, and
that it loses more of the close games.

**Macondo's own late estimates run as high as Tilefish's** (`latebias.py calib
experiments/*.jsonl.gz`). Macondo reports a winning chance for the move it plays, so its
estimates can be set against the results of the same games.

| Estimate minus result, own estimate 0.1–0.9 | Late (2–7 in the bag) | Middle game (8 or more) |
|---|---|---|
| Tilefish, the games against Macondo | +12.9 (+11.2 to +14.5), 2,008 decisions | +1.2 (−1.5 to +4.1), 7,744 |
| Macondo, the same games | **+15.9 (+14.4 to +17.6)**, 2,096 | +4.3 (+1.4 to +7.0), 7,604 |

Each side on move rates its position better than the other side does. After a late Tilefish
move, its estimate plus the opponent's next estimate exceeds 1 by 26.0 points (24.5 to 27.6;
932); after a late Macondo move, by 22.5 (21.2 to 24.0; 1,428). The late over-estimate is
therefore a property both simulators share, not a defect of Tilefish's play-outs, and it
cannot explain a difference between the two engines. This section does not identify its
cause.

**The bag-emptying effect was the bag size.** Section 7 found the over-estimate larger when the
chosen move empties the bag (+18.7 against +10.6). Within the same bag sizes the difference
goes away for Tilefish: with 2–3 in the bag, +19.9 (+16.5 to +23.2; 493) for moves that empty
it and +19.9 (+15.6 to +24.2; 291) for moves that do not; with 4–5, +13.9 (+7.5 to +20.3) and
+12.3 (+9.3 to +15.4). Emptying moves are simply most common when the bag is nearly empty,
where both engines over-rate the most. (Macondo: +24.4 and +19.9 with 2–3; +23.6 and +11.9
with 4–5.)

**The opponent's rack explains little of it** (`latebias.py racks`). At Tilefish's late
decisions, the opponent's actual rack ranks at the 54.8th percentile (53.7 to 56.0) of uniform
seven-tile draws from the unseen tiles, scored by the mean six-tile leave value of
`ENABLE.leaves`, +1.29 leave points (+0.96 to +1.61) above the uniform average; at Macondo's,
the 53.9th. By the rack's third among the uniform draws, Tilefish's over-estimate is +1.5,
+9.4 and +23.0 (Macondo's +7.5, +9.9 and +26.7). A calibrated estimate would give thirds
centred on zero; these are all shifted by about +11, and the excess of strong racks (41.5% of
decisions in the top third instead of 33.3%) accounts for only one or two of the 13 points.
This agrees with section 7, where the `keep` prior had no measurable effect.

**Where Tilefish's points come from** (`latebias.py decided --by either` on the two 20 s
full-game matches with move records, `audit-…-20s` and `fresh-9200`, 800 games). A game counts
as decided at the first decision where either engine's estimate, read as Tilefish's chance, is
1% or less or 99% or more.

| 800 games, 20 s, against Macondo `simming 5` | Value |
|---|---|
| Tilefish's mean final margin | +17.8 (+11.2 to +24.5) |
| ...the margin when the game was decided (the final margin if never) | +10.2 (+5.3 to +15.1) |
| ...points gained after the game was decided | **+7.6 (+4.4 to +10.8)** |
| Decided for Tilefish: games, its score, points it gained afterwards | 257, 99.2%, +45.0 a game |
| Decided against Tilefish: games, its score, points it gained afterwards | 245, 1.2%, −22.4 a game |
| Not decided while tiles remained: games, Tilefish's score, margin | 298, **50.3%**, +2.9 a game |

Once a game is settled, the winner keeps adding points, and Tilefish adds about twice as many
as Macondo does (+45.0 against +22.4 a game; a game settled earlier also leaves more moves in
which to add them). That pushes Tilefish's wins out of the moderate margins into the large
ones more than it does its losses, which is the pattern section 4 read as losing more of the
close games: its share of decisive games is 43.0% at margins of 11–25, 37.0% at 26–50 and
62.0% above 100 in these 800 games. In the games still open while tiles remained, the two
engines are level. The split depends on whose estimate decides a game, because each engine's
late estimates run high in its own favour: by Tilefish's estimate alone, +19.4 points come
after the decision and Tilefish scores 40.3% in the open games; by Macondo's alone, −6.3 and
62.5%. Requiring either engine's estimate is the symmetric choice and is the one reported.

**Section 4's late conversion gap does not replicate on fresh games** (`latebias.py convert`;
the lead when the bag first holds 7 or fewer; Macondo's conversion minus Tilefish's, points).

| Lead | `audit-…-20s` (section 4) | `fresh-9200` (section 8, not examined before) | Both, 800 games |
|---|---|---|---|
| 1–40 | +5.4 (−10.8 to +21.6) | +6.3 (−9.9 to +23.3) | +5.9 (−6.2 to +17.5) |
| 41–100 | +10.8 (+1.0 to +20.9) | +3.0 (−6.2 to +12.4) | +6.7 (−0.2 to +13.7) |
| 101+ | −0.9 (−6.0 to +3.5) | 0.0 | −0.5 (−3.4 to +1.9) |

Section 4 marked the 41–100 cut as not corrected for the several cuts examined; on new games
it shrinks from +10.8 to +3.0. Macondo converts a little more in both lower bands in both
matches, so a small late-game difference remains possible, but none of these intervals
excludes zero on the fresh games.

**What this changes.** (1) The late over-estimate is shared with Macondo, so it is not
evidence of a Tilefish weakness; from real late positions Tilefish gains points with a level score (section
8's control: +4.89 points, 50.89%). (2) At 20 s, Tilefish's lead in points a game is not
evidence of an edge in winning: part of it is added after the result is settled, and the
games still open are level. Points a game remain a secondary measure everywhere in this
file; this is why. (3) Section 4's account of the close games is superseded by this section.

**Added to section 9 before any of its results are seen:** the tournament log will also be
reported with `latebias.py decided --by either`, `convert` and `calib`, as descriptive
analyses. Section 9's rule (the score interval) is unchanged and is the only basis for its
claim.

## 12. Opponent-rack inference in matches (registered 4 October 2026, before any game)

**Why.** Tilefish infers the opponent's rack from their last play: each possible leave is
weighted by how close the play was to the best one that leave allowed (`InferenceParams`:
regret scale 4 points, a floor of 0.10 on every leave, at most 1 s or a quarter of the
move). It does this whenever it knows the opponent's last move: in the terminal game, the
review and the browser version, which track the game. **Under the referee it never has.**
Engines receive only a CGP, so every match in this file measured Tilefish without inference
(section 4 noted this for both engines). Macondo's inference was piloted in section 4 and
did not help Macondo at 20 s. Tilefish's has never been measured.

**Change.** The engine protocol now accepts the referee's `history <GCG>` (sent with
`--a-history`) before `position cgp`. Tilefish replays it and uses it only if the replay
reproduces the CGP's board exactly and its last move is the opponent's play; otherwise the
position stands as the CGP alone. No search code changes: the fixed-work checksum is still
349.2837. A self-test checks that a matching history switches inference on, a mismatched
one is ignored, and in a refereed match engine A infers at every decision where it can and
at no other, while engine B (no history) never does.

**Candidate:** Tilefish `champion` built at the commit that adds this section, with the
game's history (`history=a`). Everything else as in sections 8 and 10.

| Run | A | B | Pairs | Seed | Role |
|---|---|---|---|---|---|
| `infer-screen-frozen` | candidate, with history | frozen `v2.2.1` `champion` | 200 | 9500 | screen against frozen Tilefish |
| `infer-screen-macondo` | candidate, with history | Macondo `14c080b57608` `simming 5` | 200 | 9200 | the `fresh-9200` deals (section 8), where the default without history scored 52.25% |

CSW24, 20 s a move, one thread each, full games, played to full size.
**Screen rule, fixed now:** it passes if against frozen Tilefish the candidate scores above
50% with a positive spread, **and** against Macondo its score on the `fresh-9200` deals is
not below the default's 52.25% (point estimates; the paired difference by deal is also
reported). A pass leads only to a registered confirmation on fresh seeds (about 400 pairs
at 20 s, then 60 s with four threads); only then would the strength figures in `README.md`
be re-measured with history. A failure is reported with its numbers, and inference stays as
it is in interactive play.

### Results of section 12 (4 October 2026): the screen fails; matches stay without history

Both runs played their 200 registered pairs (logs `infer-screen-frozen.jsonl.gz`,
`infer-screen-macondo.jsonl.gz`). The history reached the engine: inference ran at 71.3% of
the candidate's simulated decisions in both runs (the rest came after an exchange, a pass or a
bingo by the opponent, which leave nothing to infer, or opened the game), with a median of
0.11 s (frozen) and 0.13 s (Macondo) of the move.

| Run, 200 deal pairs | Candidate's score (95%) | Spread a game (95%) | Rule |
|---|---|---|---|
| against frozen `v2.2.1` | 49.12% (45.75% to 52.50%) | +1.5 (−4.5 to +7.7) | **not met** (needs > 50%) |
| against Macondo, `fresh-9200` deals | 49.88% (45.12% to 54.75%) | +18.0 (+8.5 to +28.0) | **not met** (needs ≥ 52.25%) |
| candidate − default on the same Macondo deals | −2.38 points (−8.38 to +3.75) | −3.8 (−16.8 to +9.2) | |

**Decision: the screen fails.** Inference is not measured to help in matches, so the strength
figures in `README.md` stay as measured without history, and inference stays as it is in
interactive play. Neither leg comes close: no gain against frozen Tilefish, and on the
Macondo deals the score is a little below the default's. This agrees with section 4's pilot of
Macondo's own inference, which did not help Macondo at 20 s. Section 13 later found a defect
in inference cut short by the clock; no move of these runs was affected (the longest inference
took 0.611 s of the 1 s allowed).

## 13. How the simulation spends its time: a replay study (5 October 2026)

No new games, and no change to the default search. Sections 4 and 10 found that the 2-ply
search settles early and that the rest of a move refines the duel of its last two candidates.
This section asks what the match logs cannot answer: does pruning 28 of the 30 candidates after
about 100 iterations drop moves that more iterations would have preferred, and would another
way of sharing out the iterations choose better with the same work?

**What the 60 s tournament log shows** (`python3 tools/profile.py
experiments/tournament-60s-4t.jsonl.gz`; 7,534 2-ply decisions, four threads). By the end,
28.0 of 30 candidates are pruned and the top two have 99.7% of all iterations: the most
simulated a median of 825,933 iterations, the others a median of 104. The leader at 1/64 of
the search differs from the final choice in 3.2% of moves, at 1/16 in 1.6%, at 1/4 in 0.8%.
The last 15/16 of a 60 s move change the move about once in 60 decisions.

**Method** (`tools/allocstudy.cpp`, which includes the engine's source and calls its
simulator). Positions come from self-play by the static player on ENABLE with its trained
leaves and win model; CSW24 could not be downloaded where this ran. In each, the top 30
candidates by static equity were simulated a fixed number of iterations with no pruning, and
every result was kept: 60 middle-game positions (bag 20–70, 2 plies, 20,000 iterations each)
and 40 late ones (bag 2–7, played out to the end, 4,000 each). Iterations use common random
numbers, so iteration k deals every candidate the same tiles. Each policy is replayed on the
first half of the iterations with a budget of candidate-iterations and ranks at the end as
`Simulator::rank` does (prior tau 4; 10 in the play-out phase). Its choice is scored on the
second half, which it has not seen: the objective (win + 0.0008 × equity, the engine's) of the
second half's best candidate minus that of the choice, in win %. On this machine one thread
does about 8,600 candidate-iterations a second in the 2-ply phase (`benchsim`), so 12,000 is
about 1.4 s; a play-out to the end costs more per iteration.

The policies: the engine (one thread's batches, pruning at z 2.4 after each batch from 96
iterations on, keeping the closest challenger); z 3.2; no pruning before 10, 25 or 50% of the
budget; at least four survivors kept; uniform (every candidate the same iterations); sequential
halving (ceil(log2 30) = 5 rounds, the better half by simulated mean going on).

**Middle game** (60 positions; mean loss in win %, and in brackets the share of positions
where the choice is the second half's best):

| Budget (candidate-iterations) | 3,000 | 6,000 | 12,000 | 20,000 |
|---|---|---|---|---|
| About this long on one thread | 0.35 s | 0.7 s | 1.4 s | 2.3 s |
| Engine; no pruning before 10% or 25% (identical) | 0.118 (81.7%) | 0.018 (96.7%) | **0.000 (100%)** | **0.000 (100%)** |
| z 3.2 | 0.132 | 0.022 | 0.000 | 0.000 |
| No pruning before 50% | 0.118 | 0.029 | 0.004 | 0.000 |
| At least four survivors | 0.120 | 0.022 | 0.000 | 0.004 |
| Uniform | 0.154 | 0.081 | 0.058 | 0.005 |
| Sequential halving | **0.040 (90.0%)** | 0.007 | 0.000 | 0.000 |
| Pruned early by the engine, rated above its choice by the second half | none pruned yet | 0 of 1,649 | 0 of 1,671 | 0 of 1,679 |

Paired by position, halving against the engine at 3,000: +0.078 win % (s.e. 0.035); at 6,000:
+0.011 (0.018).

**Late game** (40 positions, play-outs to the end; the dumps hold 2,000 iterations a half, which
limits the budgets):

| Budget (candidate-iterations) | 1,500 | 3,000 | 6,000 |
|---|---|---|---|
| Engine; no pruning before 10% or 25% (identical) | 0.378 (75.0%) | 0.242 (77.5%) | 0.150 (85.0%) |
| z 3.2 | 0.378 | 0.242 | 0.096 (87.5%) |
| At least four survivors | 0.378 | 0.242 | 0.150 (82.5%) |
| No pruning before 50% | 0.378 | 0.242 | 0.241 |
| Uniform | 0.378 | 0.244 | 0.124 |
| Sequential halving | 0.500 | 0.506 | 0.148 |
| Pruned early by the engine, rated above its choice | none pruned yet | none pruned yet | 1 of 1,053 |

Every difference from the engine is within two standard errors (z 3.2 at 6,000: +0.053, s.e.
0.053). The second half's 2,000 iterations are themselves noisy, which lowers every row's share
of best choices; the comparison between rows is still fair.

**Candidates ranked 31 to 60** (`allocstudy gen ... cands 60`, then `allocstudy wide`; 30 more
middle-game positions, 60 candidates, 6,000 iterations each). In each position the best of the
top 30 and the best of ranks 31–60 were picked on the first half and compared on the second.
In **all 30** the best of 31–60 was worse, by 1.0 to 29.5 win % (median 8.9), each time by more
than 2 standard errors (the closest: 0.99, s.e. 0.023). The best of the top 30 was the static
#1 in 23 of the 30 (in the 60 s CSW24 log the chosen move is the static #1 in 65.3% of 2-ply
moves, so these positions are probably easier than real games).

**What this shows.**
1. **The early pruning is safe.** In the middle game no candidate it dropped was better by the
   second half's measure (0 of about 1,670 at each budget); in the late phase, 1 of 1,053.
2. **The middle-game choice is settled within a second or two.** From 12,000
   candidate-iterations (about 1.4 s on one thread here) the engine picks the move a much longer
   2-ply search prefers in all 60 positions. Beyond that no way of sharing out the iterations
   can improve the 2-ply choice, which agrees with the 60 s log. The time Tilefish adds at 20 or
   60 s is not misallocated: it goes to a question that has already been answered, which fits
   its lead shrinking with time (sections 3 and 4).
3. **Only very short searches would gain from another policy.** Below about half a second,
   sequential halving loses less (+0.078 win % a decision at 3,000). The browser's Strong level
   and every match in this file think longer, so no change is proposed.
4. **The late phase is harder** (0.15 win % lost at 6,000), and no policy is clearly better at
   the budgets this data allows.
5. **Thirty middle-game candidates are enough.** No candidate below the static cut came close
   in 30 positions, which agrees with section 2 (`cands=15` leaned negative) and section 5
   (100 late candidates did not help).

**Limits.** ENABLE, not CSW24. Positions from static self-play may be easier than those of real
games; the 60 s log, where 1.6% of CSW24 moves still change after 1/16 of the search, says
real decisions are settled almost but not quite as early. The second half measures the 2-ply
roll-out value, the quantity the search estimates, so this shows whether the search finds what
it is looking for, not whether that is the best move in the game; sections 1 and 10 test that.

**What follows (proposals; none registered).** In the middle game, neither more 2-ply samples
nor more candidates change the choice after the first second or two, so a gain at 20–60 s has to
come from spending the settled time on a different question. Each of these needs a registered
screen as in sections 8–12:
- a different evaluation of the finalists. Section 10's 4-ply second stage was one; it passed
  against frozen Tilefish (51.4%) and failed against Macondo (50.1% against the control's
  52.25%);
- under a game clock (the browser, `play`), stopping once the leader has held for a while and
  keeping the time for harder moves. Fixed-time matches cannot measure this.

**Also fixed on 5 October (no effect on any result here).** Opponent-rack inference enumerates
the possible leaves exactly when there are at most 3,000 of them. It weighed them in letter
order, so a clock that cut it short kept the leaves early in the alphabet: after TRAIN at 8D, at
0.03 s a move, the A was on the opponent's rack 51% of the time instead of 25%. It now weighs
them in a random order and keeps every leave's prior weight; a complete pass gives the same
model as before, and a self-test covers it. Section 12's runs were not affected: in their
5,678 decisions with inference, the longest took 0.611 s of the 1 s it was allowed, so no
enumeration was cut short.

## Results recorded automatically

Each run below was recorded by .github/workflows/report.yml when it finished: its pooled
log and the verdict of the rule registered for it before any game. Prose comes later, by hand.

### Recorded automatically: `deep-screen-frozen` (section 10)

Run 37209468373, recorded 2026-10-04 by `tools/report.py`; log `experiments/deep-screen-frozen.jsonl.gz`; 200 of 200 registered deal pairs; run conclusion: success.

```text
experiments/deep-screen-frozen.jsonl.gz: seed 9400, 200 deal pairs
  A's score   51.38% (48.00% to 54.87%)
  A's spread    +5.3 a game (-1.1 to +11.7)
  rule: score > 50.00%: met
  rule: spread > +0.0: met
  decision: PASSES (point estimates, as registered)
```

This leg's condition under section 10's screen rule: **met**. The screen's decision needs the other leg too; it is added when that run is recorded.


### Recorded automatically: `deep-screen-macondo` (section 10)

Run 37209469683, recorded 2026-10-04 by `tools/report.py`; log `experiments/deep-screen-macondo.jsonl.gz`; 200 of 200 registered deal pairs; run conclusion: success.

```text
experiments/deep-screen-macondo.jsonl.gz: seed 9200, 200 deal pairs
  A's score   50.12% (45.62% to 54.62%)
  A's spread   +13.1 a game (+4.8 to +21.4)
  control on the same 200 deal pairs (400 first positions checked identical): score 52.25%
  paired difference, candidate - control: score -2.12 points (-8.12 to +4.00); spread -8.7 (-20.6 to +3.1)
  rule: score >= 52.25%: NOT met
  decision: FAILS (point estimates, as registered)
```

This leg's condition under section 10's screen rule: **not met**. The other leg (`deep-screen-frozen`, recorded earlier) was met, so **the screen FAILS**. The default stays.


### Recorded automatically: `tournament-60s-4t` (section 9)

Run 37208427774, recorded 2026-10-04 by `tools/report.py`; log `experiments/tournament-60s-4t.jsonl.gz`; 408 of 416 registered deal pairs; run conclusion: failure.

```text
experiments/tournament-60s-4t.jsonl.gz: seed 9300, 408 deal pairs
  A's score   48.71% (45.89% to 51.59%)
  A's spread    +3.4 a game (-2.1 to +8.9)
```

```text
run 2026-10-04T14:14:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:15:00Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:59Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:15:45Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:15:44Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:14Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:13Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:16:59Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:16:58Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:32Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:31Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:17:38Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:17:37Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:30Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:29Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:18:57Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:18:56Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:19Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:18Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:51Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:50Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:12Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:11Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:46Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:46Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:52Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:51Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:18:01Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:18:00Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:01Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:00Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:29Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:28Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:09Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:08Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:16:22Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:16:21Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:08Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:07Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:09:27Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:09:26Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:30Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:29Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:13Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:11Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:54Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:53Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:05Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:04Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:38Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:37Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:58Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:57Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:17:12Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:17:10Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:48Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:47Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:59Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:58Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:40Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:39Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:26Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:25Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:39Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:38Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:08Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:07Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:52Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:51Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:18:38Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:18:37Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:34Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:33Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:17:24Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:17:23Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:20:06Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:20:05Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:26Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:25Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:08Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:07Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:34Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:33Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:51Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:51Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:46Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:45Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:41Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:40Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:14Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:13Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:36Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:35Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:39Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:38Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
A: 816 games in 408 units (pairs)   W 397  D 1  L 418   match score 397.5 / 816
A's score 48.71%, 95.0% interval 45.89% to 51.54%
standard Elo -9, 95.0% interval -29 to +11
bootstrap over pairs (10000 resamples): score 45.96% to 51.47%, Elo -28 to +10
spread +3.4 points a game, 95.0% interval -2.2 to +8.9
one-sided p (A no better than even) = 0.8140
time a move: A 53.31s (max 60.42s), B 19.94s (max 58.59s); illegal/crash/slow events: 4
```

```text
816 games; decided (A's chance by either's estimate <= 0.01 or >= 0.99) before the end: 535
  A's mean final margin                                +3.4 (  -2.1 to   +8.7)  n= 816  pairs=408
    margin when decided (the final margin if never)    +3.6 (  -0.8 to   +8.1)  n= 816  pairs=408
    points gained after the game was decided           -0.2 (  -3.1 to   +2.5)  n= 816  pairs=408
  decided for A: 267 games, A's score 0.996, points A gained afterwards +22.2 a game
  decided against A: 268 games, A's score 0.002, points A gained afterwards -22.8 a game
  never decided: 281 games, A's score 0.466, mean margin -0.4
A's share of the decisive games by final margin:
  margin   1-10   games   62  A wins   34  B wins   28  A's share 0.548
  margin  11-25   games  103  A wins   46  B wins   57  A's share 0.447
  margin  26-50   games  124  A wins   52  B wins   72  A's share 0.419
  margin  51-100  games  250  A wins  112  B wins  138  A's share 0.448
  margin 101-9999 games  276  A wins  153  B wins  123  A's share 0.554
```

```text
816 games; A's lead when the bag first holds 7 or fewer; the leader's score
  lead 1-40: A converts  67.3% (n=101), B  77.5% (n=120); B minus A +10.2 ( -1.2 to +22.2)
  lead 41-100: A converts  91.4% (n=174), B  95.1% (n=153); B minus A  +3.7 ( -1.4 to  +8.9)
  lead 101-9999: A converts 100.0% (n=134), B 100.0% (n=130); B minus A  +0.0 ( +0.0 to  +0.0)
```

**Incomplete run: not a result under the registered rule** (the rule requires the registered number of deal pairs, played to full size). Shown for the record only.


### Recorded automatically: `infer-screen-frozen` (section 12)

Run 37219855057, recorded 2026-10-04 by `tools/report.py`; log `experiments/infer-screen-frozen.jsonl.gz`; 200 of 200 registered deal pairs; run conclusion: success.

```text
experiments/infer-screen-frozen.jsonl.gz: seed 9500, 200 deal pairs
  A's score   49.12% (45.75% to 52.50%)
  A's spread    +1.5 a game (-4.5 to +7.7)
  rule: score > 50.00%: NOT met
  rule: spread > +0.0: met
  decision: FAILS (point estimates, as registered)
```

This leg's condition under section 12's screen rule: **not met**. The screen's decision needs the other leg too; it is added when that run is recorded.


### Recorded automatically: `infer-screen-macondo` (section 12)

Run 37219857657, recorded 2026-10-04 by `tools/report.py`; log `experiments/infer-screen-macondo.jsonl.gz`; 200 of 200 registered deal pairs; run conclusion: success.

```text
experiments/infer-screen-macondo.jsonl.gz: seed 9200, 200 deal pairs
  A's score   49.88% (45.12% to 54.75%)
  A's spread   +18.0 a game (+8.5 to +28.0)
  control on the same 200 deal pairs (400 first positions checked identical): score 52.25%
  paired difference, candidate - control: score -2.38 points (-8.38 to +3.75); spread -3.8 (-16.8 to +9.2)
  rule: score >= 52.25%: NOT met
  decision: FAILS (point estimates, as registered)
```

This leg's condition under section 12's screen rule: **not met**. The other leg (`infer-screen-frozen`, recorded earlier) was not met, so **the screen FAILS**. The default stays.


### Recorded automatically: `tournament-60s-4t` (section 9)

Run 37208427774, recorded 2026-10-06 by `tools/report.py`; log `experiments/tournament-60s-4t.jsonl.gz`; 416 of 416 registered deal pairs; run conclusion: success.

```text
experiments/tournament-60s-4t.jsonl.gz: seed 9300, 416 deal pairs
  A's score   48.62% (45.79% to 51.44%)
  A's spread    +3.2 a game (-2.3 to +8.7)
```

```text
run 2026-10-04T14:14:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:15:00Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:59Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:15:45Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:15:44Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:14Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:13Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:16:59Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:16:58Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:32Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:31Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:17:38Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:17:37Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) Platinum 8370C CPU @ 2.80GHz; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:30Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:29Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:18:57Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:18:56Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:19Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:18Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:51Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:50Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:12Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:11Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:46Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:46Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:52Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:51Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:18:01Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:18:00Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:01Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:00Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:29Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:28Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:09Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:08Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:16:22Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:16:21Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:37Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:36Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:08Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:07Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:09:27Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:09:26Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:30Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:29Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:13Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:11Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:54Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:53Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:05Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:04Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:38Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:37Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:58Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:57Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-05T21:17:57Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-05T21:17:56Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:17:12Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:17:10Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:48Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:47Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:59Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:58Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:40Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:39Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:26Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:25Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:39Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:38Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:08Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:07Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:52Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:51Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:18:38Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:18:37Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:19:34Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:19:33Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:17:24Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:17:23Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T17:20:06Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T17:20:05Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:26Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:25Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:08Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:07Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:34Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:33Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V74 80-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:51Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:51Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 9V45 96-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:07:46Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:07:45Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:41Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:40Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x INTEL(R) XEON(R) PLATINUM 8573C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:14Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:13Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x Intel(R) Xeon(R) 6973P-C; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T16:08:36Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T16:08:35Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
run 2026-10-04T14:14:39Z: A (proto:./tf_a --lexicon CSW24.kwg --threads 4 --quiet) vs B (proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 4 simming 5), 60000 ms/move, seed 9300, CSW24.txt sha256 4200a40a888042ac
  a_info: engine: Tilefish | commit: 10deb30dd0a81d98836c7447d7c23dd0d99de54d (10deb30) | setting: champion:iters=100000000 | toolchain: g++ (Ubuntu 13.3.0-6ubuntu2~24.04.1) 13.3.0, -O3 -march=native | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
  b_info: engine: Macondo | commit: 14c080b576080ab38d090bfd238a83092f7f59d3 | tracked files changed: 1 (threads.patch) | toolchain: go version go1.26.1 linux/amd64 | built: 2026-10-04T14:14:38Z | settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM | bot and plies: simming 5 | data: Macondo's strategy and letterdistributions; CSW24.kwg/.klv2 from get-lexicon | cpu: 4 x AMD EPYC 7763 64-Core Processor; Thread(s) per core: 2;Core(s) per socket: 2;Socket(s): 1
A: 832 games in 416 units (pairs)   W 404  D 1  L 427   match score 404.5 / 832
A's score 48.62%, 95.0% interval 45.82% to 51.42%
standard Elo -10, 95.0% interval -29 to +10
bootstrap over pairs (10000 resamples): score 45.79% to 51.44%, Elo -29 to +10
spread +3.2 points a game, 95.0% interval -2.2 to +8.7
one-sided p (A no better than even) = 0.8334
time a move: A 53.27s (max 60.42s), B 20.00s (max 58.59s); illegal/crash/slow events: 4
```

```text
832 games; decided (A's chance by either's estimate <= 0.01 or >= 0.99) before the end: 545
  A's mean final margin                                +3.2 (  -2.4 to   +8.8)  n= 832  pairs=416
    margin when decided (the final margin if never)    +3.7 (  -0.6 to   +8.3)  n= 832  pairs=416
    points gained after the game was decided           -0.5 (  -3.5 to   +2.2)  n= 832  pairs=416
  decided for A: 271 games, A's score 0.996, points A gained afterwards +22.0 a game
  decided against A: 274 games, A's score 0.002, points A gained afterwards -23.3 a game
  never decided: 287 games, A's score 0.467, mean margin -0.3
A's share of the decisive games by final margin:
  margin   1-10   games   62  A wins   34  B wins   28  A's share 0.548
  margin  11-25   games  108  A wins   48  B wins   60  A's share 0.444
  margin  26-50   games  126  A wins   53  B wins   73  A's share 0.421
  margin  51-100  games  256  A wins  115  B wins  141  A's share 0.449
  margin 101-9999 games  279  A wins  154  B wins  125  A's share 0.552
```

```text
832 games; A's lead when the bag first holds 7 or fewer; the leader's score
  lead 1-40: A converts  68.0% (n=103), B  78.2% (n=124); B minus A +10.3 ( -1.5 to +22.0)
  lead 41-100: A converts  91.6% (n=178), B  95.2% (n=155); B minus A  +3.6 ( -1.6 to  +8.7)
  lead 101-9999: A converts 100.0% (n=135), B 100.0% (n=133); B minus A  +0.0 ( +0.0 to  +0.0)
```

Under the rule registered in section 9: **level within the interval** at this budget (Tilefish's score 48.62%, 95% interval 45.79% to 51.44%). Nothing beyond this opponent, this configuration, CSW24 and this budget follows.

