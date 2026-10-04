#!/usr/bin/env python3
"""Margin distribution of a referee log: why can the average spread favour one engine
while the game score is level?

    python3 tools/margins.py experiments/base-v2.2-vs-macondo-simming-20s.jsonl

Prints, for engine A: the full table of final margins (A's score minus B's) in 25-point
bins, the mean and median margin, how many games were close (|margin| <= 25 or 50),
how much of the mean spread comes from blowouts (|margin| > 100), and the same split by
who moved first.  Every number is computed from the per-game lines of the log; nothing
is assumed about the engines.
"""

import argparse
import gzip
import json
import math
import random


def load(path):
    games, meta = [], None
    with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            d = json.loads(line)
            if "meta" in d:
                meta = meta or d["meta"]
                continue
            games.append(d)
    return meta, games


def score(m):
    return 1.0 if m > 0 else 0.5 if m == 0 else 0.0


def median(v):
    s = sorted(v)
    n = len(s)
    return (s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])) if n else float("nan")


def summary(label, ms):
    n = len(ms)
    if not n:
        return "%-28s no games" % label
    mean = sum(ms) / n
    sd = math.sqrt(sum((x - mean) ** 2 for x in ms) / max(1, n - 1))
    return "%-28s n=%3d  score %5.1f%%  mean %+6.1f  median %+5.1f  SD %5.1f" % (
        label, n, 100 * sum(score(m) for m in ms) / n, mean, median(ms), sd)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--bin", type=int, default=25)
    ap.add_argument("--bootstrap", type=int, default=2000, help="resamples (by pair) for the intervals")
    args = ap.parse_args()
    for path in args.logs:
        meta, games = load(path)
        ms = [g["sa"] - g["sb"] for g in games]
        n = len(ms)
        print("=" * 78)
        print(path)
        if meta:
            print("movetime %s ms, seed %s, A = %s, B = %s" % (meta.get("movetime"), meta.get("seed"),
                                                             meta.get("a_init") or meta.get("a"), meta.get("b")))
        print(summary("all games", ms))
        print(summary("A moved first", [g["sa"] - g["sb"] for g in games if g["a_first"]]))
        print(summary("B moved first", [g["sa"] - g["sb"] for g in games if not g["a_first"]]))
        wins = [m for m in ms if m > 0]
        losses = [m for m in ms if m < 0]
        print("A's wins:   %3d, mean margin %+6.1f, median %+5.1f" % (len(wins), sum(wins) / max(1, len(wins)), median(wins)))
        print("A's losses: %3d, mean margin %+6.1f, median %+5.1f" % (len(losses), sum(losses) / max(1, len(losses)), median(losses)))
        for cut in (10, 25, 50):
            close = [m for m in ms if abs(m) <= cut]
            print("close games |margin| <= %2d: %3d (%4.1f%%), A scores %5.1f%% of them" % (
                cut, len(close), 100 * len(close) / n, 100 * sum(score(m) for m in close) / max(1, len(close))))
        big = [m for m in ms if abs(m) > 100]
        print("blowouts |margin| > 100:   %3d (%4.1f%%), A scores %5.1f%% of them; they contribute %+.1f of the "
              "mean spread %+.1f" % (len(big), 100 * len(big) / n, 100 * sum(score(m) for m in big) / max(1, len(big)),
                                     sum(big) / n, sum(ms) / n))
        # Interval for (mean spread) and (score) by resampling pairs.
        pairs = {}
        for g in games:
            pairs.setdefault(g["pair"], []).append(g["sa"] - g["sb"])
        keys = sorted(pairs)
        rng = random.Random(1)
        boot_s, boot_w, boot_c = [], [], []
        for _ in range(args.bootstrap):
            sample = [m for k in (rng.choice(keys) for _ in keys) for m in pairs[k]]
            boot_s.append(sum(sample) / len(sample))
            boot_w.append(sum(score(m) for m in sample) / len(sample))
            cl = [m for m in sample if abs(m) <= 50]
            boot_c.append(sum(score(m) for m in cl) / max(1, len(cl)))
        for name, b in (("mean spread", boot_s), ("score", boot_w), ("score in |margin|<=50", boot_c)):
            b.sort()
            lo, hi = b[int(0.025 * len(b))], b[int(0.975 * len(b)) - 1]
            print("  95%% interval (bootstrap over %d pairs) for %-22s %+.3f to %+.3f" % (len(keys), name + ":", lo, hi))
        lo_edge = (min(ms) // args.bin) * args.bin
        hi_edge = (max(ms) // args.bin + 1) * args.bin
        print("margin bin (A - B)      games   A wins  cumulative")
        cum = 0
        e = lo_edge
        while e < hi_edge:
            k = [m for m in ms if e <= m < e + args.bin]
            cum += len(k)
            print("[%+5d, %+5d)  %6d  %7d   %5.1f%%  %s" % (e, e + args.bin, len(k), sum(1 for m in k if m > 0),
                                                          100 * cum / n, "#" * len(k)))
            e += args.bin


if __name__ == "__main__":
    main()
