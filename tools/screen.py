#!/usr/bin/env python3
"""A full-game screen's decision under a rule fixed beforehand, optionally paired by deal.

    python3 tools/screen.py infer-screen-frozen.jsonl.gz --min-score 0.5 --min-spread 0
    python3 tools/screen.py infer-screen-macondo.jsonl.gz --control experiments/fresh-9200.jsonl.gz \\
        --min-score 0.5225

Engine A is the candidate.  Its score (draws half) and spread a game get 95% intervals from
a bootstrap over deal pairs.  With --control, a log of another engine A on the same seed is
paired with it deal by deal: the two logs must share the seed, and where both have move
records the first position of every game must be identical, so the comparison is between
the same tiles.  The paired difference (candidate minus control, per deal pair) is reported
with its own interval.  The rule passes when every given threshold is met by the point
estimates: --min-score (strictly above it if --strict, as in "above 50%"), --min-spread
(strictly above).  Nothing is decided from intervals here; they are reported.
"""
import argparse
import gzip
import json
import random
import sys
from collections import defaultdict


def read(path):
    opener = gzip.open if path.endswith(".gz") else open
    meta, pairs = {}, defaultdict(dict)
    with opener(path, "rt", encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            if "meta" in d:
                meta = d["meta"]
                continue
            if "sa" in d:
                pairs[d["pair"]][bool(d["a_first"])] = d
    return meta, pairs


def result(g):
    return 1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0


def pair_values(pairs):
    """Per complete deal pair: A's mean score and mean spread over its two games."""
    out = {}
    for k, g in pairs.items():
        if len(g) == 2:
            out[k] = (sum(result(x) for x in g.values()) / 2, sum(x["sa"] - x["sb"] for x in g.values()) / 2)
    return out


def boot(values, n, seed):
    """Mean of values with a 95% interval, resampling the values (one per deal pair)."""
    if not values:
        return float("nan"), float("nan"), float("nan")
    rng = random.Random(seed)
    m = len(values)
    bs = sorted(sum(values[rng.randrange(m)] for _ in range(m)) / m for _ in range(n))
    return sum(values) / m, bs[int(0.025 * n)], bs[min(n - 1, int(0.975 * n))]


def first_cgp(g):
    rec = g.get("record") or []
    return rec[0].get("cgp") if rec else None


def same_deals(pc, pk):
    """Pairs present in both logs, and how many first positions could be compared / differed."""
    common = sorted(set(pc) & set(pk))
    compared = differ = 0
    for k in common:
        for side in (True, False):
            a, b = first_cgp(pc[k].get(side, {})), first_cgp(pk[k].get(side, {}))
            if a and b:
                compared += 1
                differ += a != b
    return common, compared, differ


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--control", default="", help="log of the control engine A on the same seed (paired by deal)")
    ap.add_argument("--min-score", type=float, default=None)
    ap.add_argument("--strict", action="store_true", help="the score must be strictly above --min-score")
    ap.add_argument("--min-spread", type=float, default=None, help="the spread a game must be strictly above this")
    ap.add_argument("--bootstrap", type=int, default=10000)
    ap.add_argument("--seed", type=int, default=1)
    a = ap.parse_args()

    meta, pc = read(a.log)
    vals = pair_values(pc)
    keys = sorted(vals)
    s = boot([vals[k][0] for k in keys], a.bootstrap, a.seed)
    sp = boot([vals[k][1] for k in keys], a.bootstrap, a.seed)
    print(f"{a.log}: seed {meta.get('seed')}, {len(keys)} deal pairs")
    print(f"  A's score  {100 * s[0]:6.2f}% ({100 * s[1]:.2f}% to {100 * s[2]:.2f}%)")
    print(f"  A's spread {sp[0]:+7.1f} a game ({sp[1]:+.1f} to {sp[2]:+.1f})")
    ok = True
    if a.control:
        cmeta, pk = read(a.control)
        if str(cmeta.get("seed")) != str(meta.get("seed")):
            sys.exit(f"control seed {cmeta.get('seed')} differs from {meta.get('seed')}: not the same deals")
        common, compared, differ = same_deals(pc, pk)
        if differ:
            sys.exit(f"{differ} of {compared} first positions differ between the logs: not the same deals")
        cv = pair_values(pk)
        both = [k for k in common if k in vals and k in cv]
        cs = boot([cv[k][0] for k in both], a.bootstrap, a.seed)
        d = boot([vals[k][0] - cv[k][0] for k in both], a.bootstrap, a.seed)
        dsp = boot([vals[k][1] - cv[k][1] for k in both], a.bootstrap, a.seed)
        print(f"  control on the same {len(both)} deal pairs ({compared} first positions checked identical): "
              f"score {100 * cs[0]:.2f}%")
        print(f"  paired difference, candidate - control: score {100 * d[0]:+.2f} points ({100 * d[1]:+.2f} to "
              f"{100 * d[2]:+.2f}); spread {dsp[0]:+.1f} ({dsp[1]:+.1f} to {dsp[2]:+.1f})")
    if a.min_score is not None:
        met = s[0] > a.min_score + 1e-12 if a.strict else s[0] >= a.min_score - 1e-12  # 209/400 vs 0.5225
        ok &= met
        print(f"  rule: score {'>' if a.strict else '>='} {100 * a.min_score:.2f}%: {'met' if met else 'NOT met'}")
    if a.min_spread is not None:
        met = sp[0] > a.min_spread
        ok &= met
        print(f"  rule: spread > {a.min_spread:+.1f}: {'met' if met else 'NOT met'}")
    if a.min_score is not None or a.min_spread is not None:
        print(f"  decision: {'PASSES' if ok else 'FAILS'} (point estimates, as registered)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
