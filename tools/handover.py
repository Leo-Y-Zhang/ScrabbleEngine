#!/usr/bin/env python3
"""Results of prefix ("handover") matches: both games of a deal pair are played by a
static engine until the bag is small, then engines A and B take over from the same
position with seats swapped (tools/referee.py --prefix).

    python3 tools/handover.py LOG.jsonl[.gz]                  # one run
    python3 tools/handover.py CANDIDATE.jsonl --control CONTROL.jsonl
        # two runs on the same seed against the same opponent: paired by deal pair

Prints points A gains on B from the handover to the end (per pair, bootstrap over
pairs), A's score, and with --control the difference candidate - control, paired by
deal pair (the positions are identical because the prefix player is deterministic).
"""

import argparse
import gzip
import json
import random


def load(path):
    by = {}
    with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            d = json.loads(line)
            if "meta" in d:
                continue
            by.setdefault(d["pair"], []).append(d)
    return by


def gain(g):
    return (g["sa"] - g["sb"]) - (g["ha"] - g["hb"])


def score(g):
    return 1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0


def mean(v):
    return sum(v) / len(v) if v else float("nan")


def boot(keys, fn, n, seed=1):
    rng = random.Random(seed)
    out = sorted(fn([rng.choice(keys) for _ in keys]) for _ in range(n))
    return out[int(0.025 * n)], out[int(0.975 * n) - 1]


def summary(name, by, n):
    keys = sorted(by)
    games = [g for k in keys for g in by[k]]
    lo, hi = boot(keys, lambda s: mean([gain(g) for k in s for g in by[k]]), n)
    slo, shi = boot(keys, lambda s: mean([score(g) for k in s for g in by[k]]), n)
    print("%s: %d pairs, %d games, mean bag at handover %.1f" % (name, len(keys), len(games), mean([g["hbag"] for g in games])))
    print("  points A gains after the handover: %+.2f (95%% %+.2f to %+.2f)" % (mean([gain(g) for g in games]), lo, hi))
    print("  A's score: %.2f%% (95%% %.2f%% to %.2f%%)" % (100 * mean([score(g) for g in games]), 100 * slo, 100 * shi))
    close = [g for g in games if abs(g["ha"] - g["hb"]) <= 40]
    print("  handovers within 40 points: %d games, A's score %.1f%%, points gained %+.2f" % (
        len(close), 100 * mean([score(g) for g in close]), mean([gain(g) for g in close])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--control", default="")
    ap.add_argument("--bootstrap", type=int, default=10000)
    args = ap.parse_args()
    a = load(args.log)
    summary(args.log, a, args.bootstrap)
    if args.control:
        c = load(args.control)
        summary(args.control, c, args.bootstrap)
        keys = sorted(set(a) & set(c))
        same = sum(1 for k in keys if sorted((g["ha"], g["hb"], g["hbag"]) for g in a[k]) ==
                   sorted((g["ha"], g["hb"], g["hbag"]) for g in c[k]))
        print("paired by deal pair: %d pairs in both; identical handover scores and bag in %d" % (len(keys), same))

        def d_gain(s):
            return mean([gain(g) for k in s for g in a[k]]) - mean([gain(g) for k in s for g in c[k]])

        def d_score(s):
            return mean([score(g) for k in s for g in a[k]]) - mean([score(g) for k in s for g in c[k]])
        lo, hi = boot(keys, d_gain, args.bootstrap)
        slo, shi = boot(keys, d_score, args.bootstrap)
        print("  candidate - control, points after the handover: %+.2f (95%% %+.2f to %+.2f)" % (d_gain(keys), lo, hi))
        print("  candidate - control, score: %+.2f points (95%% %+.2f to %+.2f)" % (100 * d_score(keys), 100 * slo, 100 * shi))


if __name__ == "__main__":
    main()
