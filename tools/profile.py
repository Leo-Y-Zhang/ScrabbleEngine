#!/usr/bin/env python3
"""Where an engine's computation goes, from the move records of referee logs.

    python3 tools/profile.py LOG.jsonl[.gz] [LOG ...] [--engine A]

Uses each move's "info" line (Tilefish's search statistics; see tools/referee.py) and
the referee's own time and CPU measurements.  Prints, by game phase:

  * moves, wall time and CPU time a move (CPU / wall = cores actually used);
  * for simulated moves: legal moves generated, candidates kept and pruned, iterations
    and positions, and how the iterations were shared between the candidates;
  * the share of the posterior's precision that came from the static prior, for the
    competitive candidates (posterior within --close of the chosen move);
  * how often the move leading at 1/64, 1/32, ... 1/2 of the search was the final choice;
  * the time spent generating moves, inferring and simulating;
  * the chosen move's static rank;
  * calibration: the engine's estimated winning chance for its chosen move against the
    game's result, in bins.

Opponents' moves are summarised by time and CPU (and Macondo's own info, if present).
"""

import argparse
import collections
import gzip
import json
import math
import re


def load(paths):
    for path in paths:
        with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
            for line in f:
                line = line.strip()
                if line:
                    d = json.loads(line)
                    if "meta" not in d:
                        yield d


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    return s[min(len(s) - 1, int(q * (len(s) - 1) + 0.5))]


def mean(v):
    return sum(v) / len(v) if v else float("nan")


PHASES = ["sim", "playout", "peg", "endgame", "only", "other"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--engine", default="A", help="A or B: whose moves to profile in detail")
    ap.add_argument("--close", type=float, default=0.02,
                    help="a candidate is competitive if its posterior is within this of the chosen move's")
    args = ap.parse_args()
    who = 0 if args.engine.upper() == "A" else 1
    by = collections.defaultdict(list)
    other = collections.defaultdict(list)
    calib = []  # (estimated win, result) for the profiled engine's simulated moves
    games = 0
    for g in load(args.logs):
        if "record" not in g:
            continue
        games += 1
        res = [1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0]
        res.append(1.0 - res[0])
        for r in g["record"]:
            if "mv" not in r or r.get("p") not in (0, 1):
                continue
            info = r.get("info") if isinstance(r.get("info"), dict) else {}
            if r["p"] == who:
                ph = info.get("phase", "other")
                by[ph if ph in PHASES else "other"].append((r, info))
                if ph in ("sim", "playout") and info.get("best", {}).get("w") is not None:
                    calib.append((info["best"]["w"], res[who], r["bag"]))
            else:
                other["all"].append((r, info))
    name = "A" if who == 0 else "B"
    print("%d games with move records; profiling engine %s" % (games, name))
    print()
    print("phase      moves  wall s/move  CPU s/move  cores used  (wall: median, p90)")
    for ph in PHASES:
        v = by.get(ph)
        if not v:
            continue
        wall = [r["t"] for r, _ in v if "t" in r]
        cpu = [r["cpu"] for r, _ in v if "cpu" in r]
        print("%-9s %6d  %11.2f  %10s  %10s  (%.2f, %.2f)" % (
            ph, len(v), mean(wall), "%.2f" % mean(cpu) if cpu else "n/a",
            "%.2f" % (sum(cpu) / sum(wall)) if cpu and sum(wall) > 0 else "n/a", pct(wall, 0.5), pct(wall, 0.9)))
    ov = other.get("all", [])
    if ov:
        wall = [r["t"] for r, _ in ov if "t" in r]
        cpu = [r["cpu"] for r, _ in ov if "cpu" in r]
        print("opponent  %6d  %11.2f  %10s  %10s" % (len(ov), mean(wall), "%.2f" % mean(cpu) if cpu else "n/a",
                                                    "%.2f" % (sum(cpu) / sum(wall)) if cpu and sum(wall) > 0 else "n/a"))
        setup = [i["setup_s"] for _, i in ov if isinstance(i.get("setup_s"), (int, float))]
        search = [i["search_s"] for _, i in ov if isinstance(i.get("search_s"), (int, float))]
        infv = [i.get("inferred") for r, i in ov if "inferred" in i and r.get("bag", 0) > 0]
        games = collections.Counter(str(i.get("game", "cgp"))[:7] for _, i in ov if "setup_s" in i)
        if setup:
            print("  opponent set-up a move: mean %.3fs, max %.3fs; search a move: mean %.2fs; game built from: %s"
                  % (mean(setup), max(setup), mean(search), dict(games)))
            print("  opponent inference, moves with tiles in the bag: %d not attempted, %d attempted with nothing "
                  "inferred, %d inferred (median %s possible racks)" % (
                      sum(1 for x in infv if x == -1), sum(1 for x in infv if x == 0),
                      sum(1 for x in infv if x and x > 0), pct([x for x in infv if x and x > 0], 0.5)))
            its = []
            for _, i in ov:
                m = re.search(r"(\d+)\s+iterations", str(i.get("details", "")))
                if m:
                    its.append(int(m.group(1)))
            if its:
                print("  opponent iterations (from its own summary): n=%d, median %d, p10 %d, p90 %d"
                      % (len(its), pct(its, 0.5), pct(its, 0.1), pct(its, 0.9)))
    for ph in ("sim", "playout"):
        v = [i for _, i in by.get(ph, []) if "iters" in i]
        if not v:
            continue
        print()
        print("== %s (%d moves: %s) ==" % (ph, len(v), "2-ply rollouts" if ph == "sim" else "rollouts to the end, bag 2-7"))
        print("legal moves generated: median %d, p90 %d; candidates kept %d; excluded by the cut: median %d"
              % (pct([i["gen"] for i in v], 0.5), pct([i["gen"] for i in v], 0.9), pct([i["cands"] for i in v], 0.5),
                 pct([max(0, i["gen"] - i["cands"]) for i in v], 0.5)))
        print("iterations (most-simulated candidate): median %d, p10 %d, p90 %d; positions a move: median %d"
              % (pct([i["iters"] for i in v], 0.5), pct([i["iters"] for i in v], 0.1), pct([i["iters"] for i in v], 0.9),
                 pct([i["pos"] for i in v], 0.5)))
        print("candidates pruned by the end: mean %.1f of %.1f" % (mean([i["pruned"] for i in v]), mean([i["cands"] for i in v])))
        top1, top2, rest, still = [], [], [], []
        for i in v:
            a = sorted(i["alloc"], reverse=True)
            tot = sum(a)
            if tot <= 0:
                continue
            top1.append(a[0] / tot)
            top2.append((a[0] + (a[1] if len(a) > 1 else 0)) / tot)
            rest.append(1 - top2[-1])
            still.append(sum(1 for c in i["top"] if not c.get("pr")))
        print("share of all iterations: top candidate %.1f%%, top two %.1f%%, the other %d candidates %.1f%% (means)"
              % (100 * mean(top1), 100 * mean(top2), round(mean([i["cands"] for i in v])) - 2, 100 * mean(rest)))
        print("candidates still active at the end (of the top six): mean %.2f" % mean(still))
        # Prior weight for competitive candidates.
        pw_all, pw_n = [], []
        for i in v:
            for c in i["top"][1:]:
                if c.get("pw") is None or c.get("post") is None:
                    continue
                if abs(c["post"]) <= args.close:
                    pw_all.append(c["pw"])
                    pw_n.append(c["n"])
        if pw_all:
            print("prior's share of the posterior precision, competitive candidates (|posterior| <= %.3f): n=%d, "
                  "median %.3f, p75 %.3f, p90 %.3f, max %.3f; >0.5 in %.1f%%"
                  % (args.close, len(pw_all), pct(pw_all, 0.5), pct(pw_all, 0.75), pct(pw_all, 0.9), max(pw_all),
                     100 * sum(1 for x in pw_all if x > 0.5) / len(pw_all)))
            print("  their iterations: median %d, p10 %d" % (pct(pw_n, 0.5), pct(pw_n, 0.1)))
        flips = [0] * 6
        seen = [0] * 6
        for i in v:
            for k, s in enumerate(i.get("snap", [])[:6]):
                seen[k] += 1
                flips[k] += 1 - s["same"]
        print("leader at a fraction of the search differs from the final choice:  " + "  ".join(
            "1/%d: %s" % (64 >> k, "%.1f%%" % (100 * flips[k] / seen[k]) if seen[k] else "n/a") for k in range(6)))
        print("time a move: generate %.3fs, infer %.3fs, simulate %.2fs, total %.2fs (means)"
              % (mean([i["t"]["gen"] for i in v]), mean([i["t"]["inf"] for i in v]), mean([i["t"]["sim"] for i in v]),
                 mean([i["t"]["total"] for i in v])))
        sr = collections.Counter(min(i["best"]["s"], 5) for i in v)
        print("chosen move's static rank: " + ", ".join("%s: %.1f%%" % ("#%d" % (k + 1) if k < 5 else "#6+", 100 * sr[k] / len(v))
                                                       for k in range(6)))
        inf = [i["inf"] for i in v]
        print("inference used on %d of %d moves" % (sum(1 for x in inf if x >= 0), len(inf)))
    eg = [i for _, i in by.get("endgame", [])]
    if eg:
        print()
        print("== endgame (%d moves) == solved %.1f%%, depth median %d, time median %.2fs" % (
            len(eg), 100 * mean([1.0 if i.get("solved") else 0.0 for i in eg]), pct([i.get("depth", -1) for i in eg], 0.5),
            pct([i["t"]["total"] for i in eg], 0.5)))
    pg = [i for _, i in by.get("peg", [])]
    if pg:
        print("== pre-endgame (%d moves) == candidates solved: median %d, exactly: median %d; time median %.2fs" % (
            len(pg), pct([i.get("rows", 0) for i in pg], 0.5), pct([i.get("exact_rows", 0) for i in pg], 0.5),
            pct([i["t"]["total"] for i in pg], 0.5)))
    if calib:
        print()
        print("calibration of the estimated winning chance (chosen move, simulated moves) against the result:")
        print("estimate bin   moves  mean estimate  actual score   (bag >= 8 | bag 2-7)")
        for lo in [x / 10 for x in range(10)]:
            c = [(w, r, b) for w, r, b in calib if lo <= w < lo + 0.1 or (lo == 0.9 and w == 1.0)]
            if not c:
                continue
            mid = [(w, r) for w, r, b in c if b >= 8]
            late = [(w, r) for w, r, b in c if b < 8]
            print("[%.1f, %.1f)  %6d  %13.3f  %12.3f   (%s | %s)" % (
                lo, lo + 0.1, len(c), mean([w for w, _, _ in c]), mean([r for _, r, _ in c]),
                "%.3f vs %.3f, n=%d" % (mean([w for w, _ in mid]), mean([r for _, r in mid]), len(mid)) if mid else "-",
                "%.3f vs %.3f, n=%d" % (mean([w for w, _ in late]), mean([r for _, r in late]), len(late)) if late else "-"))
        brier = mean([(w - r) ** 2 for w, r, _ in calib])
        print("Brier score %.4f over %d moves (a constant 0.5 would score 0.25)" % (brier, len(calib)))


if __name__ == "__main__":
    main()
