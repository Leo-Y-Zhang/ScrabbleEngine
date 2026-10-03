#!/usr/bin/env python3
"""Statistics for match logs written by tools/referee.py --log.

    analyze.py LOG [LOG ...] [--alpha 0.05] [--candidates K] [--plan ELO [ELO ...]] [--power 0.8]
               [--bootstrap N]

Reports engine A's wins, draws and losses, its match score (wins plus half the
draws), the standard-Elo difference that score implies, and the spread, each with
a 95% interval.  The unit of sampling is the deal pair: both games of a pair use
the same tiles, so they are correlated, and the intervals come from the spread of
the pair averages, not of single games.  (With --single matches each game is its
own unit.)  The intervals use the normal approximation to the mean of the pair
scores; --bootstrap N adds a percentile bootstrap over the pairs (N resamples,
fixed seed), which does not lean on that approximation in short matches.

--candidates K applies a Bonferroni correction when this match is one of K
candidates screened against the same baseline, so a lucky screen is not
mistaken for an improvement.

--plan ELO gives the number of deal pairs a confirmation match needs to show a
true advantage of ELO points at the chosen alpha and power, using the pair
variance measured in these logs as the pilot estimate.
"""

import argparse
import json
import math
import random
import sys
from statistics import NormalDist


def score(g):
    return 1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0


def elo(s):
    s = min(max(s, 1e-4), 1 - 1e-4)
    return 400 * math.log10(s / (1 - s))


def mean_se(v):
    n = len(v)
    mu = sum(v) / n
    if n < 2:
        return mu, float("nan"), float("nan")
    sd = math.sqrt(sum((x - mu) ** 2 for x in v) / (n - 1))
    return mu, sd / math.sqrt(n), sd


def load(paths):
    """Games grouped into units: (run, pair) -> games.  A meta line starts a run."""
    units, metas, run = {}, [], 0
    for path in paths:
        with open(path) as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                d = json.loads(line)
                if "meta" in d:
                    run += 1
                    metas.append(d["meta"])
                    continue
                units.setdefault((run, d["pair"]), []).append(d)
    return units, metas


def report(units, metas, alpha, candidates, plan, power, boot=0):
    games = [g for u in units.values() for g in u]
    if not games:
        return "no games in the logs"
    out = []
    for m in metas:
        out.append("run %s: %s (%s) vs %s (%s), %d ms/move, seed %s, %s sha256 %s" % (
            m.get("started"), m.get("a_name"), m.get("a"), m.get("b_name"), m.get("b"), m.get("movetime", 0),
            m.get("seed"), m.get("lexicon"), (m.get("lexicon_sha256") or "")[:16]))
        for k in ("a_info", "b_info"):
            if m.get(k):
                out.append("  %s: %s" % (k, " | ".join(m[k].strip().splitlines())))
    w = sum(1 for g in games if g["sa"] > g["sb"])
    d = sum(1 for g in games if g["sa"] == g["sb"])
    l = len(games) - w - d
    pair_scores = [sum(score(g) for g in u) / len(u) for u in units.values()]
    pair_spreads = [sum(g["sa"] - g["sb"] for g in u) / len(u) for u in units.values()]
    s, se, sd = mean_se(pair_scores)
    sp, sp_se, _ = mean_se(pair_spreads)
    z = NormalDist().inv_cdf(1 - alpha / (2 * candidates))
    lo, hi = s - z * se, s + z * se
    level = 100 * (1 - alpha / candidates)
    out.append("A: %d games in %d units (pairs)   W %d  D %d  L %d   match score %.1f / %d" % (
        len(games), len(units), w, d, l, w + 0.5 * d, len(games)))
    out.append("A's score %.2f%%, %.1f%% interval %.2f%% to %.2f%%" % (100 * s, level, 100 * lo, 100 * hi))
    out.append("standard Elo %+.0f, %.1f%% interval %+.0f to %+.0f" % (elo(s), level, elo(lo), elo(hi)))
    if boot:
        rng = random.Random(1)
        k = len(pair_scores)
        means = sorted(sum(rng.choice(pair_scores) for _ in range(k)) / k for _ in range(boot))
        q = alpha / (2 * candidates)
        blo, bhi = means[int(q * (boot - 1))], means[int(math.ceil((1 - q) * (boot - 1)))]
        out.append("bootstrap over pairs (%d resamples): score %.2f%% to %.2f%%, Elo %+.0f to %+.0f" % (
            boot, 100 * blo, 100 * bhi, elo(blo), elo(bhi)))
    out.append("spread %+.1f points a game, %.1f%% interval %+.1f to %+.1f" % (
        sp, level, sp - z * sp_se, sp + z * sp_se))
    if se > 0:
        p = 1 - NormalDist().cdf((s - 0.5) / se)
        out.append("one-sided p (A no better than even) = %.4f%s" % (
            p, "" if candidates == 1 else ", Bonferroni over %d candidates: %.4f" % (candidates, min(1, p * candidates))))
    spent = [sum(g["spent"][k] for g in games) / max(1, sum(g["moves"][k] for g in games)) for k in (0, 1)]
    maxt = [max(g["maxt"][k] for g in games) for k in (0, 1)]
    errs = sum(len(g["errors"]) for g in games)
    out.append("time a move: A %.2fs (max %.2fs), B %.2fs (max %.2fs); illegal/crash/slow events: %d" % (
        spent[0], maxt[0], spent[1], maxt[1], errs))
    if plan:
        za = NormalDist().inv_cdf(1 - alpha / (2 * candidates))
        zb = NormalDist().inv_cdf(power)
        out.append("pair score SD %.3f (pilot estimate from %d units)" % (sd, len(units)))
        for e in plan:
            target = 1 / (1 + 10 ** (-e / 400))
            n = "%d units" % math.ceil(((za + zb) * sd / (target - 0.5)) ** 2) if sd > 0 else "unknown (no variance in the pilot)"
            out.append("to show +%g Elo (score %.2f%%) at alpha %g, power %g: %s" % (
                e, 100 * target, alpha / candidates, power, n))
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--alpha", type=float, default=0.05)
    ap.add_argument("--candidates", type=int, default=1)
    ap.add_argument("--plan", type=float, nargs="*", default=[])
    ap.add_argument("--power", type=float, default=0.8)
    ap.add_argument("--bootstrap", type=int, default=0, metavar="N")
    a = ap.parse_args()
    units, metas = load(a.logs)
    print(report(units, metas, a.alpha, a.candidates, a.plan, a.power, a.bootstrap))


if __name__ == "__main__":
    sys.exit(main())
