#!/usr/bin/env python3
"""Both engines' own winning estimates against results, and where the points of a match come from.

    python3 tools/latebias.py calib experiments/*.jsonl.gz
    python3 tools/latebias.py racks experiments/*.jsonl.gz
    python3 tools/latebias.py decided experiments/fresh-9200.jsonl.gz --by either
    python3 tools/latebias.py convert experiments/audit-v2.2-vs-macondo-simming-20s.jsonl.gz

Reads referee logs with move records.  Engine A is Tilefish; engine B is Tilefish or Macondo.
An estimate is the mover's own winning chance for the move it played: Tilefish's `best.w`
(phases `sim`, `playout` and `peg`; the endgame reports a spread, not a chance), Macondo's
first percentage in `details` (bag not empty).  The result is the mover's game result (1,
0.5, 0).  Intervals are 95%, from a bootstrap over deal pairs, since the moves of a game, and
the two games of a pair, share their deals.

  calib    mean(estimate - result) for late decisions (2-7 in the bag before the move,
           estimate 0.1-0.9), per engine, split by bag, by whether the move empties the bag
           and by estimate band; the middle game for comparison; and consecutive estimates:
           the mover's e(t) plus the opponent's e'(t+1) minus 1, which is 0 on average when
           both are calibrated, and the mover's drop from e(t) to its own e(t+2).
  racks    the opponent's actual rack at the mover's late decisions against uniform 7-tile
           draws from the mover's unseen tiles (rack quality: the mean six-tile leave value
           over the rack's seven one-tile removals, from ENABLE.leaves).
  decided  the final margin (A minus B) split at the first decision where an estimate (A's,
           B's or either's, as Tilefish's chance) reaches --lo or --hi: points gained before
           and after the game was decided; then A's share of wins by final-margin band.
  convert  section 4's table: A's and B's conversion of a lead when the bag first holds 7 or
           fewer, by lead band, with an interval for the difference.
"""
import argparse
import gzip
import itertools
import json
import os
import random
import re
import sys
from collections import Counter, defaultdict

PCT = re.compile(r"\(([\d.]+)%\)")
TILEFISH_PHASES = ("sim", "playout", "peg")
DIST = dict(A=9, B=2, C=2, D=4, E=12, F=2, G=3, H=2, I=9, J=1, K=1, L=4, M=2, N=6, O=8, P=2, Q=1,
            R=6, S=4, T=6, U=4, V=2, W=2, X=1, Y=2, Z=1)
DIST["?"] = 2
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def games(path):
    """The games of a log with move records, in file order."""
    with gzip.open(path, "rt", encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            if "record" in d:
                yield d


def result_a(g):
    return 1.0 if g["sa"] > g["sb"] else 0.5 if g["sa"] == g["sb"] else 0.0


def mover_result(g, p):
    ra = result_a(g)
    return ra if p == 0 else 1 - ra


def estimate(r):
    """(engine, the mover's estimate) for a move record, or None."""
    info = r.get("info")
    if not info or "bag" not in r:
        return None
    if "phase" in info:
        if info["phase"] not in TILEFISH_PHASES:
            return None
        w = (info.get("best") or {}).get("w")
        return None if w is None else ("tilefish", float(w))
    if "details" in info and r["bag"] > 0:
        m = PCT.search(info["details"])
        return ("macondo", float(m.group(1)) / 100) if m else None
    return None


def tiles_placed(mv):
    """Tiles a move takes from the rack: play-through letters, in brackets or as '.', excluded."""
    parts = (mv or "").split()
    if len(parts) < 2 or parts[0].startswith("-") or parts[0].lower().startswith("exch"):
        return 0
    word = re.sub(r"\([^)]*\)", "", parts[1]).replace(".", "")
    return sum(c.isalpha() for c in word)


def lead_of_a(r):
    """A's score minus B's from a record's CGP (whose scores are mover/opponent)."""
    s = r["cgp"].split()[2].split("/")
    lead = int(s[0]) - int(s[1])
    return lead if r["p"] == 0 else -lead


def unseen(board, rack):
    """The tiles the mover cannot see: the full set minus the board (lower case = blank) and its rack."""
    c = Counter(DIST)
    for ch in board:
        if ch.isupper():
            c[ch] -= 1
        elif ch.islower():
            c["?"] -= 1
    for ch in rack:
        c[ch] -= 1
    return [t for t, n in sorted(c.items()) for _ in range(n)]


def bootstrap(items, n=2000, seed=1):
    """Mean of the values with a 95% interval, resampling keys.  items: (key, value) pairs.
    Returns (mean, low, high, values, keys) or None when empty."""
    by = defaultdict(list)
    for k, v in items:
        by[k].append(v)
    keys = list(by)
    if not keys:
        return None
    tot = [sum(by[k]) for k in keys]
    cnt = [len(by[k]) for k in keys]
    est = sum(tot) / sum(cnt)
    rng = random.Random(seed)
    bs = []
    for _ in range(n):
        s = c = 0
        for _ in keys:
            j = rng.randrange(len(keys))
            s += tot[j]
            c += cnt[j]
        bs.append(s / c)
    bs.sort()
    return est, bs[int(0.025 * n)], bs[min(n - 1, int(0.975 * n))], sum(cnt), len(keys)


def decisions(paths):
    """Every move with an estimate, with what the analyses need."""
    rows = []
    for path in paths:
        log = os.path.basename(path)
        for g in games(path):
            for i, r in enumerate(g["record"]):
                e = estimate(r)
                if e is None:
                    continue
                rows.append(dict(log=log, pair=g["pair"], game=(log, g["pair"], g["a_first"]), i=i, p=r["p"],
                                 eng=e[0], w=e[1], bag=r["bag"], res=mover_result(g, r["p"]),
                                 empties=tiles_placed(r.get("mv")) >= r["bag"] > 0))
    return rows


def late(rows):
    return [x for x in rows if 2 <= x["bag"] <= 7 and 0.1 <= x["w"] <= 0.9]


def consecutive(rows):
    """For each late decision: (key, e(t) + opponent's e'(t+1) - 1) and (key, e(t) - own e(t+2), e(t) - result)."""
    idx = {(x["game"], x["i"]): x for x in rows}
    nxt, drop = [], []
    for x in late(rows):
        y = idx.get((x["game"], x["i"] + 1))
        z = idx.get((x["game"], x["i"] + 2))
        k = (x["log"], x["pair"])
        if y and y["p"] != x["p"]:
            nxt.append((x["eng"], k, x["w"] + y["w"] - 1))
        if z and z["p"] == x["p"]:
            drop.append((x["eng"], k, x["w"] - z["w"], x["w"] - x["res"]))
    return nxt, drop


def line(label, b, scale=100.0, fmt="+6.1f"):
    if b is None:
        return
    m, lo, hi, n, pairs = b
    print(f"  {label:50s} {m * scale:{fmt}} ({lo * scale:{fmt}} to {hi * scale:{fmt}})  n={n:4d}  pairs={pairs}")


def cmd_calib(a):
    rows = decisions(a.logs)
    L = late(rows)
    B = lambda sel: bootstrap([((x["log"], x["pair"]), x["w"] - x["res"]) for x in sel], a.bootstrap, a.seed)
    print("Late decisions (2-7 in the bag), own estimate 0.1-0.9: mean(estimate - result), points")
    with_macondo = {x["log"] for x in rows if x["eng"] == "macondo"}
    for eng in ("tilefish", "macondo"):
        E = [x for x in L if x["eng"] == eng]
        if not E:
            continue
        print(eng)
        line("all logs", B(E))
        if eng == "tilefish" and with_macondo:
            line("logs where Macondo's estimates exist (same games)", B([x for x in E if x["log"] in with_macondo]))
        line("the move empties the bag", B([x for x in E if x["empties"]]))
        line("the move leaves tiles in the bag", B([x for x in E if not x["empties"]]))
        for lo, hi in ((2, 3), (4, 5), (6, 7)):
            for emp in (True, False):
                line(f"bag {lo}-{hi}, {'empties the bag' if emp else 'leaves tiles'}",
                     B([x for x in E if lo <= x["bag"] <= hi and x["empties"] == emp]))
        for lo, hi in ((0.1, 0.3), (0.3, 0.5), (0.5, 0.7), (0.7, 0.9)):
            line(f"estimate {lo}-{hi}", B([x for x in E if lo <= x["w"] < hi or (hi == 0.9 and x["w"] == hi)]))
        line("middle game for comparison (8+ in the bag)",
             B([x for x in rows if x["eng"] == eng and x["bag"] >= 8 and 0.1 <= x["w"] <= 0.9]))
    nxt, drop = consecutive(rows)
    print("Consecutive estimates after a late decision (points; 0 when calibrated):")
    for eng in ("tilefish", "macondo"):
        line(f"{eng} moves, then the opponent: e(t) + e'(t+1) - 1",
             bootstrap([(k, v) for e, k, v in nxt if e == eng], a.bootstrap, a.seed))
        line(f"{eng}: e(t) - its own e(t+2)", bootstrap([(k, v) for e, k, v, _ in drop if e == eng], a.bootstrap, a.seed))
        line(f"{eng}: e(t) - result, the same decisions",
             bootstrap([(k, v) for e, k, _, v in drop if e == eng], a.bootstrap, a.seed))


def load_leaves(path):
    lv = {}
    with open(path, encoding="utf-8") as f:
        for ln in f:
            if ln.startswith("#") or not ln.strip():
                continue
            k, v = ln.split()
            lv[k] = float(v)
    return lv


def rack_quality(rack, lv):
    vals = [lv.get("".join(sorted(rack[:i] + rack[i + 1:]))) for i in range(len(rack))]
    vals = [v for v in vals if v is not None]
    return sum(vals) / len(vals) if vals else 0.0


def rack_rows(paths, lv):
    rows = []
    for path in paths:
        log = os.path.basename(path)
        for g in games(path):
            rec = g["record"]
            for i, r in enumerate(rec[:-1]):
                e = estimate(r)
                nxt = rec[i + 1]
                if (e is None or not (2 <= r["bag"] <= 7) or not (0.1 <= e[1] <= 0.9) or "cgp" not in r
                        or nxt.get("p") == r["p"] or "cgp" not in nxt):
                    continue
                board, racks = r["cgp"].split()[:2]
                opp = nxt["cgp"].split()[1].split("/")[0]
                pool = unseen(board, racks.split("/")[0])
                if len(opp) != 7 or Counter(opp) - Counter(pool) or len(pool) > 14:
                    continue
                qa = rack_quality(opp, lv)
                qs = [rack_quality("".join(pool[j] for j in c), lv) for c in itertools.combinations(range(len(pool)), 7)]
                pct = (sum(q < qa for q in qs) + 0.5 * sum(q == qa for q in qs)) / len(qs)
                rows.append(dict(key=(log, g["pair"]), eng=e[0], bag=r["bag"], dq=qa - sum(qs) / len(qs), pct=pct,
                                 over=e[1] - mover_result(g, r["p"])))
    return rows


def cmd_racks(a):
    rows = rack_rows(a.logs, load_leaves(a.leaves))
    B = lambda sel, f: bootstrap([(x["key"], f(x)) for x in sel], a.bootstrap, a.seed)
    print("Opponent's actual rack quality minus the uniform expectation (leave points), late decisions:")
    for eng in ("tilefish", "macondo"):
        E = [x for x in rows if x["eng"] == eng]
        if not E:
            continue
        line(f"{eng}, bag 2-7", B(E, lambda x: x["dq"]), 1.0, "+7.3f")
        for b in range(2, 8):
            line(f"  bag {b}", B([x for x in E if x["bag"] == b], lambda x: x["dq"]), 1.0, "+7.3f")
        line(f"{eng}: percentile of the actual rack (0.5 = typical)", B(E, lambda x: x["pct"]), 1.0, "7.3f")
        for lo, hi in ((0, 1 / 3), (1 / 3, 2 / 3), (2 / 3, 1.01)):
            line(f"{eng}: over-estimate, rack percentile {lo:.2f}-{min(hi, 1):.2f}",
                 B([x for x in E if lo <= x["pct"] < hi], lambda x: x["over"]))


def decided_rows(paths, by, lo, hi):
    rows = []
    for path in paths:
        log = os.path.basename(path)
        for g in games(path):
            at = None
            for r in g["record"]:
                e = estimate(r)
                if e is None or "cgp" not in r or (by == "a" and r["p"] != 0) or (by == "b" and r["p"] != 1):
                    continue
                wa = e[1] if r["p"] == 0 else 1 - e[1]
                if wa <= lo or wa >= hi:
                    at = (lead_of_a(r), wa >= hi)
                    break
            rows.append(dict(key=(log, g["pair"]), final=g["sa"] - g["sb"], at=at, res=result_a(g)))
    return rows


def cmd_decided(a):
    by = {"tilefish": "a", "macondo": "b", "a": "a", "b": "b", "either": "either"}[a.by]
    rows = decided_rows(a.logs, by, a.lo, a.hi)
    B = lambda vals: bootstrap(vals, a.bootstrap, a.seed)
    dec = [r for r in rows if r["at"]]
    print(f"{len(rows)} games; decided (A's chance by {a.by}'s estimate <= {a.lo} or >= {a.hi}) before the end: {len(dec)}")
    line("A's mean final margin", B([(r["key"], r["final"]) for r in rows]), 1.0)
    line("  margin when decided (the final margin if never)",
         B([(r["key"], r["at"][0] if r["at"] else r["final"]) for r in rows]), 1.0)
    line("  points gained after the game was decided", B([(r["key"], r["final"] - r["at"][0] if r["at"] else 0) for r in rows]), 1.0)
    for won in (True, False):
        s = [r for r in dec if r["at"][1] == won]
        if s:
            print(f"  decided {'for' if won else 'against'} A: {len(s)} games, A's score {sum(r['res'] for r in s) / len(s):.3f},"
                  f" points A gained afterwards {sum(r['final'] - r['at'][0] for r in s) / len(s):+.1f} a game")
    und = [r for r in rows if not r["at"]]
    if und:
        print(f"  never decided: {len(und)} games, A's score {sum(r['res'] for r in und) / len(und):.3f},"
              f" mean margin {sum(r['final'] for r in und) / len(und):+.1f}")
    print("A's share of the decisive games by final margin:")
    for lo, hi in ((1, 10), (11, 25), (26, 50), (51, 100), (101, 9999)):
        s = [r["final"] for r in rows if lo <= abs(r["final"]) <= hi]
        w = sum(m > 0 for m in s)
        print(f"  margin {lo:3d}-{hi:<4d} games {len(s):4d}  A wins {w:4d}  B wins {len(s) - w:4d}  A's share {w / max(1, len(s)):.3f}")


def late_leads(paths):
    out = []
    for path in paths:
        log = os.path.basename(path)
        for g in games(path):
            for r in g["record"]:
                if "cgp" in r and r.get("bag", 99) <= 7:
                    out.append(((log, g["pair"]), lead_of_a(r), result_a(g)))
                    break
    return out


def conversion(rows, lo, hi):
    t = [ra for _, lead, ra in rows if lo <= lead <= hi]
    m = [1 - ra for _, lead, ra in rows if lo <= -lead <= hi]
    return (sum(t) / len(t) if t else float("nan"), len(t), sum(m) / len(m) if m else float("nan"), len(m))


def cmd_convert(a):
    rows = late_leads(a.logs)
    keys = sorted({k for k, _, _ in rows})
    by = defaultdict(list)
    for x in rows:
        by[x[0]].append(x)
    rng = random.Random(a.seed)
    print(f"{len(rows)} games; A's lead when the bag first holds 7 or fewer; the leader's score")
    for lo, hi in ((1, 40), (41, 100), (101, 9999)):
        t, nt, m, nm = conversion(rows, lo, hi)
        bs = []
        for _ in range(a.bootstrap):
            s = [x for _ in keys for x in by[keys[rng.randrange(len(keys))]]]
            ta, _, mb, _ = conversion(s, lo, hi)
            bs.append(mb - ta)
        bs = sorted(v for v in bs if v == v)
        ci = f"({100 * bs[int(.025 * len(bs))]:+5.1f} to {100 * bs[min(len(bs) - 1, int(.975 * len(bs)))]:+5.1f})" if bs else ""
        print(f"  lead {lo}-{hi}: A converts {100 * t:5.1f}% (n={nt}), B {100 * m:5.1f}% (n={nm});"
              f" B minus A {100 * (m - t):+5.1f} {ci}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bootstrap", type=int, default=2000, help="bootstrap resamples (default 2000)")
    ap.add_argument("--seed", type=int, default=1, help="bootstrap seed (default 1)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("calib").add_argument("logs", nargs="+")
    r = sub.add_parser("racks")
    r.add_argument("logs", nargs="+")
    r.add_argument("--leaves", default=os.path.join(ROOT, "ENABLE.leaves"))
    d = sub.add_parser("decided")
    d.add_argument("logs", nargs="+")
    d.add_argument("--by", choices=("either", "tilefish", "macondo", "a", "b"), default="either",
                   help="whose estimates decide a game: A's (tilefish), B's (macondo) or either's")
    d.add_argument("--lo", type=float, default=0.01)
    d.add_argument("--hi", type=float, default=0.99)
    sub.add_parser("convert").add_argument("logs", nargs="+")
    a = ap.parse_args()
    {"calib": cmd_calib, "racks": cmd_racks, "decided": cmd_decided, "convert": cmd_convert}[a.cmd](a)


if __name__ == "__main__":
    sys.exit(main())
