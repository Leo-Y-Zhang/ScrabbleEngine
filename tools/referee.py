#!/usr/bin/env python3
"""Neutral referee for engine-vs-engine Scrabble matches.

The referee owns the game: it shuffles the bag, deals racks, checks every move
against the word list, scores it itself and applies the end-of-game rules.
Engines only see what a player at the board would see (their own rack, the
board, the scores; the opponent's rack only once the bag is empty) and answer
with a move.  Games are played in pairs with the same bag order and the
players swapped, which removes much of the luck of the draw.

Engine protocol (one line each way, like UCI for chess):
    -> position cgp <CGP>          CGP of the position, rack of the player to move first
    -> go movetime <ms>
    <- bestmove <move>             8D WORD (across), D8 WORD (down), WO(R)D or WO.D for
                                   tiles already on the board, lower case = blank,
                                   exch ABC, pass
Before "bestmove" an engine may print "info <JSON>" lines (its search statistics); the
last one is stored with the move in the log.  Every game in the log carries its full
record: for each move the position the mover saw (CGP, opponent's rack hidden while the
bag has tiles), the move, its score, the time it took, the CPU time the engine's
processes used (Linux), and the engine's info.
With --a-history / --b-history the engine is also sent, before each position,
    -> history <GCG>               the game so far as the mover knows it: GCG lines joined
                                   by " | ", the mover's own racks, and for the opponent's
                                   plays only the tiles they played
so it can infer the opponent's rack.  The mover never saw the tiles of the opponent's
exchanges and passes; the GCG names placeholder tiles from the mover's unseen pool for
them, which only keeps the replay's tile count right (the engine checks the replayed
position against the CGP).
With --prefix, a (deterministic) engine plays both seats until the bag holds
--prefix-until-bag tiles; A and B then take over from identical positions, which
measures pre-endgame and endgame play on their own.
With --positions FILE, each pair starts from a recorded position extracted by
tools/positions.py.  Pair k uses zero-based line k (starting at --first-pair), with
the same board, racks, scores and shuffled remaining bag in both games.  A holds
the mover's rack in the first game, B in the second.  This also measures points
gained after the handover.  --prefix and --positions cannot be combined.
History before a recorded start position is unknown: --a-history / --b-history
send only moves played after that position, with no earlier history lines.

Engine specs on the command line:
    proto:<shell command>          speaks the protocol above (tilefish --quiet does)
    legacy:<shell command>         Tilefish 1.0: `cgp ...` then `go <secs> json`

Example:
    referee.py --lexicon CSW24.txt --games 100 --movetime 1000 --parallel 4 \\
        --a "proto:./tilefish --lexicon CSW24.txt --quiet" --a-name tilefish \\
        --b "proto:cd ~/MAGPIE && ./bin/magpie_bot CSW24 1" --b-name magpie
"""

import argparse
import hashlib
import json
import math
import multiprocessing as mp
import os
import platform
import random
import signal
import subprocess
import sys
import threading
import time

N = 15
CLK_TCK = os.sysconf("SC_CLK_TCK") if hasattr(os, "sysconf") and "SC_CLK_TCK" in getattr(os, "sysconf_names", {}) else 100


def session_cpu(sid):
    """CPU seconds (user + system) used so far by the live processes of session `sid`
    (an engine and everything it started).  None where /proc is not available."""
    if not os.path.isdir("/proc"):
        return None
    total = 0
    for d in os.listdir("/proc"):
        if not d.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % d) as f:
                st = f.read()
        except OSError:
            continue
        rest = st[st.rfind(")") + 2:].split()
        # fields after the command: state ppid pgrp session ... utime(12) stime(13)
        if int(rest[3]) == sid:
            total += int(rest[11]) + int(rest[12])
    return total / float(CLK_TCK)
CENTER = (7, 7)
LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
DIST = dict(zip("?" + LETTERS, [2, 9, 2, 2, 4, 12, 2, 3, 2, 9, 1, 1, 4, 2, 6, 8, 2, 1, 6, 4, 6, 4, 2, 2, 1, 2, 1]))
SCORE = dict(zip("?" + LETTERS, [0, 1, 3, 3, 2, 1, 4, 2, 4, 1, 8, 5, 1, 3, 1, 1, 3, 10, 1, 1, 1, 1, 4, 4, 8, 4, 10]))
LAYOUT = [
    "=  '   =   '  =",
    " -   \"   \"   - ",
    "  -   ' '   -  ",
    "'  -   '   -  '",
    "    -     -    ",
    " \"   \"   \"   \" ",
    "  '   ' '   '  ",
    "=  '   -   '  =",
    "  '   ' '   '  ",
    " \"   \"   \"   \" ",
    "    -     -    ",
    "'  -   '   -  '",
    "  -   ' '   -  ",
    " -   \"   \"   - ",
    "=  '   =   '  =",
]
LM = [[{"'": 2, '"': 3}.get(ch, 1) for ch in row] for row in LAYOUT]
WM = [[{"-": 2, "=": 3}.get(ch, 1) for ch in row] for row in LAYOUT]
RACK = 7
BINGO = 50


def tile_value(t):
    """Board tiles: upper case = real tile, lower case = blank."""
    return 0 if t.islower() else SCORE[t]


def rack_value(rack):
    return sum(SCORE[t] for t in rack)


def rack_str(rack):
    return "".join(sorted(t for t in rack if t != "?")) + "?" * rack.count("?")


class IllegalMove(Exception):
    pass


def parse_board(rows):
    """Expand the board rows of a CGP, preserving lower-case blanks."""
    board = []
    for row in rows.split("/"):
        cells = []
        i = 0
        while i < len(row):
            if row[i].isdigit():
                j = i + 1
                while j < len(row) and row[j].isdigit():
                    j += 1
                run = int(row[i:j])
                if not 1 <= run <= N:
                    raise ValueError("invalid empty run in CGP board")
                cells.extend([None] * run)
                i = j
            elif row[i].upper() in LETTERS:
                cells.append(row[i])
                i += 1
            else:
                raise ValueError("invalid tile in CGP board")
        if len(cells) != N:
            raise ValueError("CGP board row must have 15 squares")
        board.append(cells)
    if len(board) != N:
        raise ValueError("CGP board must have 15 rows")
    return board


class Game:
    def __init__(self, words, seed, position=None):
        self.words = words
        self.rng = random.Random(seed)
        bag = list(position["bag"]) if position is not None else [t for t, k in DIST.items() for _ in range(k)]
        self.rng.shuffle(bag)
        self.bag = bag  # draw from the end
        self.board = parse_board(position["board"]) if position is not None else [[None] * N for _ in range(N)]
        self.racks = [list(r) for r in position["racks"]] if position is not None else [[], []]
        self.scores = list(position["scores"]) if position is not None else [0, 0]
        self.turn = 0
        self.zeros = position["zeros"] if position is not None else 0
        self.over = False
        self.moves = []  # (player, rack_before, text, score)
        self.initial_scores = list(self.scores)
        self.initial_tiles = ["?" if t.islower() else t for row in self.board for t in row if t is not None]
        if position is None:
            for p in (0, 1):
                self.draw(p)

    def draw(self, p):
        while len(self.racks[p]) < RACK and self.bag:
            self.racks[p].append(self.bag.pop())

    def empty(self):
        return all(self.board[r][c] is None for r in range(N) for c in range(N))

    def cgp(self, show_opp):
        rows = []
        for r in range(N):
            s, run = "", 0
            for c in range(N):
                t = self.board[r][c]
                if t is None:
                    run += 1
                else:
                    if run:
                        s += str(run)
                    run = 0
                    s += t
            if run:
                s += str(run)
            rows.append(s)
        p = self.turn
        opp = rack_str(self.racks[1 - p]) if show_opp else ""
        return "%s %s/%s %d/%d %d" % ("/".join(rows), rack_str(self.racks[p]), opp,
                                      self.scores[p], self.scores[1 - p], self.zeros)

    # ---- move parsing / validation / scoring ------------------------------------
    def parse(self, text):
        tok = text.split()
        if not tok:
            raise IllegalMove("empty move")
        t0 = tok[0].lower()
        if t0 == "pass":
            return ("pass",)
        if t0 in ("exch", "exchange", "-") or (t0.startswith("-") and len(t0) > 1):
            tiles = tok[1] if t0 in ("exch", "exchange", "-") else tok[0][1:]
            tiles = tiles.upper().replace("_", "?")
            return ("exch", tiles)
        if len(tok) < 2:
            raise IllegalMove("bad move %r" % text)
        coord, word = tok[0].upper(), tok[1]
        if coord[0].isdigit():
            i = 0
            while i < len(coord) and coord[i].isdigit():
                i += 1
            row, col, d = int(coord[:i]) - 1, ord(coord[i]) - 65, 0
            if i + 1 != len(coord):
                raise IllegalMove("bad coordinate %r" % coord)
        else:
            col, row, d = ord(coord[0]) - 65, int(coord[1:]) - 1, 1
        if not (0 <= row < N and 0 <= col < N):
            raise IllegalMove("bad coordinate %r" % coord)
        cells, through = [], False
        for ch in word:
            if ch == "(":
                through = True
            elif ch == ")":
                through = False
            else:
                cells.append((ch, through or ch == "."))
        return ("place", row, col, d, cells)

    def check_and_score(self, mv, rack):
        """Validates a parsed move for the player to move; returns (score, placed, used)."""
        kind = mv[0]
        if kind == "pass":
            return 0, [], []
        if kind == "exch":
            tiles = list(mv[1])
            if len(self.bag) < RACK:
                raise IllegalMove("exchange with fewer than 7 tiles in the bag")
            left = list(rack)
            for t in tiles:
                if t not in left:
                    raise IllegalMove("exchange of tiles not on the rack")
                left.remove(t)
            if not tiles:
                raise IllegalMove("empty exchange")
            return 0, [], tiles
        _, row, col, d, cells = mv
        dr, dc = (0, 1) if d == 0 else (1, 0)
        er, ec = row + dr * (len(cells) - 1), col + dc * (len(cells) - 1)
        if er >= N or ec >= N:
            raise IllegalMove("word runs off the board")
        placed, used = [], []
        left = list(rack)
        for i, (ch, through) in enumerate(cells):
            r, c = row + dr * i, col + dc * i
            on = self.board[r][c]
            if through or (on is not None and ch.upper() == on.upper()):
                if on is None:
                    raise IllegalMove("played-through square %s%d is empty" % (chr(65 + c), r + 1))
                if ch != "." and ch.upper() != on.upper():
                    raise IllegalMove("letter mismatch at %s%d" % (chr(65 + c), r + 1))
                continue
            if on is not None:
                raise IllegalMove("square %s%d is occupied" % (chr(65 + c), r + 1))
            if not ch.isalpha():
                raise IllegalMove("bad letter %r" % ch)
            need = "?" if ch.islower() else ch
            if need not in left:
                raise IllegalMove("tile %s not on the rack" % need)
            left.remove(need)
            used.append(need)
            placed.append((r, c, ch))
        if not placed:
            raise IllegalMove("no tiles placed")
        pr, pc = row - dr, col - dc
        if pr >= 0 and pc >= 0 and self.board[pr][pc] is not None:
            raise IllegalMove("word does not include the tile before it")
        nr, nc = er + dr, ec + dc
        if nr < N and nc < N and self.board[nr][nc] is not None:
            raise IllegalMove("word does not include the tile after it")
        pos = {(r, c): ch for r, c, ch in placed}

        def at(r, c):
            if (r, c) in pos:
                return pos[(r, c)]
            return self.board[r][c]

        if self.empty():
            if CENTER not in pos:
                raise IllegalMove("first play must cover the centre")
        else:
            touch = len(placed) < len(cells)
            for r, c, _ in placed:
                for rr, cc in ((r - 1, c), (r + 1, c), (r, c - 1), (r, c + 1)):
                    if 0 <= rr < N and 0 <= cc < N and self.board[rr][cc] is not None:
                        touch = True
            if not touch:
                raise IllegalMove("play is not connected")
        words = []
        total = 0

        def run(r, c, ddr, ddc):
            while r - ddr >= 0 and c - ddc >= 0 and at(r - ddr, c - ddc) is not None:
                r, c = r - ddr, c - ddc
            sq = []
            while r < N and c < N and at(r, c) is not None:
                sq.append((r, c))
                r, c = r + ddr, c + ddc
            return sq

        def score_run(sq):
            s, wm = 0, 1
            for r, c in sq:
                t = at(r, c)
                if (r, c) in pos:
                    s += tile_value(t) * LM[r][c]
                    wm *= WM[r][c]
                else:
                    s += tile_value(t)
            return s * wm

        main = run(placed[0][0], placed[0][1], dr, dc)
        if len(main) >= 2:
            words.append(main)
            total += score_run(main)
        for r, c, _ in placed:
            cross = run(r, c, dc, dr)
            if len(cross) >= 2:
                words.append(cross)
                total += score_run(cross)
        if not words:
            raise IllegalMove("a single tile must form a word")
        for sq in words:
            w = "".join(at(r, c).upper() for r, c in sq)
            if w not in self.words:
                raise IllegalMove("phony: " + w)
        if len(placed) == RACK:
            total += BINGO
        return total, placed, used

    def apply(self, text):
        """Applies a move for the player to move.  Returns (score, error or None)."""
        p = self.turn
        rack = self.racks[p]
        err = None
        try:
            mv = self.parse(text)
            score, placed, used = self.check_and_score(mv, rack)
        except (IllegalMove, ValueError, IndexError) as e:
            err = "%s (%r)" % (e, text)
            mv, score, placed, used = ("pass",), 0, [], []
            text = "pass"
        before = rack_str(rack)
        self.last_tiles = len(placed)
        if mv[0] == "place":
            for r, c, ch in placed:
                self.board[r][c] = ch
            for t in used:
                rack.remove(t)
            self.scores[p] += score
            self.draw(p)
        elif mv[0] == "exch":
            for t in used:
                rack.remove(t)
            self.draw(p)
            for t in used:
                self.bag.insert(self.rng.randrange(len(self.bag) + 1), t)
        self.moves.append((p, before, mv, text, score))
        self.zeros = 0 if (mv[0] == "place" and score > 0) else self.zeros + 1
        if mv[0] == "place" and not rack and not self.bag:
            bonus = 2 * rack_value(self.racks[1 - p])
            self.scores[p] += bonus
            self.moves.append((p, None, ("end",), "(%s)" % rack_str(self.racks[1 - p]), bonus))
            self.over = True
        elif self.zeros >= 6:
            for q in (0, 1):
                v = rack_value(self.racks[q])
                self.scores[q] -= v
                self.moves.append((q, None, ("end",), "(%s)" % rack_str(self.racks[q]), -v))
            self.over = True
        self.turn = 1 - p
        return score, err

    def mover_gcg(self, lexicon):
        """The game so far as the player to move knows it, as GCG lines."""
        me = self.turn
        names = ["p1", "p2"]
        out = ["#character-encoding UTF-8", "#player1 p1 p1", "#player2 p2 p2"]
        if lexicon:
            out.append("#lexicon %s" % lexicon)
        tot = list(self.initial_scores)
        on_board = list(self.initial_tiles)
        mine = [m[1] for m in self.moves if m[0] == me and m[2][0] != "end"] + [rack_str(self.racks[me])]

        def placeholder(n, k):
            # n tiles the mover could not see at move k: unseen = all - board - own rack
            pool = [t for t, c in DIST.items() for _ in range(c)]
            own = mine[sum(1 for m in self.moves[:k] if m[0] == me and m[2][0] != "end")]
            for t in on_board + list(own):
                pool.remove(t)
            return "".join(sorted(pool)[:n])

        for k, (p, before, mv, text, score) in enumerate(self.moves):
            if mv[0] == "end":
                return None
            tot[p] += score
            if mv[0] == "place":
                _, row, col, d, cells = mv
                w = "".join("." if through else ch for ch, through in cells)
                if p == me:
                    rack = before
                else:  # only the tiles they put down
                    rack = rack_str(["?" if ch.islower() else ch for ch, through in cells if not through])
                out.append(">%s: %s %s %s +%d %d" % (names[p], rack, text.split()[0].upper(), w, score, tot[p]))
                on_board += ["?" if ch.islower() else ch for ch, through in cells if not through]
            elif p != me:
                n = len(mv[1]) if mv[0] == "exch" else 1
                fake = placeholder(n, k)
                if len(fake) < n:
                    return None
                if mv[0] == "exch":
                    out.append(">%s: %s -%s +0 %d" % (names[p], fake, fake, tot[p]))
                else:
                    out.append(">%s: %s - +0 %d" % (names[p], fake, tot[p]))
            elif mv[0] == "exch":
                out.append(">%s: %s -%s +0 %d" % (names[p], before, mv[1], tot[p]))
            else:
                out.append(">%s: %s - +0 %d" % (names[p], before, tot[p]))
        out.append("#rack%d %s" % (me + 1, rack_str(self.racks[me])))
        return " | ".join(out)

    def gcg(self, names):
        out = ["#character-encoding UTF-8", "#player1 %s %s" % (names[0], names[0]),
               "#player2 %s %s" % (names[1], names[1])]
        tot = list(self.initial_scores)
        for p, before, mv, text, score in self.moves:
            tot[p] += score
            if mv[0] == "end":
                out.append(">%s: %s %+d %d" % (names[p], text, score, tot[p]))
            elif mv[0] == "pass":
                out.append(">%s: %s - +0 %d" % (names[p], before, tot[p]))
            elif mv[0] == "exch":
                out.append(">%s: %s -%s +0 %d" % (names[p], before, mv[1], tot[p]))
            else:
                tok = text.split()
                word = tok[1].replace("(", "").replace(")", "")
                # GCG marks played-through tiles with '.'
                _, row, col, d, cells = mv
                dr, dc = (0, 1) if d == 0 else (1, 0)
                w = ""
                for i, (ch, through) in enumerate(cells):
                    w += "." if through else ch
                out.append(">%s: %s %s %s +%d %d" % (names[p], before, tok[0].upper(), w, score, tot[p]))
        return "\n".join(out) + "\n"


class Engine:
    def __init__(self, spec, name, init="", history=False):
        self.name = name
        self.history = history  # send "history <GCG>" before each position
        self.init = [c.strip() for c in init.split(";") if c.strip()]
        kind, _, cmd = spec.partition(":")
        if kind not in ("proto", "legacy"):
            raise SystemExit("engine spec must start with proto: or legacy:")
        self.kind, self.cmd = kind, cmd
        self.proc = None
        self.info = None  # the last "info" line before a bestmove
        self.startup_s = None
        self.start()

    STARTUP_SECONDS = 600  # loading a lexicon and its data; --startup-timeout

    def start(self):
        t0 = time.time()
        # Its own process group, so a watchdog can stop a shell command and its children.
        self.proc = subprocess.Popen(self.cmd, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1,
                                     start_new_session=(os.name == "posix"))
        for c in self.init:
            self.send(c)
        # Wait until the engine has loaded its data, so start-up is not charged to a move.
        self.send("isready")

        def ready():
            while self.readline() != "readyok":
                pass
        self.deadline(Engine.STARTUP_SECONDS, ready)
        self.startup_s = time.time() - t0

    def cpu(self):
        return session_cpu(self.proc.pid) if os.name == "posix" else None

    def kill(self):
        try:
            if os.name == "posix":
                os.killpg(self.proc.pid, signal.SIGKILL)
            else:  # the shell and everything it started
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(self.proc.pid)], capture_output=True)
        except Exception:
            pass

    def deadline(self, seconds, fn):
        # An engine that stops answering is killed; its pipe then closes and readline
        # raises, which the game loop records as a crash (the move counts as a pass).
        t = threading.Timer(seconds, self.kill)
        t.daemon = True
        t.start()
        try:
            return fn()
        finally:
            t.cancel()

    def send(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def readline(self):
        line = self.proc.stdout.readline()
        if not line:
            raise EOFError("engine %s exited" % self.name)
        return line.strip()

    def best(self, cgp, ms, hist=None, extra=""):
        return self.deadline(3 * ms / 1000.0 + 60, lambda: self._best(cgp, ms, hist, extra))

    def _best(self, cgp, ms, hist=None, extra=""):
        self.info = None
        if self.kind == "proto":
            if self.history and hist:
                self.send("history " + hist)
            self.send("position cgp " + cgp)
            self.send("go movetime %d%s" % (ms, extra))
            while True:
                line = self.readline()
                if line.startswith("info "):
                    try:
                        self.info = json.loads(line[5:])
                    except ValueError:
                        self.info = line[5:]
                elif line.startswith("bestmove "):
                    return line[9:].strip()
        self.send("cgp " + cgp)
        self.send("go %.3f json" % (ms / 1000.0))
        while True:
            line = self.readline()
            if line.startswith("{"):
                return json.loads(line)["best"]

    def close(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=5)
        except Exception:
            self.proc.kill()


WORKER = {}


def load_words(path):
    with open(path) as f:
        for line in f:
            w = line.split()
            if w and w[0].isalpha():
                WORDS.add(w[0].upper())


def load_positions(path, first_pair, games):
    """Load and check recorded starts before launching any engines."""
    if first_pair < 0:
        raise ValueError("--first-pair must be nonnegative")
    with open(path) as f:
        positions = [json.loads(line) for line in f]
    needed = first_pair + games
    if len(positions) < needed:
        raise ValueError("file has %d positions; need %d for --first-pair %d and --games %d" % (
            len(positions), needed, first_pair, games))
    full = sorted(t for t, n in DIST.items() for _ in range(n))
    for k in range(first_pair, needed):
        try:
            g = Game(set(), 0, positions[k])
            if len(g.racks) != 2 or len(g.scores) != 2 or any(len(r) > RACK for r in g.racks):
                raise ValueError("expected two racks and two scores")
            if sorted(g.initial_tiles + g.racks[0] + g.racks[1] + g.bag) != full:
                raise ValueError("tiles do not match the distribution")
            if not all(isinstance(s, int) for s in g.scores) or not isinstance(g.zeros, int) or not 0 <= g.zeros < 6:
                raise ValueError("invalid scores or zero-score-turn count")
        except (ValueError, KeyError, TypeError) as ex:
            raise ValueError("line %d: %s" % (k + 1, ex)) from ex
    return positions


def worker_init(args):
    if not WORDS:  # processes started with "spawn" (Windows, macOS) do not inherit it
        load_words(args.lexicon)
    WORKER["args"] = args
    WORKER["engines"] = [Engine(args.a, args.a_name, args.a_init, args.a_history),
                         Engine(args.b, args.b_name, args.b_init, args.b_history)]
    WORKER["prefix"] = Engine(args.prefix, "prefix", args.prefix_init) if args.prefix else None


def play_game(pair, a_first):
    args = WORKER["args"]
    eng = WORKER["engines"]
    position = args.position_data[pair] if args.positions else None
    g = Game(WORDS, args.seed * 1000003 + pair, position)
    # seat 0 moves first
    seat = [0, 1] if a_first else [1, 0]  # seat -> engine index (0 = A)
    errors, spent, maxt = [], [0.0, 0.0], [0.0, 0.0]
    cpu = [0.0, 0.0]
    record = []  # one entry a move: what the mover saw, what it played, how long it took
    bingos = [0, 0]
    turns = 0
    handoff = None  # scores of A and B at the start position or prefix handover
    prefix = WORKER["prefix"]
    while not g.over and turns < 150:
        cgp = g.cgp(show_opp=not g.bag)
        if prefix is not None and len(g.bag) > args.prefix_until_bag:
            # Opening and middle game played identically in both games of a pair.
            try:
                text = prefix.best(cgp, args.movetime)
            except Exception as ex:
                errors.append("prefix crashed: %s" % ex)
                prefix.kill()
                prefix.start()
                text = "pass"
            bag_before = len(g.bag)
            n_moves = len(g.moves)
            score, err = g.apply(text)
            if err:
                errors.append("prefix: %s  [%s]" % (err, cgp))
            record.append(dict(p="prefix", bag=bag_before, cgp=cgp, mv=g.moves[n_moves][3], sc=score))
            turns += 1
            continue
        if handoff is None:
            handoff = (g.scores[seat.index(0)], g.scores[seat.index(1)], len(g.bag))
        e = seat[g.turn]
        c0 = eng[e].cpu()
        t0 = time.time()
        hist = g.mover_gcg(args.history_lexicon) if eng[e].history else None
        try:
            text = eng[e].best(cgp, args.movetime, hist)
        except Exception as ex:  # crashed engine: restart, count as pass
            errors.append("%s crashed: %s" % (eng[e].name, ex))
            eng[e].kill()
            eng[e].start()
            text = "pass"
        dt = time.time() - t0
        c1 = eng[e].cpu()
        used = (c1 - c0) if c0 is not None and c1 is not None and c1 >= c0 else None
        if used is not None:
            cpu[e] += used
        if dt > 3 * args.movetime / 1000.0 + 0.2:
            errors.append("%s slow: %.2fs, bag %d  [%s]" % (eng[e].name, dt, len(g.bag), cgp))
        spent[e] += dt
        maxt[e] = max(maxt[e], dt)
        bag_before = len(g.bag)
        n_moves = len(g.moves)
        score, err = g.apply(text)
        if err:
            errors.append("%s: %s  [%s]" % (eng[e].name, err, cgp))
        rec = dict(p=e, bag=bag_before, cgp=cgp, mv=g.moves[n_moves][3], sc=score, t=round(dt, 3))
        if used is not None:
            rec["cpu"] = round(used, 3)
        if err:
            rec["err"] = err
        if eng[e].info is not None:
            rec["info"] = eng[e].info
        record.append(rec)
        if g.last_tiles == RACK:
            bingos[e] += 1
        turns += 1
    sa = g.scores[seat.index(0)]
    sb = g.scores[seat.index(1)]
    names = [eng[seat[0]].name, eng[seat[1]].name]
    if args.gcg_dir:
        os.makedirs(args.gcg_dir, exist_ok=True)
        with open(os.path.join(args.gcg_dir, "game%05d%s.gcg" % (pair, "a" if a_first else "b")), "w") as f:
            f.write(g.gcg(names))
    if handoff is None:
        handoff = (0, 0, len(g.bag)) if prefix is None else (sa, sb, 0)
    for p, before, mv, text, score in g.moves:
        if mv[0] == "end":
            record.append(dict(p=seat[p], end=text, sc=score))
    return dict(pair=pair, a_first=a_first, sa=sa, sb=sb, ha=handoff[0], hb=handoff[1], hbag=handoff[2], errors=errors,
                spent=spent, maxt=maxt, cpu=[round(c, 2) for c in cpu],
                startup=[round(x.startup_s or 0, 2) for x in eng],
                moves=[len([m for m in g.moves if m[2][0] != "end" and seat[m[0]] == k]) for k in (0, 1)],
                bingos=bingos, record=record)


def run_pair(pair):
    if WORKER["args"].single:
        return [play_game(pair, pair % 2 == 0)]
    return [play_game(pair, True), play_game(pair, False)]


WORDS = set()


def summarize(results, args, final=False):
    games = [g for r in results for g in r]
    n = len(games)
    if not n:
        return ""
    wins = sum(1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0 for g in games)
    spreads = [g["sa"] - g["sb"] for g in games]
    # standard errors from pairs (the unit of independence)
    pw = [sum(1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0 for g in r) / len(r) for r in results]
    ps = [sum(g["sa"] - g["sb"] for g in r) / len(r) for r in results]
    m = len(results)

    def se(v):
        if len(v) < 2:
            return float("nan")
        mu = sum(v) / len(v)
        return math.sqrt(sum((x - mu) ** 2 for x in v) / (len(v) - 1) / len(v))

    wr = wins / n
    msp = sum(spreads) / n
    ta = sum(g["spent"][0] for g in games) / max(1, sum(g["moves"][0] for g in games))
    tb = sum(g["spent"][1] for g in games) / max(1, sum(g["moves"][1] for g in games))
    mxa = max(g["maxt"][0] for g in games)
    mxb = max(g["maxt"][1] for g in games)
    nerr = sum(len(g["errors"]) for g in games)
    avg_a = sum(g["sa"] for g in games) / n
    avg_b = sum(g["sb"] for g in games) / n
    bi_a = sum(g["bingos"][0] for g in games) / n
    bi_b = sum(g["bingos"][1] for g in games) / n
    s = ("%s vs %s: %d games (%d pairs), %d ms/move\n"
         "  %s win %.1f%% +/- %.1f   spread %+.1f +/- %.1f   avg score %.1f - %.1f   bingos %.2f - %.2f\n"
         "  time/move %.3fs (max %.2fs) vs %.3fs (max %.2fs)   illegal/crash events: %d"
         % (args.a_name, args.b_name, n, m, args.movetime, args.a_name, 100 * wr, 196 * se(pw), msp,
            1.96 * se(ps), avg_a, avg_b, bi_a, bi_b, ta, mxa, tb, mxb, nerr))
    if args.prefix or args.positions:
        # Spread gained after the handoff (the position is identical within a pair).
        pg = [sum((g["sa"] - g["sb"]) - (g["ha"] - g["hb"]) for g in r) / len(r) for r in results]
        start = "recorded positions" if args.positions else "%d tiles in the bag" % args.prefix_until_bag
        s += "\n  from %s: %s gains %+.2f +/- %.2f points a game" % (
            start, args.a_name, sum(pg) / len(pg), 1.96 * se(pg))
    return s


def run_meta(args):
    """What a log needs to be reproduced: engines, settings, word list, builds.
    The home directory is written as ~ so a shared log names no user."""
    home = os.path.expanduser("~")

    def clean(s):
        return s.replace(home, "~") if home and home != "~" else s

    def info(path):
        if not path:
            return ""
        with open(path) as f:
            return clean(f.read())

    h = hashlib.sha256()
    with open(args.lexicon, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    ph = hashlib.sha256()
    if args.positions:
        with open(args.positions, "rb") as f:
            for block in iter(lambda: f.read(1 << 20), b""):
                ph.update(block)
    return dict(started=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                a=clean(args.a), b=clean(args.b), a_name=args.a_name, b_name=args.b_name,
                a_init=args.a_init, b_init=args.b_init, a_info=info(args.a_info), b_info=info(args.b_info),
                a_history=args.a_history, b_history=args.b_history,
                prefix=clean(args.prefix), prefix_init=args.prefix_init, prefix_until_bag=args.prefix_until_bag,
                positions=os.path.basename(args.positions), positions_sha256=ph.hexdigest() if args.positions else "",
                games=args.games, first_pair=args.first_pair, single=args.single, movetime=args.movetime,
                parallel=args.parallel, seed=args.seed, lexicon=os.path.basename(args.lexicon),
                lexicon_sha256=h.hexdigest(), python=platform.python_version(),
                os=platform.system() + " " + platform.release(), cpus=os.cpu_count())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--lexicon", required=True, help="word list (one word per line)")
    ap.add_argument("--a", required=True)
    ap.add_argument("--b", required=True)
    ap.add_argument("--a-init", default="", help="commands sent to engine A at start, ';'-separated")
    ap.add_argument("--b-init", default="", help="commands sent to engine B at start, ';'-separated")
    ap.add_argument("--a-name", default="A")
    ap.add_argument("--b-name", default="B")
    ap.add_argument("--games", type=int, default=100, help="number of game pairs")
    ap.add_argument("--movetime", type=int, default=1000, help="milliseconds per move")
    ap.add_argument("--parallel", type=int, default=1)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--first-pair", type=int, default=0, help="start at this pair number (to resume a match)")
    ap.add_argument("--single", action="store_true", help="one game per seed instead of a swapped pair")
    start = ap.add_mutually_exclusive_group()
    start.add_argument("--prefix", default="", help="engine that plays both seats until the bag is small (e.g. a static "
                    "player), so A and B meet in identical pre-endgame or endgame positions")
    start.add_argument("--positions", default="", help="recorded positions in JSON lines; pair k uses zero-based line k")
    ap.add_argument("--prefix-init", default="", help="commands sent to the prefix engine at start")
    ap.add_argument("--prefix-until-bag", type=int, default=7, help="hand over once the bag has this many tiles or fewer")
    ap.add_argument("--gcg-dir", default="")
    ap.add_argument("--log", default="", help="append per-game JSON lines here (analyse with tools/analyze.py)")
    ap.add_argument("--a-info", default="", help="file describing engine A's build (e.g. bin/magpie_bot.provenance), "
                    "copied into the log")
    ap.add_argument("--b-info", default="", help="the same for engine B")
    ap.add_argument("--a-history", action="store_true", help="send engine A the game's history (see above)")
    ap.add_argument("--b-history", action="store_true", help="send engine B the game's history")
    ap.add_argument("--history-lexicon", default="", help="lexicon name written into the history's GCG")
    ap.add_argument("--startup-timeout", type=float, default=600, help="seconds an engine may take to load")
    args = ap.parse_args()
    if args.positions:
        try:
            args.position_data = load_positions(args.positions, args.first_pair, args.games)
        except (OSError, ValueError, KeyError, TypeError) as ex:
            ap.error("--positions: %s" % ex)
    Engine.STARTUP_SECONDS = args.startup_timeout
    # Start every engine once here: one that cannot start stops the match with a clear
    # message (inside the worker pool it would be restarted silently, for ever).
    for spec, name, init in [(args.a, args.a_name, args.a_init), (args.b, args.b_name, args.b_init)] + (
            [(args.prefix, "prefix", args.prefix_init)] if args.prefix else []):
        try:
            Engine(spec, name, init).close()
        except Exception as ex:
            raise SystemExit("engine %s did not start (%s): %s" % (name, ex, spec))
    load_words(args.lexicon)
    t0 = time.time()
    results = []
    logf = open(args.log, "a") if args.log else None
    if logf:
        logf.write(json.dumps({"meta": run_meta(args)}) + "\n")
        logf.flush()
    with mp.Pool(args.parallel, initializer=worker_init, initargs=(args,)) as pool:
        for r in pool.imap_unordered(run_pair, range(args.first_pair, args.first_pair + args.games)):
            results.append(r)
            for g in r:
                for e in g["errors"]:
                    print("  !", e, file=sys.stderr)
                if logf:
                    logf.write(json.dumps(g) + "\n")
                    logf.flush()
            if len(results) % max(1, args.games // 10) == 0 or len(results) == args.games:
                lines = summarize(results, args).split("\n")
                print("[%d/%d pairs, %.0fs] %s%s" % (len(results), args.games, time.time() - t0, lines[1].strip(),
                                                    ("   |" + lines[3]) if len(lines) > 3 else ""), flush=True)
    print(summarize(results, args, True))


if __name__ == "__main__":
    main()
