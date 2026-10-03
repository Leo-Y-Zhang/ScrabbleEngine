#!/usr/bin/env python3
"""Reanalyse the decisions of logged games with a longer search (a "judge").

    # judge one shard of the sample (the engine sees exactly what the mover saw)
    python3 tools/reanalyse.py judge LOG.jsonl.gz --per-stratum 6 --sample-seed 1 \\
        --shard 0 --shards 10 --engine "proto:./tilefish --lexicon CSW24.kwg --threads 4 --quiet" \\
        --init "player champion" --movetime 60000 --include --out judge-0.jsonl
    # tabulate
    python3 tools/reanalyse.py report judge-*.jsonl

The sample: games of engine A's log in four strata (A lost by 1-50, won by 1-50, lost by
51+, won by 51+), --per-stratum of each, drawn with --sample-seed; every move of both
engines in those games is a decision.  The judge gets the position the mover saw (the
CGP in the record: the opponent's rack is hidden while the bag has tiles) and nothing
else: not the opponent's rack, not the tiles drawn later, not the result.

A judge is a stronger search, not an oracle.  With --include (Tilefish judges), the
judge also simulates the move that was played, so the report can give the judge's own
estimate of what the played move gave up; for a Macondo judge only agreement is known.
Agreement is measured on normalised moves (tiles played through written as '.').
"""

import argparse
import collections
import gzip
import json
import os
import random
import re
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))


def load_games(path):
    with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
        for line in f:
            line = line.strip()
            if line:
                d = json.loads(line)
                if "meta" not in d and "record" in d:
                    yield d


STRATA = [("A lost by 1-50", lambda m: -50 <= m < 0), ("A won by 1-50", lambda m: 0 < m <= 50),
          ("A lost by 51+", lambda m: m < -50), ("A won by 51+", lambda m: m > 50)]


def sample(games, per_stratum, seed):
    rng = random.Random(seed)
    out = []
    for name, f in STRATA:
        sel = sorted((g for g in games if f(g["sa"] - g["sb"])), key=lambda g: (g["pair"], not g["a_first"]))
        for g in rng.sample(sel, min(per_stratum, len(sel))):
            out.append((name, g))
    return out


def norm(move):
    """'8D WO(R)D' and '8D WO.D' -> '8D WO.D'; exchanges sorted; pass."""
    t = move.strip().split()
    if not t:
        return ""
    if t[0].lower() in ("pass",):
        return "pass"
    if t[0].lower() in ("exch", "exchange", "-"):
        return "exch " + "".join(sorted(t[1].upper())) if len(t) > 1 else "exch"
    word = re.sub(r"\(([^)]*)\)", lambda m: "." * len(m.group(1)), t[1]) if len(t) > 1 else ""
    return t[0].upper() + " " + word


def judge(args):
    import referee
    games = list(load_games(args.log))
    picked = sample(games, args.per_stratum, args.sample_seed)
    decisions = []
    for stratum, g in picked:
        for i, r in enumerate(g["record"]):
            if "mv" in r and r.get("p") in (0, 1):
                decisions.append((stratum, g, i, r))
    mine = [d for k, d in enumerate(decisions) if k % args.shards == args.shard]
    print("%d games sampled, %d decisions, this shard: %d" % (len(picked), len(decisions), len(mine)), flush=True)
    eng = referee.Engine(args.engine, "judge", args.init)
    with open(args.out, "a") as out:
        for n, (stratum, g, i, r) in enumerate(mine):
            extra = (" include " + r["mv"]) if args.include and r["mv"] != "pass" else ""
            t0 = time.time()
            try:
                best = eng.best(r["cgp"], args.movetime, extra=extra)
            except Exception as ex:
                print("judge failed on pair %d move %d: %s" % (g["pair"], i, ex), flush=True)
                eng.kill()
                eng.start()
                continue
            rec = dict(log=os.path.basename(args.log), pair=g["pair"], a_first=g["a_first"], idx=i, p=r["p"],
                       bag=r["bag"], stratum=stratum, margin=g["sa"] - g["sb"], cgp=r["cgp"], played=r["mv"],
                       played_info=r.get("info"), judge=args.name, judge_ms=args.movetime, best=best,
                       agree=norm(best) == norm(r["mv"]), info=eng.info, t=round(time.time() - t0, 2))
            out.write(json.dumps(rec) + "\n")
            out.flush()
            if n % 10 == 0:
                print("%d/%d  pair %d move %d: played %s, judge %s" % (n + 1, len(mine), g["pair"], i, r["mv"], best),
                      flush=True)
    eng.close()


def phase(bag):
    return "opening 61+" if bag > 60 else "middle 15-60" if bag >= 15 else "approach 8-14" if bag >= 8 else \
        "late 2-7" if bag >= 2 else "last tile 1" if bag == 1 else "endgame 0"


def loss_of(rec):
    """The judge's own estimate of what the played move gave up (win probability for
    simulated positions, points for endgames); None when it cannot say."""
    i = rec.get("info")
    if not isinstance(i, dict):
        return None
    if rec["agree"]:
        return 0.0
    inc = i.get("inc")
    if i.get("phase") in ("sim", "playout") and inc and inc.get("post") is not None:
        top = i["top"][0].get("post")
        return None if top is None else max(0.0, top - inc["post"])
    if i.get("phase") == "endgame" and inc and inc.get("value") is not None:
        return max(0.0, i["value"] - inc["value"])
    return None


def report(args):
    recs = []
    for path in args.files:
        with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
            recs += [json.loads(l) for l in f if l.strip()]
    if not recs:
        raise SystemExit("no judged decisions")
    judges = sorted(set(r["judge"] for r in recs))
    for jn in judges:
        rs = [r for r in recs if r["judge"] == jn]
        print("=" * 100)
        print("judge %s, %d decisions in %d games, %.0f s a decision" % (
            jn, len(rs), len(set((r["log"], r["pair"], r["a_first"]) for r in rs)), sum(r["judge_ms"] for r in rs) / len(rs) / 1000))
        print("%-16s %-10s %6s %8s %12s %14s %14s" % ("phase", "mover", "n", "agree", "loss known", "mean loss", "losses > thr"))
        for ph in ["opening 61+", "middle 15-60", "approach 8-14", "late 2-7", "last tile 1", "endgame 0",
                   "all but endgame"]:
            for who, name in ((0, "Tilefish"), (1, "opponent")):
                sel = [r for r in rs if r["p"] == who and (phase(r["bag"]) == ph or (ph == "all but endgame" and r["bag"] > 0))]
                if not sel:
                    continue
                ls = [loss_of(r) for r in sel]
                known = [x for x in ls if x is not None]
                eg = ph == "endgame 0"
                thr = 5.0 if eg else 0.02
                unit = "pts" if eg else "win"
                print("%-16s %-10s %6d %7.1f%% %12d %10.4f %3s %10d (>%s)" % (
                    ph, name, len(sel), 100 * sum(r["agree"] for r in sel) / len(sel), len(known),
                    sum(known) / len(known) if known else float("nan"), unit, sum(1 for x in known if x > thr),
                    "%g %s" % (thr, unit)))
        print()
        print("By stratum (non-endgame decisions; mean judged loss in win probability, agreement):")
        for st, _ in STRATA:
            for who, name in ((0, "Tilefish"), (1, "opponent")):
                sel = [r for r in rs if r["stratum"] == st and r["p"] == who and r["bag"] > 0]
                if not sel:
                    continue
                known = [x for x in (loss_of(r) for r in sel) if x is not None]
                print("  %-15s %-9s n=%4d  agree %5.1f%%  mean loss %.4f (n=%d)" % (
                    st, name, len(sel), 100 * sum(r["agree"] for r in sel) / len(sel),
                    sum(known) / len(known) if known else float("nan"), len(known)))
        worst = sorted((r for r in rs if (loss_of(r) or 0) > 0 and r["bag"] > 0), key=lambda r: -loss_of(r))[:args.worst]
        if worst:
            print()
            print("Largest judged losses (non-endgame):")
            for r in worst:
                print("  %s pair %d %s move %d, bag %d, %s played %-18s judge %-18s loss %.3f  [%s]" % (
                    r["stratum"], r["pair"], "A first" if r["a_first"] else "B first", r["idx"], r["bag"],
                    "Tilefish" if r["p"] == 0 else "opponent", r["played"], r["best"], loss_of(r), r["cgp"]))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    j = sub.add_parser("judge")
    j.add_argument("log")
    j.add_argument("--per-stratum", type=int, default=6)
    j.add_argument("--sample-seed", type=int, default=1)
    j.add_argument("--shard", type=int, default=0)
    j.add_argument("--shards", type=int, default=1)
    j.add_argument("--engine", required=True)
    j.add_argument("--init", default="")
    j.add_argument("--name", default="judge")
    j.add_argument("--movetime", type=int, default=60000)
    j.add_argument("--include", action="store_true", help="ask the judge to simulate the played move too (Tilefish)")
    j.add_argument("--out", required=True)
    r = sub.add_parser("report")
    r.add_argument("files", nargs="+")
    r.add_argument("--worst", type=int, default=15)
    args = ap.parse_args()
    judge(args) if args.cmd == "judge" else report(args)


if __name__ == "__main__":
    main()
