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
With --prefix, a (deterministic) engine plays both seats until the bag holds
--prefix-until-bag tiles; A and B then take over from identical positions, which
measures pre-endgame and endgame play on their own.

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
import subprocess
import sys
import time

N = 15
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


class Game:
    def __init__(self, words, seed):
        self.words = words
        self.rng = random.Random(seed)
        bag = [t for t, k in DIST.items() for _ in range(k)]
        self.rng.shuffle(bag)
        self.bag = bag  # draw from the end
        self.board = [[None] * N for _ in range(N)]
        self.racks = [[], []]
        self.scores = [0, 0]
        self.turn = 0
        self.zeros = 0
        self.over = False
        self.moves = []  # (player, rack_before, text, score)
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

    def gcg(self, names):
        out = ["#character-encoding UTF-8", "#player1 %s %s" % (names[0], names[0]),
               "#player2 %s %s" % (names[1], names[1])]
        tot = [0, 0]
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
    def __init__(self, spec, name, init=""):
        self.name = name
        self.init = [c.strip() for c in init.split(";") if c.strip()]
        kind, _, cmd = spec.partition(":")
        if kind not in ("proto", "legacy"):
            raise SystemExit("engine spec must start with proto: or legacy:")
        self.kind, self.cmd = kind, cmd
        self.proc = None
        self.start()

    def start(self):
        self.proc = subprocess.Popen(self.cmd, shell=True, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=subprocess.DEVNULL, text=True, bufsize=1)
        for c in self.init:
            self.send(c)
        # Wait until the engine has loaded its data, so start-up is not charged to a move.
        self.send("isready")
        while self.readline() != "readyok":
            pass

    def send(self, line):
        self.proc.stdin.write(line + "\n")
        self.proc.stdin.flush()

    def readline(self):
        line = self.proc.stdout.readline()
        if not line:
            raise EOFError("engine %s exited" % self.name)
        return line.strip()

    def best(self, cgp, ms):
        if self.kind == "proto":
            self.send("position cgp " + cgp)
            self.send("go movetime %d" % ms)
            while True:
                line = self.readline()
                if line.startswith("bestmove "):
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


def worker_init(args):
    WORKER["args"] = args
    WORKER["engines"] = [Engine(args.a, args.a_name, args.a_init), Engine(args.b, args.b_name, args.b_init)]
    WORKER["prefix"] = Engine(args.prefix, "prefix", args.prefix_init) if args.prefix else None


def play_game(pair, a_first):
    args = WORKER["args"]
    eng = WORKER["engines"]
    g = Game(WORDS, args.seed * 1000003 + pair)
    # seat 0 moves first
    seat = [0, 1] if a_first else [1, 0]  # seat -> engine index (0 = A)
    errors, spent, maxt = [], [0.0, 0.0], [0.0, 0.0]
    bingos = [0, 0]
    turns = 0
    handoff = None  # scores of A and B when the prefix engine hands over
    prefix = WORKER["prefix"]
    while not g.over and turns < 150:
        cgp = g.cgp(show_opp=not g.bag)
        if prefix is not None and len(g.bag) > args.prefix_until_bag:
            # Opening and middle game played identically in both games of a pair.
            try:
                text = prefix.best(cgp, args.movetime)
            except Exception as ex:
                errors.append("prefix crashed: %s" % ex)
                prefix.proc.kill()
                prefix.start()
                text = "pass"
            score, err = g.apply(text)
            if err:
                errors.append("prefix: %s  [%s]" % (err, cgp))
            turns += 1
            continue
        if handoff is None:
            handoff = (g.scores[seat.index(0)], g.scores[seat.index(1)], len(g.bag))
        e = seat[g.turn]
        t0 = time.time()
        try:
            text = eng[e].best(cgp, args.movetime)
        except Exception as ex:  # crashed engine: restart, count as pass
            errors.append("%s crashed: %s" % (eng[e].name, ex))
            eng[e].proc.kill()
            eng[e].start()
            text = "pass"
        dt = time.time() - t0
        if dt > 3 * args.movetime / 1000.0 + 0.2:
            errors.append("%s slow: %.2fs, bag %d  [%s]" % (eng[e].name, dt, len(g.bag), cgp))
        spent[e] += dt
        maxt[e] = max(maxt[e], dt)
        score, err = g.apply(text)
        if err:
            errors.append("%s: %s  [%s]" % (eng[e].name, err, cgp))
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
    return dict(pair=pair, a_first=a_first, sa=sa, sb=sb, ha=handoff[0], hb=handoff[1], hbag=handoff[2], errors=errors,
                spent=spent, maxt=maxt,
                moves=[len([m for m in g.moves if m[2][0] != "end" and seat[m[0]] == k]) for k in (0, 1)],
                bingos=bingos)


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
    if args.prefix:
        # Spread gained after the handoff (the prefix is identical within a pair).
        pg = [sum((g["sa"] - g["sb"]) - (g["ha"] - g["hb"]) for g in r) / len(r) for r in results]
        s += "\n  from %d tiles in the bag: %s gains %+.2f +/- %.2f points a game" % (
            args.prefix_until_bag, args.a_name, sum(pg) / len(pg), 1.96 * se(pg))
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
    return dict(started=time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
                a=clean(args.a), b=clean(args.b), a_name=args.a_name, b_name=args.b_name,
                a_init=args.a_init, b_init=args.b_init, a_info=info(args.a_info), b_info=info(args.b_info),
                prefix=clean(args.prefix), prefix_init=args.prefix_init, prefix_until_bag=args.prefix_until_bag,
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
    ap.add_argument("--prefix", default="", help="engine that plays both seats until the bag is small (e.g. a static "
                    "player), so A and B meet in identical pre-endgame or endgame positions")
    ap.add_argument("--prefix-init", default="", help="commands sent to the prefix engine at start")
    ap.add_argument("--prefix-until-bag", type=int, default=7, help="hand over once the bag has this many tiles or fewer")
    ap.add_argument("--gcg-dir", default="")
    ap.add_argument("--log", default="", help="append per-game JSON lines here (analyse with tools/analyze.py)")
    ap.add_argument("--a-info", default="", help="file describing engine A's build (e.g. bin/magpie_bot.provenance), "
                    "copied into the log")
    ap.add_argument("--b-info", default="", help="the same for engine B")
    args = ap.parse_args()
    with open(args.lexicon) as f:
        for line in f:
            w = line.split()
            if w and w[0].isalpha():
                WORDS.add(w[0].upper())
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
