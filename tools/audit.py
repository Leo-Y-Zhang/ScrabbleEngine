#!/usr/bin/env python3
"""Where games are won and lost, from the move records of referee logs.

    python3 tools/audit.py LOG.jsonl[.gz] [LOG ...]

For engine A against engine B, using only the referee's records (no engine's opinion):

  * points A gains on B in each phase of the game (by tiles in the bag before the move:
    opening 61+, middle 15-60, approach 8-14, late 2-7, last tile 1, endgame 0, plus the
    end-of-game rack adjustments), for all games, A's wins, A's losses and close games;
  * conversion: the result as a function of the spread when the bag first holds 7 tiles
    or fewer, separately for positions where A leads and where B leads by the same
    amount, so the two engines' late play can be compared from equal standing;
  * a list of the games for the representative sample (wins, losses, close games), with
    the spread at each phase boundary.
"""

import argparse
import collections
import gzip
import json
import math
import random

PHASES = [("opening", 61, 100), ("middle", 15, 60), ("approach", 8, 14), ("late", 2, 7), ("last tile", 1, 1),
          ("endgame", 0, 0)]


def phase_of(bag):
    for name, lo, hi in PHASES:
        if lo <= bag <= hi:
            return name
    return "?"


def load(paths):
    for path in paths:
        with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
            for line in f:
                line = line.strip()
                if line:
                    d = json.loads(line)
                    if "meta" not in d and "record" in d:
                        d["_log"] = path
                        yield d


def mean(v):
    return sum(v) / len(v) if v else float("nan")


def boot_ci(units, stat, n=2000, seed=1):
    """95% interval of stat(list of games) resampling pairs."""
    keys = sorted(units)
    rng = random.Random(seed)
    vals = []
    for _ in range(n):
        sample = [g for k in (rng.choice(keys) for _ in keys) for g in units[k]]
        vals.append(stat(sample))
    vals.sort()
    return vals[int(0.025 * n)], vals[int(0.975 * n) - 1]


def analyse_game(g):
    """Per-phase points for A minus B, spread at phase boundaries (A - B, before the first
    move of the phase), and who is to move then."""
    gain = collections.defaultdict(float)
    spread = 0
    entry = {}
    for r in g["record"]:
        if "end" in r:
            gain["rack adjustments"] += r["sc"] if r["p"] == 0 else -r["sc"]
            spread += r["sc"] if r["p"] == 0 else -r["sc"]
            continue
        ph = phase_of(r["bag"])
        if ph not in entry:
            entry[ph] = (spread, r["p"])
        if r["bag"] <= 7 and "le7" not in entry:
            entry["le7"] = (spread, r["p"])
        pts = r["sc"] if r["p"] == 0 else -r["sc"]
        gain[ph] += pts
        spread += pts
    return gain, entry, spread


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--sample", type=int, default=0, help="also print a representative sample of this many games")
    ap.add_argument("--sample-seed", type=int, default=1)
    args = ap.parse_args()
    games = list(load(args.logs))
    if not games:
        raise SystemExit("no games with move records")
    rows = []
    for g in games:
        gain, entry, final = analyse_game(g)
        margin = g["sa"] - g["sb"]
        if final != margin:
            print("warning: record of pair %s does not add up (%d vs %d)" % (g["pair"], final, margin))
        rows.append(dict(g=g, gain=gain, entry=entry, margin=margin))
    units = collections.defaultdict(list)
    for r in rows:
        units[(r["g"]["_log"], r["g"]["pair"])].append(r)
    print("%d games (%d pairs) with move records" % (len(rows), len(units)))
    print()
    cols = [p[0] for p in PHASES] + ["rack adjustments"]
    groups = [("all games", lambda r: True), ("A's wins", lambda r: r["margin"] > 0),
              ("A's losses", lambda r: r["margin"] < 0), ("close (|margin| <= 50)", lambda r: abs(r["margin"]) <= 50),
              ("close, A lost", lambda r: -50 <= r["margin"] < 0), ("close, A won", lambda r: 0 < r["margin"] <= 50)]
    print("Points A gains on B a game, by phase (tiles in the bag before the move)")
    print("%-24s %5s " % ("games", "n") + " ".join("%10s" % c[:10] for c in cols) + " %8s" % "total")
    for name, f in groups:
        sel = [r for r in rows if f(r)]
        if not sel:
            continue
        print("%-24s %5d " % (name, len(sel)) + " ".join("%+10.1f" % mean([r["gain"].get(c, 0.0) for r in sel]) for c in cols)
              + " %+8.1f" % mean([r["margin"] for r in sel]))
    print()
    print("95% intervals (bootstrap over pairs) for the per-phase gain over all games:")
    for c in cols:
        lo, hi = boot_ci(units, lambda s, c=c: mean([r["gain"].get(c, 0.0) for r in s]))
        print("  %-17s %+6.1f  (%+.1f to %+.1f)" % (c, mean([r["gain"].get(c, 0.0) for r in rows]), lo, hi))
    print()
    # Conversion from the start of the late phase (first move with 7 or fewer in the bag).
    print("Result by spread when the bag first holds 7 tiles or fewer (A's view; 'mirror' = B leads by the same)")
    print("A's lead at that point   games  A's score   |  B's lead   games  B's score   (equal play: the same)")
    bins = [(1, 20), (21, 40), (41, 60), (61, 100), (101, 1000)]
    for side_lo, side_hi in [(-0.5, 0.5)] + bins:
        a_lead = [r for r in rows if "le7" in r["entry"] and side_lo <= r["entry"]["le7"][0] <= side_hi]
        b_lead = [r for r in rows if "le7" in r["entry"] and side_lo <= -r["entry"]["le7"][0] <= side_hi]
        sa = mean([1.0 if r["margin"] > 0 else 0.5 if r["margin"] == 0 else 0.0 for r in a_lead])
        sb = mean([1.0 if r["margin"] < 0 else 0.5 if r["margin"] == 0 else 0.0 for r in b_lead])
        label = "level" if side_hi < 1 else "%d to %d" % (side_lo, side_hi)
        print("%-22s %7d  %8.1f%%   |  %-9s %5d  %8.1f%%" % (label, len(a_lead), 100 * sa if a_lead else float("nan"),
                                                           label, len(b_lead), 100 * sb if b_lead else float("nan")))
    late = [r for r in rows if "le7" in r["entry"]]
    close_late = [r for r in late if abs(r["entry"]["le7"][0]) <= 40]
    if close_late:
        # Points gained from the late phase on, in games within 40 at its start.
        def after(r):
            return r["margin"] - r["entry"]["le7"][0]
        lo, hi = boot_ci(units, lambda s: mean([r["margin"] - r["entry"]["le7"][0] for r in s
                                                 if "le7" in r["entry"] and abs(r["entry"]["le7"][0]) <= 40]))
        score = mean([1.0 if r["margin"] > 0 else 0.5 if r["margin"] == 0 else 0.0 for r in close_late])
        print("games within 40 when the bag first holds <= 7: %d; A scores %.1f%%; A gains %+.1f points from there on "
              "(95%% interval %+.1f to %+.1f)" % (len(close_late), 100 * score, mean([after(r) for r in close_late]), lo, hi))
    if args.sample:
        rng = random.Random(args.sample_seed)
        strata = [("A lost by 1-50", lambda r: -50 <= r["margin"] < 0), ("A won by 1-50", lambda r: 0 < r["margin"] <= 50),
                  ("A lost by 51+", lambda r: r["margin"] < -50), ("A won by 51+", lambda r: r["margin"] > 50)]
        k = max(1, args.sample // len(strata))
        print()
        print("Representative sample: %d games from each stratum, chosen with seed %d" % (k, args.sample_seed))
        for name, f in strata:
            sel = [r for r in rows if f(r)]
            pick = rng.sample(sel, min(k, len(sel)))
            for r in pick:
                g = r["g"]
                print("  %-15s pair %4d %s  margin %+4d  spread at late %+4s" % (
                    name, g["pair"], "A first" if g["a_first"] else "B first", r["margin"],
                    r["entry"].get("le7", ("-",))[0]))


if __name__ == "__main__":
    main()
