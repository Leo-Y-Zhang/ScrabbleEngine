#!/usr/bin/env python3
"""Enumerate exact pre-endgame values with two tiles in the bag.

    python3 tools/exactpeg.py run RUNS.jsonl [RUNS ...] --words CSW24.txt \\
        --engine "proto:./tf --lexicon CSW24.kwg --threads 1 --quiet" \\
        --egtime 3000 --parallel 8 --out exact.jsonl --settings base,k6 --limit 2
    python3 tools/exactpeg.py report exact.jsonl

Each candidate placing at least two tiles empties the bag.  Enumerate unordered
draws from the unseen pool, with their combinatorial probabilities, and ask the
engine to solve the resulting visible-rack endgames.  Unsolved values are saved
for inspection, but the report uses only positions whose evaluated candidates
all have every split solved.  Invalid candidates also exclude their position.
The chosen candidate is rank zero in info.top, even if it cannot be evaluated.
"""

import argparse
import collections
import itertools
import json
import math
import multiprocessing as mp
from multiprocessing.util import Finalize
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee  # noqa: E402

IDENT = ("src", "seed", "pair", "a_first", "move_no", "setting")
WORKER = {}


def splits(unseen):
    """Yield (draw Counter, probability) for each distinct two-tile multiset."""
    total = math.comb(sum(unseen.values()), 2)
    if not total:
        raise ValueError("fewer than two unseen tiles")
    for a, b in itertools.combinations_with_replacement(sorted(t for t in unseen if unseen[t]), 2):
        draw = collections.Counter((a, b))
        if any(draw[t] > unseen[t] for t in draw):
            continue
        ways = math.prod(math.comb(unseen[t], n) for t, n in draw.items())
        yield draw, ways / total


def position(cgp):
    """Recover the physical unseen tiles; lower-case board tiles are blanks."""
    board, racks, scores, zeros = cgp.split()[:4]
    rack = racks.split("/")[0]
    unseen = collections.Counter(referee.DIST)
    for row in referee.parse_board(board):
        for t in row:
            if t is not None:
                unseen["?" if t.islower() else t] -= 1
    unseen.subtract(rack)
    if any(n < 0 for n in unseen.values()) or sum(unseen.values()) != 9 or len(rack) != 7:
        raise ValueError("position is inconsistent with seven rack tiles and two bag tiles")
    return dict(board=board, racks=[rack, ""], scores=[int(s) for s in scores.split("/")],
                zeros=int(zeros)), unseen


def init_worker(words, engine, egtime):
    # This also runs under spawn: do not rely on inherited referee.WORDS.
    referee.WORDS.clear()
    referee.load_words(words)
    WORKER.clear()
    WORKER.update(spec=engine, egtime=egtime, engine=None)


def worker_engine():
    if WORKER["engine"] is None:
        WORKER["engine"] = referee.Engine(WORKER["spec"], "exactpeg")
        Finalize(None, WORKER["engine"].close, exitpriority=10)
    return WORKER["engine"]


def work(job):
    row, index, candidate = job
    pos, unseen = position(row["cgp"])
    draws = list(splits(unseen))
    out = {k: row[k] for k in IDENT if k in row}
    out.update(input_file=row["input_file"], input_line=row["input_line"],
               move=candidate["m"], top_index=index, w=candidate.get("w"),
               exact_w=None, exact_spread=None, splits=len(draws), solved=0)
    if "result" in row:
        out["result"] = row["result"]
    before = pos["scores"][0] - pos["scores"][1]
    wins, gains = [], []
    for draw, weight in draws:
        start = dict(pos, racks=[pos["racks"][0], list((unseen - draw).elements())],
                     bag=list(draw.elements()))
        game = referee.Game(referee.WORDS, 0, start)
        try:
            if game.parse(candidate["m"])[0] != "place":
                return None
        except (referee.IllegalMove, ValueError, IndexError):
            pass  # Let apply supply the referee's error text below.
        _, error = game.apply(candidate["m"])
        if error:
            # apply substitutes a pass on error: never search that substituted state.
            out.update(invalid=True, error=error)
            return out
        if game.last_tiles < 2:
            return None
        if game.bag:
            raise ValueError("candidate did not empty the bag")
        eng = worker_engine()
        eng.best(game.cgp(show_opp=True), WORKER["egtime"])
        info = eng.info
        if (not isinstance(info, dict) or info.get("phase") != "endgame"
                or isinstance(info.get("value"), bool)
                or not isinstance(info.get("value"), (int, float))
                or not math.isfinite(info["value"])):
            out["error"] = "engine did not return a finite endgame value"
            return out
        out["solved"] += info.get("solved") is True
        final = game.scores[0] - game.scores[1] - info["value"]
        wins.append(weight * (1.0 if final > 0 else 0.5 if final == 0 else 0.0))
        gains.append(weight * (final - before))
    out.update(exact_w=math.fsum(wins), exact_spread=math.fsum(gains))
    return out


def positions(paths, settings, limit):
    out = []
    for path in paths:
        with open(path) as f:
            for line_no, line in enumerate(f, 1):
                if not line.strip():
                    continue
                row = json.loads(line)
                if row.get("bag") != 2 or (settings and row.get("setting") not in settings):
                    continue
                out.append(dict(row, input_file=path, input_line=line_no))
                if limit and len(out) >= limit:
                    return out
    return out


def cmd_run(a):
    settings = set(a.settings.split(",")) if a.settings else None
    ps = positions(a.runs, settings, a.limit)
    jobs = [(p, i, c) for p in ps for i, c in enumerate((p.get("info") or {}).get("top", []))]
    print("%d positions, %d candidates, %d ms per split" % (len(ps), len(jobs), a.egtime), file=sys.stderr)
    with open(a.out, "w") as f, mp.Pool(a.parallel, initializer=init_worker,
                                       initargs=(a.words, a.engine, a.egtime)) as pool:
        for done, row in enumerate(pool.imap_unordered(work, jobs), 1):
            if row is not None:
                f.write(json.dumps(row) + "\n")
                f.flush()
            if done % 10 == 0 or done == len(jobs):
                print("%d/%d candidates" % (done, len(jobs)), file=sys.stderr)
        # Normal worker shutdown runs the finalizer and closes each engine.
        pool.close()
        pool.join()


def boot(groups, B=4000, seed=1):
    """Bootstrap whole (seed, pair) clusters, preserving the move-weighted mean."""
    totals = [(sum(g), len(g)) for g in groups.values()]
    rng = random.Random(seed)
    vals = []
    for _ in range(B):
        pick = [rng.choice(totals) for _ in totals]
        vals.append(sum(s for s, n in pick) / sum(n for s, n in pick))
    vals.sort()
    return vals[int(0.025 * B)], vals[int(0.975 * B) - 1]


def report(rows):
    by = collections.defaultdict(lambda: collections.defaultdict(list))
    for row in rows:
        # A file and physical line distinguish repeated inputs with the same metadata.
        key = (row["input_file"], row["input_line"])
        by[row.get("setting", "")][key].append(row)
    for setting, positions_by_key in sorted(by.items()):
        eligible = []
        unsolved, invalid = 0, 0
        for moves in positions_by_key.values():
            if any(m.get("invalid") for m in moves):
                invalid += 1
            elif any(m["splits"] <= 0 or m["solved"] != m["splits"]
                     or m.get("exact_w") is None or m.get("error") for m in moves):
                unsolved += 1
            else:
                eligible.append(moves)
        chosen = [m for moves in eligible for m in moves if m["top_index"] == 0]
        all_moves = [m for moves in eligible for m in moves]
        print("%s: %d positions included; excluded %d positions for unsolved splits, %d invalid positions"
              % (setting, len(eligible), unsolved, invalid))
        for label, moves in (("chosen", chosen), ("all", all_moves)):
            groups = collections.defaultdict(list)
            errors = [m["w"] - m["exact_w"] for m in moves if m.get("w") is not None]
            for m in moves:
                if m.get("w") is not None:
                    groups[(m.get("seed"), m.get("pair"))].append(m["w"] - m["exact_w"])
            if errors:
                lo, hi = boot(groups)
                print("  %s: count %d, mean w - exact_w %+.6f (95%% bootstrap %+.6f to %+.6f), "
                      "mean absolute difference %.6f, %d deal pairs"
                      % (label, len(errors), sum(errors) / len(errors), lo, hi,
                         sum(abs(e) for e in errors) / len(errors), len(groups)))
            else:
                print("  %s: count 0, mean and interval n/a, mean absolute difference n/a" % label)
        agreements, compared = 0, 0
        for moves in eligible:
            first = next((m for m in moves if m["top_index"] == 0), None)
            if first is not None:
                compared += 1
                agreements += first["exact_w"] >= max(m["exact_w"] for m in moves) - 1e-12
        share = "%.6f" % (agreements / compared) if compared else "n/a"
        print("  chosen exact-best agreement: %d/%d (%s); %d positions without an evaluated chosen move"
              % (agreements, compared, share, len(eligible) - compared))


def cmd_report(a):
    rows = []
    for path in a.files:
        with open(path) as f:
            rows.extend(json.loads(line) for line in f if line.strip())
    report(rows)


def positive(value):
    n = int(value)
    if n <= 0:
        raise argparse.ArgumentTypeError("must be positive")
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("runs", nargs="+")
    r.add_argument("--words", required=True)
    r.add_argument("--engine", required=True)
    r.add_argument("--egtime", type=positive, default=3000)
    r.add_argument("--parallel", type=positive, default=1)
    r.add_argument("--out", required=True)
    r.add_argument("--settings", default="")
    r.add_argument("--limit", type=int, default=0)
    p = sub.add_parser("report")
    p.add_argument("files", nargs="+")
    a = ap.parse_args()
    if a.cmd == "run" and a.limit < 0:
        ap.error("--limit must be nonnegative")
    (cmd_run if a.cmd == "run" else cmd_report)(a)


if __name__ == "__main__":
    main()
