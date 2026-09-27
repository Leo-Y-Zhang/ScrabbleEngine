# Tilefish

*A championship-style Scrabble engine in one C++ file. Stockfish, but for tiles.*

Everything is in **`tilefish.cpp`**: the lexicon compiler, move generator, evaluation,
Monte-Carlo simulation, endgame and pre-endgame solvers, opponent-rack inference,
self-play training, engine-vs-engine matches, game-record import/export and a terminal
UI. It uses only the C++ standard library.

**Tilefish 2.1** plays tournament lexicons (CSW24, NWL23) straight from `.kwg` files.
Its simulation is about 3.5 times as fast as 2.0's and 8 times as fast as 1.0's, a
little faster than MAGPIE's on the same positions, and every shortcut is checked against
brute force. Under a neutral referee it has played both of the Woogles team's engines:
**MAGPIE**, their open-source C engine, and **Macondo** with the settings of BestBot,
which Woogles calls the best crossword-game engine it knows of. On one core at 1 to 20
seconds a move, Tilefish won 57.1% of 760 CSW24 games against MAGPIE's full search, and
68.5% of 100 NWL23 games. Against BestBot's settings at 20 seconds a move it won
55.0% of 100 games, ahead but not yet by a proven margin. With four threads each at
5 seconds a move it won 51.5% of 200 games against MAGPIE, averaging +16.7 points a
game. What that does and does not prove is set out in
[How strong is it?](#how-strong-is-it).

## Quick start

1. **Download** the zip for your computer from the
   [latest release](https://github.com/Leo-Y-Zhang/ScrabbleEngine/releases/latest)
   (Windows, macOS or Linux) and unzip it.
2. **Get the championship word list** once: double-click `get-lexicon.bat` on Windows,
   or type `sh get-lexicon.sh` in a terminal in that folder on macOS and Linux. This
   downloads CSW24 (Collins Scrabble Words 2024, the World Scrabble Championship's
   list) and its leave values, about 10 MB.
3. **Start** `tilefish` (double-click `tilefish.exe` on Windows, `./tilefish` elsewhere)
   and type `play` to play it at full strength, or `help`.

It prints the word list it loaded when it starts: CSW24 when the files from step 2 are
next to it, otherwise ENABLE, a free list that comes with it. For North American play,
`get-lexicon.bat NWL23` or `sh get-lexicon.sh NWL23` fetches NWL23 instead, used with
`tilefish --lexicon NWL23.kwg`. The ready-made programs run on any recent computer.
Building from source (below) gives one tuned to your processor, which is somewhat faster.

## What's in this folder

| File | What it is |
|---|---|
| `tilefish.cpp` | The whole engine (C++17, no dependencies). |
| `ENABLE.txt` | ENABLE, a free public-domain English word list (~173k words), so the engine runs out of the box. |
| `ENABLE.leaves` | Leave values Tilefish learned for ENABLE by playing itself (400,000 self-play games). |
| `ENABLE.win` | Win-probability model learned from the same games. |
| `CSW24.win`, `NWL23.win` | Win-probability models for CSW24 and NWL23, each fitted on 100,000 self-play games (the word lists themselves are not included). |
| `build.sh`, `build.bat` | One-line builds for Linux/macOS and Windows. |
| `get-lexicon.sh`, `get-lexicon.bat`, `get-lexicon.ps1` | Download CSW24 (or NWL23) and its leave values, checked against known checksums. |
| `LICENSE` | The GNU General Public License, version 3. |
| `tools/referee.py` | Neutral referee for engine-vs-engine matches (its own rules code, paired games, parallel play). |
| `tools/magpie_bot.c`, `tools/build_magpie_bot.sh` | Lets MAGPIE play through the same protocol, for head-to-head matches. |
| `tools/macondo_bot/`, `tools/build_macondo_bot.sh` | The same for Macondo with Woogles' BestBot settings. |
| `.github/workflows/selftest.yml` | Builds with g++, clang and Visual Studio and runs the quick self-test on every push. |
| `.github/workflows/release.yml` | Builds the ready-made Windows, macOS and Linux downloads and publishes a release. |
| `tools/release/QUICKSTART.txt` | The short instructions that come with the downloads. |
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

Then run `./tilefish` (or `tilefish.exe`). It finds its word list and trained data in
the current folder or in its own. Type `help` to list the commands, and run
`sh get-lexicon.sh` (or `get-lexicon.bat`) once for the championship word list.

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

Engine strengths: `play static` (instant), `play sim` (up to 2 s a move),
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
midgame, the pre-endgame solver with one tile in the bag, the exact solver in the
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
$ ./tilefish --quiet --threads 1
ready
cgp 15/15/15/15/15/15/15/5CAT7/15/15/15/15/15/15/15 AEINRST/ 5/0 0
go 5 json
{"position":"15/15/15/15/15/15/15/5CAT7/15/15/15/15/15/15/15 AEINRST/ 5/0 0 lex ENABLE;","method":"simulation","exact":false,"seconds":5.00,"best":"E5 ANTSIER",
 "moves":[{"move":"E5 ANTSIER","score":84,"leave":"","static":84.00,"value":85.09,"win":0.7640,"iterations":23613,"pruned":false},
  {"move":"E5 ANESTRI","score":84,"leave":"","static":84.00,"value":84.96,"win":0.7637,"iterations":23613,"pruned":false}, ...]}
isready
readyok
```

Each `go ... json` answer is one line of JSON: the method used (simulation, pre-endgame,
endgame), whether the result is proven exact, and for every candidate its score,
leave, static equity, simulated/solved value and win probability. This is enough to
drive a live win-probability bar for a broadcast, or a web front-end.

For engine-vs-engine play there is a UCI-style exchange:

```
position cgp 15/15/15/15/15/15/15/5CAT7/15/15/15/15/15/15/15 AEINRST/ 5/0 0
go movetime 1000
bestmove E5 ANESTRI
```

`tools/referee.py` uses it to run matches between any two engines that speak it. The
referee deals the tiles, checks every move against the word list, scores it itself and
plays each deal twice with the seats swapped. With `--prefix`, a deterministic engine
(for example Tilefish with `player static`) plays both seats until the bag is down to
`--prefix-until-bag` tiles. The two engines then take over from identical positions,
which measures pre-endgame or endgame play on its own. For example, Tilefish against
MAGPIE:

```sh
tools/build_magpie_bot.sh ~/MAGPIE     # once, after building MAGPIE (make magpie BUILD=no_pgo_release)
python3 tools/referee.py --lexicon CSW24.txt --games 50 --movetime 1000 --parallel 4 \
    --a "proto:./tilefish --lexicon CSW24.kwg --threads 1 --quiet" --a-name tilefish \
    --b "proto:cd ~/MAGPIE && ./bin/magpie_bot CSW24 1" --b-name magpie
```

Macondo, the engine behind Woogles' BestBot, plays through `tools/macondo_bot` with
BestBot's settings. Its data folder needs `lexica/gaddag/CSW24.kwg` and `CSW24.klv2`
next to Macondo's own `strategy` and `letterdistributions` folders:

```sh
tools/build_macondo_bot.sh ~/macondo   # once; needs Go
python3 tools/referee.py --lexicon CSW24.txt --games 25 --movetime 20000 --parallel 4 \
    --a "proto:./tilefish --lexicon CSW24.kwg --threads 1 --quiet" --a-name tilefish \
    --b "proto:~/macondo/bin/macondo_bot ~/macondo-data CSW24 1" --b-name bestbot
```

The two bots are built against MAGPIE and Macondo, which are GPL-3.0, so their sources
here are GPL-3.0 too. The engine itself contains no code from either.

## Use a tournament dictionary (important)

ENABLE is only a stand-in. Real play uses **CSW** (Collins; WESPA and the World
Championship, which is what Nigel Richards plays) or **NWL** (NASPA, North America).
Those lists are copyrighted, so they cannot be bundled. `get-lexicon.sh` (or
`get-lexicon.bat` on Windows) downloads CSW24, or NWL23 when asked, with its leave values
from the MAGPIE project's public data, pinned to one version and checked against known
checksums. These are the files every match above was played with.

Tilefish reads a word list either as a text file (one word per line) or as a **`.kwg`**
file, the binary lexicon used by wolges, MAGPIE and Macondo, which loads in well under a
second. Leave values next to the lexicon (`CSW24.klv2` or `CSW24.leaves`) and a win
model (`CSW24.win`) are picked up automatically, and a `CSW24.kwg` or `NWL23.kwg` in the
folder is preferred over ENABLE.

```sh
sh get-lexicon.sh                     # CSW24.kwg and CSW24.klv2 (sh get-lexicon.sh NWL23 for NWL23)
./tilefish --lexicon CSW24.kwg        # loads CSW24.klv2 / CSW24.win from the same folder
tilefish> train games=100000 gens=8   # or learn your own leave values
tilefish> train games=100000 gens=1 leaves=0   # refit only the win model
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

### Against MAGPIE

MAGPIE (an open-source C engine that started as a rewrite of Macondo) was built from
source and played through `tools/magpie_bot.c`. That wrapper calls MAGPIE's own
PlayChooser, its full-strength move picker: simulation in the midgame, its pre-endgame
solver when the bag is low, and its endgame solver when the bag is empty.

Both engines used the CSW24 lexicon (NWL23 where stated) and **the same leave values**
(MAGPIE's `.klv2` files), one thread each unless stated otherwise, and the same fixed
time per move. `tools/referee.py` refereed the games, with each deal played twice and
the seats swapped.

| Match (A vs B) | Games | A's win rate | A's spread per game |
|---|---|---|---|
| Tilefish 1.0 (as uploaded) vs MAGPIE, 1 s a move | 100 | 59.0% ± 9.2 | +26.9 ± 18.0 |
| Tilefish 2.0 in progress (fast move generation) vs MAGPIE, 1 s a move | 100 | 59.0% ± 9.2 | +21.4 ± 17.5 |
| Tilefish 2.0 in progress (fast move generation) vs MAGPIE, 5 s a move | 60 | 61.7% ± 11.2 | +21.4 ± 24.9 |
| Tilefish 2.0 before the review fixes vs MAGPIE, 1 s a move | 100 | 58.5% ± 8.8 | +16.8 ± 17.9 |
| Tilefish 2.0 vs MAGPIE, 1 s a move | 100 | 51.5% ± 8.4 | +11.9 ± 14.6 |
| Tilefish 2.1 (this version) vs MAGPIE, 1 s a move | 100 | 57.0% ± 8.4 | +21.6 ± 16.4 |
| **All six one-thread CSW24 matches at 1–5 s pooled** | **560** | **57.5% ± 3.7** | **+19.9 ± 7.2** |
| Tilefish 2.1 vs MAGPIE, 10 s a move | 100 | 58.0% ± 7.6 | +9.5 ± 16.9 |
| Tilefish 2.1 vs MAGPIE, 20 s a move | 100 | 54.0% ± 8.3 | +20.8 ± 15.7 |
| **Tilefish 2.1 at 10 and 20 s pooled** | **200** | **56.0% ± 5.6** | **+15.2 ± 11.5** |
| Tilefish 2.1 vs MAGPIE, **NWL23**, 1 s a move | 100 | **68.5% ± 8.9** | **+27.1 ± 16.0** |
| Tilefish 2.0 before the time fix vs MAGPIE, 5 s a move, 4 threads each | 50 | 51.0% ± 14.6 | +10.8 ± 28.5 |
| Tilefish 2.0 vs MAGPIE, 5 s a move, 4 threads each | 50 | 55.0% ± 13.3 | +19.2 ± 21.5 |
| Tilefish 2.1 before the iteration-cap fix vs MAGPIE, 5 s a move, 4 threads each | 50 | 50.0% ± 12.7 | +19.9 ± 19.3 |
| Tilefish 2.1 (this version) vs MAGPIE, 5 s a move, 4 threads each | 50 | 50.0% ± 11.3 | +16.7 ± 23.3 |
| **All four four-thread matches pooled** | **200** | **51.5% ± 6.4** | **+16.7 ± 11.5** |
| Self-play: Tilefish 2.1 vs 2.0, 1 s a move | 400 | 54.1% ± 4.2 | +8.1 ± 8.1 |
| Control: Tilefish static vs MAGPIE static (no search at all) | 400 | 49.9% ± 3.2 | +1.9 ± 5.8 |
| Diagnostic: MAGPIE's search vs MAGPIE static, 1 s a move | 60 | 48.3% ± 11.0 | −11.3 ± 24.1 |
| Diagnostic: Tilefish 1.0's search vs Tilefish static, 1 s a move | 60 | 60.0% ± 12.8 | +27.9 ± 26.6 |

(± = 95% confidence interval, computed over game pairs.)

**Pre-endgame and endgame on their own.** A static Tilefish played both seats until
the bag held 7 tiles (or 1, or none); then the two engines took over, 1 s a move, and
each position was played twice with the seats swapped. The table shows what each
engine gained from that point on:

| Phase, CSW24, 1 s a move | Games | Tilefish's gain per game | Tilefish win rate |
|---|---|---|---|
| From 7 tiles in the bag (pre-endgame, then endgame) | 200 | **+15.4 ± 4.3** | 56.5% ± 4.1 |
| From 1 tile in the bag (one-tile pre-endgame, then endgame) | 200 | +1.3 ± 0.9 | 50.0% |
| From an empty bag (endgame only) | 200 | +0.3 ± 0.3 | 50.0% |

On the same positions, the build before the review fixes gained +12.3 ± 4.6 from
7 tiles and +0.8 ± 1.4 from 1 tile. Both engines solve almost every endgame exactly,
so the endgame is a draw between them, and with one tile in the bag they are nearly
even. The pre-endgame from 2 to 7 tiles is where Tilefish pulls ahead: its simulations
play every line out to the end. That is worth about 15 points a game here, against
MAGPIE's dedicated pre-endgame solver at the same time per move.

What this shows, and what it does not:

* **The comparison is fair.** With search switched off, the two engines are dead even
  (49.9%). Same leaves, same tiles, same rules, and the referee checked every move.
  The referee sends positions, not game histories, so Tilefish never infers the
  opponent's rack from their last play in these matches (MAGPIE's move picker has no
  such inference).
* **On one core Tilefish is ahead, up to 20 s a move.** Each match on its own is
  borderline, with intervals of ±8–11%. Pooled over the six CSW24 matches at 1–5 s
  (560 games), Tilefish won 57.5% ± 3.7 and averaged +19.9 ± 7.2 points a game. At
  10 and 20 s a move it won 56.0% ± 5.6 of 200 games (+15.2 ± 11.5 points), and over
  all eight one-thread matches 57.1% ± 3.1 of 760 games (+18.7 ± 6.1). On NWL23, the
  first match (100 games) gave 68.5% ± 8.9.
* **With four threads each it is close.** At 5 s a move the build before the time fix
  scored 51.0% ± 14.6 over 50 games while thinking only 1.06 s a move against
  MAGPIE's 4.4 s: its simulation stopped as soon as one candidate was statistically
  ahead, which four threads reach early. It now keeps the closest challenger in play
  and spends the whole budget. On the same deals it then scored 55.0% ± 13.3
  (+19.2 ± 21.5 points a game), thinking 3.9 s a move against MAGPIE's 4.4 s.
  2.1, three and a half times as fast, then ran into a leftover cap of 20,000
  iterations per candidate after about 2 s and stopped there (1.8 s a move against
  MAGPIE's 4.5 s). With the cap lifted it scored 50.0% ± 11.3 (+16.7 ± 23.3 points a
  game) on the same deals, thinking 4.5 s a move against MAGPIE's 4.4 s. Pooled, the
  four four-thread matches (200 games) give 51.5% ± 6.4 in wins and +16.7 ± 11.5
  points a game: level on wins, ahead on spread.
* **More thinking time narrows the lead in wins, not in points.** One thread at 20 s
  a move is as much computing as four threads at 5 s, and it gave the same picture:
  54.0% ± 8.3 in wins, +20.8 ± 15.7 points a game. All 300 games at that budget
  together: 52.3% ± 5.1 and +18.1 ± 9.3. It is not the threads: both engines run
  four threads about four times as fast as one (MAGPIE 4.2 times, Tilefish 3.7 to 4.1
  times on the same position). Tilefish's wins are larger than its losses (at 20 s it
  won by 97 points on average and lost by 69), which fits the two objectives: MAGPIE
  ranks moves by winning chances alone, and Tilefish also counts the margin. In a
  tournament the margin is the tie-break.
* **2.0 is not measurably stronger than 1.0 at 1 s a move.** 1.0 scored 59.0%, the 2.0
  builds 59.0%, 58.5% and 51.5%, all inside each other's noise; 100 games cannot
  resolve a few percent. The difference is in how they get there. 1.0 overran its
  clock (1.10 s a move against MAGPIE's 0.93 s, from the one-tile pre-endgame solver).
  2.0 keeps to it (0.92 s on average, never more than 1.27 s) and does twice the
  simulation work in that time. Played directly against the build before the review
  fixes, 2.0 scored 50.6% ± 4.6 over 400 games at 1 s a move (+4.1 ± 8.2
  points a game): the fixes cost nothing there, and gain nothing measurable either.
  Where they should matter, from 7 tiles in the bag and with four threads, the
  numbers above point the same way but are not yet significant: on identical
  positions from 7 tiles the gain rose by 3.1 ± 3.6 points a game.
* **The one-core lead was not about speed.** Until 2.1, MAGPIE simulated about three
  times as fast as Tilefish. On the same three midgame positions (one thread, 10
  candidates, 2 plies), MAGPIE ran about 21,000 positions a second, 2.0 about 7,100 and
  2.1 about 23,900. The difference was how a short search is used. With about 40
  samples per candidate, MAGPIE ranks moves by noisy win percentages and ends up no
  better than its own static player (48.3%). Tilefish blends the static evaluation into
  the simulation result as a prior, so a short search refines the static choice
  instead of overriding it on noise (60.0%).
* **2.1's faster simulation is worth something.** Against 2.0 at 1 s a move it scored
  54.1% ± 4.2 over 400 games (+8.1 ± 8.1 points a game), at the edge of significance.
  More simulation should count for more with longer thinking time.
* **It does not prove Tilefish is the strongest engine at tournament length.**
  Nothing here went beyond 20 core-seconds a move, and the lead in wins shrinks as
  thinking time grows: about 57% at 1–10 s on one core, 54–55% at 20 s against MAGPIE
  and BestBot's settings, 51.5% with four threads at 5 s. At a minute or more a move,
  where tournament broadcasts would run, either engine could be ahead. The tools to
  settle it are in `tools/`; it needs hundreds of games at that length.

### Against Macondo (Woogles' BestBot)

Woogles.io calls its BestBot the best crossword-game engine it knows of. BestBot runs
Macondo, the Go engine that MAGPIE was rewritten from. It simulates 100 candidate moves
5 plies deep, and uses a pre-endgame solver with one tile in the bag and an exact
endgame solver. `tools/macondo_bot` runs Macondo's own bot code with BestBot's settings
(the bot type and simulation depth of its production entry point), under the same
referee, word list and leave values as the MAGPIE matches.

| Match (A vs B), CSW24 | Games | A's win rate | A's spread per game |
|---|---|---|---|
| Tilefish 2.1 vs Macondo with BestBot's settings, 20 s a move, one thread each | 100 | 55.0% ± 8.0 | +14.8 ± 15.6 |
| Tilefish 2.1 vs MAGPIE and BestBot's settings together, 20 s a move | 200 | 54.5% ± 5.8 | **+17.8 ± 11.0** |

Tilefish is ahead, but 100 games cannot prove a margin of this size. As against MAGPIE,
its wins were larger than its losses (82 against 68 points on average). Against both
engines together at 20 s a move the lead in points is significant and the lead in wins
is not.

* **Macondo is the slower program.** With 5-ply simulations it evaluates about 6,000
  positions a second on one core, against Tilefish's 24,000 with 2 plies, so at 20 s
  a move each of its 100 candidates gets about 200 samples. That is part of what an
  engine match at equal time and hardware measures.
* **BestBot's own budget is larger.** In production it divides its clock by the turns
  it expects to have left, up to three minutes a move, on a cloud function with three
  to four cores. That was not tested here.
* **Two changes let it run in parallel matches.** Its endgame hash table was cut from
  20% to 4% of memory, as for MAGPIE. Its endgame and pre-endgame solvers use the
  thread count they are given instead of every core of the machine.
* **A crash is caught.** Macondo's endgame solver can return an empty line when it is
  stopped very early, and the bot then crashes. The wrapper searches again, so no move
  is forfeited. At 20 s a move this never happened.

### Speed (CSW24, one thread, same machine)

| | Tilefish 1.0 | Tilefish 2.0 | Tilefish 2.1 |
|---|---|---|---|
| Best static play (`benchgen`, 590 midgame positions) | about 480 µs | about 185 µs | about 85 µs |
| Simulation (`benchsim`) | about 3,250 positions/s | about 7,300 positions/s | about 26,000 positions/s |
| Endgame solver (`benchendgame 20 10`, the same 20 endgames) | 131k nodes/s, 14 solved in 10 s | 190k nodes/s, 15 solved | 191k nodes/s, 15 solved |

All three were built with the same compiler and flags and run back to back; 1.0 got the
two benchmark commands it lacked, and its simulation figure comes from its `bench`
command, which runs the same position and settings. Timings on this machine vary by
about ±10% from run to run. 2.1 did not change the endgame solver. Simulation gains
more than a single best-play search because it also shares work between candidates:
with common random numbers every candidate hands the opponent the same rack, and each
candidate's board is set up once per simulation.

### Self-play on ENABLE (from 1.0)

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
* These self-play numbers say nothing about other engines; see
  [Against MAGPIE](#against-magpie) for that. Scrabble also has a lot of luck: even a
  perfect engine loses a good share of games to a world-class human, so the measure is
  always win rate over many games.

## What a championship needs from an engine

Chess championships broadcast Stockfish's evaluation because it is (1) the strongest,
(2) trusted, (3) easy to plug into anything. For Scrabble, today's reference engines are
**Macondo** (behind Woogles' BestBot), its faster successor **MAGPIE** and, historically,
**Quackle**.
Here is where Tilefish stands on each requirement:

| Requirement | Tilefish today |
|---|---|
| Official lexicons (CSW for WESPA/world championship, NWL for NASPA) | Any word list or `.kwg` file, with `.klv2` leaves; `train` learns leave values for it. You supply the licensed list. |
| Correct rules and scoring | Move generator verified against brute force (every word at every position), the fast best-play search against full generation, the endgame solver against plain minimax; `selftest` re-runs all of it. An independent referee checked every move of the MAGPIE matches. |
| Standard formats | GCG game records (read, write, review), CGP positions, KLV/KLV2 leave files. |
| Machine interface for broadcasts and GUIs | `--quiet` mode with one-line JSON analyses (win %, spread, every candidate). |
| Post-game analysis | `review`: every move vs the engine, with win % lost and a win-probability timeline. |
| Proven playing strength | **Partly.** Against MAGPIE: 57.1% ± 3.1 on one core at 1–20 s a move (760 CSW24 games), 68.5% ± 8.9 on NWL23 (100 games), 51.5% ± 6.4 with four threads each at 5 s a move (200 games). Against Macondo with BestBot's settings: 55.0% ± 8.0 at 20 s a move (100 games). Not yet measured at tournament length. |

The last row is the whole job. The plan, in order of expected payoff:

1. **Measure at tournament length.** Up to 20 core-seconds a move has been tried
   (above), and the lead in wins narrows as thinking time grows. Next: a minute or more
   a move over hundreds of game pairs, against MAGPIE and BestBot's settings. The lead
   has to survive there before any "strongest" claim. Deeper simulations (BestBot uses
   5 plies, Tilefish 2) are the first thing to try at that length.
2. **Pre-endgame.** Measured against MAGPIE from 7 tiles in the bag, Tilefish already
   gains 15 points a game at 1 s a move. With one tile in the bag it values every
   candidate against each possible last tile, and it plays simulations out to the end
   below 8 tiles. Missing: passing with one tile in the
   bag. Valuing a pass needs a nested solve from the opponent's side, because they
   don't know which tile is in the bag. Assuming they do makes a pass look never better
   than the best play, so that shortcut was left out.
3. **Speed = strength for simulation.** 2.1 made simulation 3.5 times as fast again, a
   little faster than MAGPIE's. Where its time now goes: bounding every span, about
   half; searching the spans for words, about a quarter; setting up a rack that is not
   in the cache, about a sixth. Cross-check updates after each simulated play take 4%.
4. **Endgame.** 2.0 generates each side's plays once and filters them, and plays out
   greedily at the depth limit. Next: a lexicon pruned to the words the remaining tiles
   can make (fast move generation in the endgame), better move ordering, and handling
   stuck tiles.
5. **Train on the real lexicon.** For CSW24, MAGPIE's leave values are used as is. For
   other lexicons, `train` with at least 500,000 games. On ENABLE, learned leaves were
   worth +39 points a game over no leave knowledge.
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
| 3 | lexicon: DAWG + GADDAG builder (Daciuk's minimisation) or KWG loader, anagram word maps and letter-multiset filters |
| 4-6 | moves, board with incremental cross-checks, notation, scoring, validation |
| 7-8 | leave table (~915k leaves), win-probability model |
| 9 | move generator (GADDAG), static equity, best-play search (span bounds searched best first, word-spelling bounds, word maps, cached rack tables) |
| 10 | game rules and state |
| 11 | Monte-Carlo simulation |
| 12-13 | endgame and pre-endgame solvers |
| 14-15 | inference and the engine front-end |
| 16-17 | autoplay matches and self-play training |
| 18-19 | CGP/GCG and the command-line interface |
| 20-21 | self-tests, benchmarks, `main` |

Run `selftest` after changing the move generator. It checks the generator against a
brute-force generator that tries every word everywhere, the fast best-play search
against full generation (`verifybest`), the endgame solver against plain minimax and
its move lists against full generation (`verifyendgame`). `benchgen`, `benchsim` and
`benchendgame` measure speed on fixed positions.

## What changed in 2.1

* **Simulation 3.5 times as fast** (about 7,300 to about 26,000 positions a second on
  one thread), with the best-play search still exact. Each step was checked against
  full generation (`verifybest`) and a checksum of the simulation results:
  * rack tables (subset frontiers, bounds, the best exchange) are kept for the last four
    racks, since in a simulation every candidate hands the opponent the same rack;
  * every span's bound is computed once, in the bound pass, and spans are searched best
    first across the whole board from one-point buckets;
  * for each span shape, the rack subsets that spell any word are memoized, so the
    search only visits those;
  * span multipliers are updated as a span grows, square score caps come from per-rack
    score classes, anagram-map misses stop early, and each candidate's board is set up
    once per simulation.
* **Time use with several threads**: the `champion` setting stopped at 20,000
  iterations per candidate, which 2.1 reached after about 2 s on four threads. The cap
  is now 1,000,000, and batches and pruning checks grow with the iterations done, so
  their overhead stays small.
* **Passing**: the fast search now passes when a pass has the best equity, as the full
  move list does (verifybest found a lone blank kept with 40 tiles in the bag).
* **Easy to run**: ready-made downloads for Windows, macOS and Linux (built by
  `.github/workflows/release.yml`), `get-lexicon` scripts that fetch CSW24 or NWL23, and
  the word list is also looked for next to the program, so double-clicking it works.
  CI builds with g++, clang and Visual Studio. Tilefish is now GPL-3.0 licensed.
* **Match tools**: `tools/macondo_bot` plays Macondo with Woogles' BestBot settings
  through the referee. It and `tools/magpie_bot.c` are built against GPL-3.0 engines
  and carry GPL-3.0 notices.
* **Benchmarks**: `benchsim [SECS [THREADS [ITERS]]]` runs a fixed number of iterations
  for profiling and prints a checksum of the results. `benchendgame` plays its games
  with the plain generator, so every version is timed on the same endgames.

## What changed in 2.0

* **Lexicons**: `.kwg` files load directly; `.klv2` leaves and `.win` models next to the
  lexicon are found automatically.
* **Best-play search** (the inner loop of simulation), each step checked against full
  generation on thousands of positions:
  * Upper bounds per anchor couple the tiles played with the leave they keep.
  * A letter-multiset filter asks whether any subset of the rack spells a word with
    the tiles a span plays through, which cuts the anchors searched per position
    from 29 to 8.
  * Words then come straight from anagram maps instead of a GADDAG walk (two blanks
    included).
* **Endgame solver**: each side's plays are generated once. At every node they are
  filtered by the squares they depend on, and only plays touching new tiles are
  generated afresh. At the depth limit both sides play greedily to the end instead of
  counting rack values.
* **Engine protocol and match tools**: `position cgp` / `go movetime` / `bestmove`,
  `tools/referee.py`, and `tools/magpie_bot.c` for head-to-head matches.
* **Time control**: every move keeps to its budget and uses it.
  * The one-tile pre-endgame solver first values every candidate with both sides
    playing greedily to the end, then searches the leaders one ply deeper at a time,
    all to the same depth, while time allows. Positions with a blank on the rack used
    to take up to 11 s for a 1 s budget.
  * The simulation keeps the closest challenger in play instead of stopping once one
    candidate is ahead, so a 5 s budget is actually spent. Inference of the opponent's
    rack counts against the same budget.
* **Training**: `train leaves=0` refits only the win model; `CSW24.win` and
  `NWL23.win` ship with it.

## Command reference

Type `help` inside the program. Command-line use:
`./tilefish --lexicon FILE(.txt|.kwg) [--leaves FILE] [--win FILE] [--threads N] [--color] [--quiet] [command; command...]`,
for example `./tilefish "autoplay 1000 sim static threads=8"`. Engine settings are written
`NAME:option=value,...` where NAME is `static`, `static+`, `sim` or `champion` and the
options are `time`, `iters`, `plies`, `cands`, `threads`, `win` (0 = rank by spread),
`tau` (trust in the static evaluation), `playout`, `eg`, `egtime`, `peg`, `pegtime`,
`inf`, `leaves=FILE`, `winmodel=FILE`.

## License

Tilefish is free software: you can redistribute it and/or modify it under the terms of
the GNU General Public License as published by the Free Software Foundation, version 3
or (at your option) any later version. See `LICENSE`. It is the license Stockfish uses,
and MAGPIE and Macondo too. The word lists that `get-lexicon` downloads belong to their
publishers (Collins Scrabble Words: HarperCollins; NWL: NASPA) and are not part of
Tilefish.

---
Scrabble is a trademark of Hasbro, Inc. in the USA and Canada and of Mattel elsewhere.
ENABLE (Enhanced North American Benchmark Lexicon) is in the public domain.
