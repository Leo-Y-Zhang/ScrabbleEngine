# Tilefish

*A championship-style Scrabble engine in one C++ file. Stockfish, but for tiles.*

Everything is in **`tilefish.cpp`**: the lexicon compiler, move generator, evaluation,
Monte-Carlo simulation, endgame and pre-endgame solvers, opponent-rack inference,
self-play training, engine-vs-engine matches, game-record import/export and a terminal
UI. It uses only the C++ standard library.

## What's in this folder

| File | What it is |
|---|---|
| `tilefish.cpp` | The whole engine (C++17, no dependencies). |
| `ENABLE.txt` | ENABLE, a free public-domain English word list (~173k words), so the engine runs out of the box. |
| `ENABLE.leaves` | Leave values Tilefish learned for ENABLE by playing itself (400,000 self-play games). |
| `ENABLE.win` | Win-probability model learned from the same games. |
| `build.sh`, `build.bat` | One-line builds for Linux/macOS and Windows. |
| `README.md` | This file. |

## Build

**Linux / macOS / WSL / MinGW**

```sh
g++ -O3 -march=native -std=c++17 -pthread tilefish.cpp -o tilefish
```

(`clang++` works the same way. On macOS install the command-line tools first with
`xcode-select --install`. If your compiler rejects `-march=native`, leave it out; it is
only a speed-up.)

**Windows (Visual Studio)**: open the *x64 Native Tools Command Prompt* and run

```bat
cl /O2 /std:c++17 /EHsc tilefish.cpp
```

or just run `build.sh` / `build.bat`.

Then run `./tilefish` (or `tilefish.exe`) from this folder. It finds `ENABLE.txt` and its
trained data automatically. Type `help` to list the commands.

## Play against it

```
tilefish> play
```

Enter moves in standard notation:

| You type | Meaning |
|---|---|
| `8D WORD` | row 8, column D, **across** (number first = across) |
| `D8 WORD` | column D, row 8, **down** (letter first = down) |
| `8D WO(R)D` or `8D WO.D` | play through a tile already on the board |
| `8D WoRD` | lower case = blank |
| `exch QVU` / `-QVU` | exchange |
| `pass` | pass |

Other commands during a game: `hint`, `unseen` (tile tracking), `resign`.
After the game, `savegcg mygame.gcg` saves it.

Engine strengths: `play static` (instant), `play sim` (about 2 s a move),
`play champion` (full strength, about 12 s a move). You can fine-tune:
`play champion:time=30` or `play sim:plies=3`.

## Analyse positions

```
tilefish> gcg mygame.gcg 14        # the position before move 14 of a game record
tilefish> cgp 15/15/15/15/15/15/15/5CAT7/15/15/15/15/15/15/15 AEINRST/ 5/0 0
tilefish> rack AEINST?             # change the rack of the player to move
tilefish> gen 20                   # top 20 moves by static equity
tilefish> sim 20                   # simulate for 20 seconds
tilefish> go                       # what the engine would play, and why
tilefish> endgame                  # exact endgame solution (bag empty)
tilefish> peg                      # pre-endgame: exactly one tile in the bag
```

## Game review (like chess.com's)

```
tilefish> review mygame.gcg 1
  #  P  rack      played                     score  engine prefers              win% lost  spread lost  P1 win%
  1  1  ABEGMNT   8D BAGMEN                    28   =                             0.0       0.0     54.8
  2  2  ELNNORX   F8 (G)OX                     27   9G EXON                       4.1      10.3     57.8  ?
  3  1  AAMRTTU   H1 MATURAT(E)                83   =                             0.0       0.0     75.3
  4  2  ELNNOOR   E8 (A)NON                    19   E6 LO(A)NER                   0.5       3.4     80.9
  5  1  ACDESSW   D10 CAW                      30   D10 CAWS                      1.0       3.4     84.5
  ...
 21  1  ACDORTY   O1 D(R)AY                    36   =                             0.0       0.0    100.0
 22  2  HILLPR?   K11 PHI                      29   K11 PRILL                     0.0      33.2    100.0
 23  1  ACORTW?   M11 WRACk                    32   =                             0.0       0.0    100.0
 24  2  ILLR?     14G RILLs                    14   =                             0.0       0.0    100.0
Player 1: 12 moves, engine's choice 10 times, total win% lost 1.1, spread lost 4.7 (0.4 per move)
Player 2: 12 moves, engine's choice 7 times, total win% lost 5.5, spread lost 58.9 (4.9 per move)
```

(Above: a fast self-play game reviewed at one second per move.) For every move whose
rack is recorded in the `.gcg` file, Tilefish analyses the position (simulation in the
midgame, the exhaustive solver with one tile in the bag, the exact solver in the
endgame) and reports how much win probability and spread the move cost.
`?` marks a 2-5% mistake, `??` more than 5%. The last column is player 1's winning chance
with best play from that position, which is the data behind a broadcast win graph.
Games from Woogles and Quackle can be downloaded as `.gcg`; games you play against
Tilefish can be saved with `savegcg`.

CGP (Crossword Game Position) is the "FEN" of Scrabble:
`<15 rows> <rack to move>/<opponent rack> <score to move>/<opponent score> <zero-score turns>`,
digits are runs of empty squares, lower case letters are blanks. The same format is used
by Macondo, Magpie and Woogles, so you can paste positions between engines.

## Driving Tilefish from other programs (GUIs, broadcasts, scripts)

Start it with `--quiet` and talk to it over stdin/stdout, the way chess GUIs talk to
Stockfish:

```
$ ./tilefish --quiet
ready
cgp 15/15/15/15/15/15/15/5CAT7/15/15/15/15/15/15/15 AEINRST/ 5/0 0
go 5 json
{"position":"15/15/15/15/15/15/15/5CAT7/15/15/15/15/15/15/15 AEINRST/ 5/0 0 lex enable1;","method":"simulation","exact":false,"seconds":5.00,"best":"E5 ANTSIER",
 "moves":[{"move":"E5 ANTSIER","score":84,"leave":"","static":84.00,"value":84.95,"win":0.7635,"iterations":10544,"pruned":false},
  {"move":"E5 ANESTRI","score":84,"leave":"","static":84.00,"value":84.80,"win":0.7630,"iterations":10544,"pruned":false}, ...]}
isready
readyok
```

Each `go ... json` answer is one line of JSON: the method used (simulation, pre-endgame,
endgame), whether the result is proven exact, and for every candidate its score,
leave, static equity, simulated/solved value and win probability. This is enough to
drive a live win-probability bar for a broadcast, or a web front-end.

## Use a tournament dictionary (important)

ENABLE is only a stand-in. Real play uses **CSW** (Collins; WESPA and the World
Championship, which is what Nigel Richards plays) or **NWL** (NASPA, North America).
Those lists are copyrighted, so they cannot be bundled. Once you have one as a text
file (one word per line):

```sh
./tilefish --lexicon CSW24.txt
tilefish> train games=100000 gens=8
```

Training plays hundreds of thousands of games against itself and writes
`CSW24.leaves` and `CSW24.win`, which are loaded automatically next time. On a 4-core
laptop that is roughly an hour; more games give better values for rare leaves, so
leaving it overnight with `games=500000` is worthwhile.

Leave files are plain text (`LEAVE value` per line), and Tilefish also reads and writes
the binary **KLV/KLV2** format used by wolges and Macondo (`leaves CSW21.klv2`,
`saveleaves mine.klv2`). So if you already have leave values from those programs you can
load them directly, and settle which set is stronger with a match:
`autoplay 4000 static:leaves=CSW24.leaves static:leaves=CSW21.klv2`.

Why training matters: tile *face values are badly priced*. On ENABLE, self-play found
these leave values (points you should be willing to give up to keep the tiles):

| Tile | Face | Worth | Tile | Face | Worth | Tile | Face | Worth |
|---|---|---|---|---|---|---|---|---|
| **?** | 0 | +25.0 | **H** | 4 | +0.8 | **K** | 5 | -2.0 |
| **S** | 1 | +7.4 | **C** | 3 | +0.7 | **B** | 3 | -3.0 |
| **Z** | 10 | +2.5 | **M** | 3 | +0.4 | **G** | 2 | -3.0 |
| **E** | 1 | +2.2 | **I** | 1 | +0.3 | **F** | 4 | -3.2 |
| **X** | 8 | +2.2 | **D** | 2 | +0.2 | **J** | 8 | -3.4 |
| **R** | 1 | +2.0 | **L** | 1 | -0.5 | **U** | 1 | -3.8 |
| **A** | 1 | +1.3 | **P** | 3 | -0.6 | **W** | 4 | -4.6 |
| **T** | 1 | +1.1 | **O** | 1 | -1.4 | **V** | 4 | -6.0 |
| **N** | 1 | +1.0 | **Y** | 4 | -1.5 | **Q** | 10 | -10.6 |

Some combinations (synergy and duplication matter as much as the tiles themselves):

| Leave | ?S | ?? | AEINST | EINRST | ERS | QU | QI | EEE | UU | VV | OOO | IIII |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Worth | +31.7 | +38.3 | +29.9 | +28.7 | +15.3 | +2.8 | -10.2 | -9.1 | -16.2 | -16.6 | -18.5 | -32.8 |

Note how Z and X (10 and 8 points on their face) are worth only about +2 to keep, the
Q costs over 10 points (in ENABLE `QI` is not a word; in CSW/NWL it is, which is exactly
why you must retrain for your lexicon), and a blank is worth 25 points.

## How strong is it?

Measured with engine-vs-engine matches on this machine (ENABLE lexicon, games played
in pairs with the same tiles and the players swapped, which removes a lot of luck):

| Match (A vs B) | Games | A's win rate | A's spread per game |
|---|---|---|---|
| Trained leaves vs **no leave values** (pure score maximiser), both static | 4,000 | **66.0% ± 1.5** | **+38.9 ± 3.0** |
| Trained leaves vs the built-in hand-made leave model, both static | 6,000 | 51.7% ± 1.3 | +3.7 ± 2.5 |
| static+ (endgame + pre-endgame solvers, 0.5 s) vs static | 800 | 51.9% ± 3.5 | +5.5 ± 7.1 |
| Short simulation (≈100 iterations per candidate) vs static+ | 300 | 51.3% ± 5.7 | −2.2 ± 10.3 |
| Simulation, 6 candidates × 300 iterations, vs static+ | 240 | 53.1% ± 6.3 | +12.1 ± 11.6 |

(± = 95% confidence interval. Every game is played twice with the same tiles and the
players swapped.)

What these numbers do and do not show:

* **Leave knowledge is the big win**: +39 points a game over a pure score maximiser, with
  roughly twice as many bingos. Self-training then beats a sensible hand-written model by
  a small but statistically clear margin.
* **The solvers help where they apply**: the endgame / one-tile pre-endgame solvers
  gained +5.5 points a game in these fast settings. That is not yet statistically
  significant because endgames decide only part of the games.
* **Simulation needs samples.** At about 100 iterations per candidate it was no better
  than the static evaluator (noise cancels its insight). At 300 iterations per candidate
  it gained +12 points a game (just significant on spread). Since then the ranking also
  blends the static evaluation in as a Bayesian prior, so short searches fall back on it
  instead of on noise. The `champion` setting runs thousands of iterations per candidate
  on all cores. It is too slow to measure with hundreds of games here.
* None of this shows that Tilefish beats **Macondo, Magpie or Quackle**, or
  **Nigel Richards**. Only playing them can settle that. Scrabble also has a lot of luck:
  even a perfect engine loses a good share of games to a world-class human, so the
  measure is always win rate over many games.

## What a championship needs from an engine

Chess championships broadcast Stockfish's evaluation because it is (1) the strongest,
(2) trusted, (3) easy to plug into anything. For Scrabble, today's reference engines are
**Macondo** and **Magpie** (behind Woogles' BestBot) and, historically, **Quackle**.
Here is where Tilefish stands on each requirement:

| Requirement | Tilefish today |
|---|---|
| Official lexicons (CSW for WESPA/world championship, NWL for NASPA) | Any word list, plus `train` to learn leave values for it. You supply the licensed list. |
| Correct rules and scoring | Move generator verified against brute force (every word at every position), endgame solver verified against plain minimax, `selftest` re-runs all of it. |
| Standard formats | GCG game records (read, write, review), CGP positions, KLV/KLV2 leave files. |
| Machine interface for broadcasts and GUIs | `--quiet` mode with one-line JSON analyses (win %, spread, every candidate). |
| Post-game analysis | `review`: every move vs the engine, with win % lost and a win-probability timeline. |
| Proven playing strength | **Not yet established.** It must beat Macondo/Magpie over thousands of games on CSW. |

The last row is the whole job. The plan, in order of expected payoff:

1. **Train on the real lexicon.** `train` with CSW24 (or NWL2023) and at least 500,000
   games. On ENABLE, learned leaves were worth +39 points a game over no leave knowledge.
   Leaves learned for one dictionary are wrong for another (QI is a word in CSW and
   NWL but not in ENABLE, which changes what a Q is worth).
2. **Measure against the champions.** Install Macondo or Magpie, give both engines the
   same CGP positions or whole Woogles games, and compare choices at long time controls.
   Then play full matches. Test every change with `autoplay` against the previous
   version, and keep it only if it wins with statistical confidence. Small edges need
   thousands of games; the same-tiles pairing Tilefish uses helps.
3. **Speed = strength for simulation.** The experiments above show short simulations
   barely beat the static evaluator. Simulation needs hundreds of iterations per
   candidate to pay off, so iterations per second matter. `MoveGen::rec` dominates the
   profile. Ideas: tighter shadow bounds, pruning inside an anchor once the bound is
   beaten, cheaper leave lookups, then more cores.
4. **Pre-endgame with 2 to 7 tiles in the bag.** Tilefish solves exactly one tile in the
   bag exhaustively and plays simulations out to the end below 8 tiles. Many games are
   decided here. Next: exhaustive two-tile solving (enumerate the pairs you might draw).
5. **Endgame.** Exact, and multi-threaded, but slow when a blank or a stuck tile gives
   thousands of options. Ideas: reuse move lists between sibling nodes, better
   ordering, a better estimate at the depth limit.
6. **A learned evaluation (the AlphaZero/NNUE idea).** Top Scrabble engines still rely
   on leave tables plus simulation. A small neural network over board + rack + unseen
   tiles, trained on self-play outcomes, could judge board openness, hot spots and
   defence directly, and would make every simulation iteration smarter.
7. **Opponent modelling.** Inference assumes the opponent evaluates like Tilefish.
   Against humans, fit `InferenceParams` (tau, floor) to real tournament games.

## Your notes, and where they ended up

| Note | In the engine |
|---|---|
| "Deviance is poorly priced points of letters that can be filtered from noise" | Learned leave values (§7, §17): self-play measures what tiles are really worth. |
| "There are only endgames - no Scholar's Mate" | Exact endgame solver (§12) plus the pre-endgame solver (§13). |
| "Use Set Dictionary for English lexicon" | Any word list compiles to a DAWG + GADDAG at start-up (§3). |
| "Use Stockfish for positional analysis" | Stockfish-style search tools: iterative deepening, transposition table, PVS, Lazy SMP threads (§12). |
| "Search only needs to search through possible ones" | The GADDAG only ever builds legal words, and shadow bounds skip anchors that can't matter (§9). |
| "255 square board with bias" | The 15x15 = 225-square board with premium squares (§2). |
| "Roster is like captured pieces in Crazyhouse" | Racks as multisets (`Rack`), exchanges and passes are real moves (§9). |
| "Play for win vs best move" | Simulation ranks by win probability, with spread as a tie-breaker; `win=0` switches to pure spread (§11). |
| "Randomness of the pot can be minimised" | Tile tracking, common random numbers across candidates, inference of the opponent's rack (§11, §14). |
| "Game score and score need + number of tiles" | The win model is a function of spread and unseen tiles (§8). |
| "Win percentage + (potential for win percent)" | Each candidate gets a simulated win % (§11). |
| "Torch + Lila + AlphaZero" | Self-play training loop (§17); the neural-net step is roadmap item 6. |
| "4 person engine" | Tournament Scrabble is two players, which is what the search assumes. A multiplayer mode would need a different (non-zero-sum) search. |
| "Starting gets starting letterpoint until end" / first-move advantage | Measured by the win model: the player to move at the start wins about 55% of self-play games. |

## How the code is organised

`tilefish.cpp` is split into numbered sections:

| § | Section |
|---|---|
| 1-2 | utilities, tiles, board layout, racks |
| 3 | lexicon: DAWG + GADDAG builder (Daciuk's minimisation), anagram index |
| 4-6 | moves, board with incremental cross-checks, notation, scoring, validation |
| 7-8 | leave table (~915k leaves), win-probability model |
| 9 | move generator (GADDAG), static equity, shadow pruning |
| 10 | game rules and state |
| 11 | Monte-Carlo simulation |
| 12-13 | endgame and pre-endgame solvers |
| 14-15 | inference and the engine front-end |
| 16-17 | autoplay matches and self-play training |
| 18-19 | CGP/GCG and the command-line interface |
| 20-21 | self-tests, benchmarks, `main` |

Run `selftest` after changing the move generator. It checks the generator against a
brute-force generator that tries every word everywhere, and the endgame solver against
plain minimax.

## Command reference

Type `help` inside the program. Command-line use:
`./tilefish --lexicon FILE [--leaves FILE] [--win FILE] [--threads N] [--color] [--quiet] [command; command...]`,
for example `./tilefish "autoplay 1000 sim static threads=8"`. Engine settings are written
`NAME:option=value,...` where NAME is `static`, `static+`, `sim` or `champion` and the
options are `time`, `iters`, `plies`, `cands`, `threads`, `win` (0 = rank by spread),
`tau` (trust in the static evaluation), `playout`, `eg`, `egtime`, `peg`, `pegtime`,
`inf`, `leaves=FILE`, `winmodel=FILE`.

---
Scrabble is a trademark of Hasbro, Inc. in the USA and Canada and of Mattel elsewhere.
ENABLE (Enhanced North American Benchmark Lexicon) is in the public domain.
