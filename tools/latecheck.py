#!/usr/bin/env python3
"""Re-search an engine's late-game decisions with several settings, side by side.

    python3 tools/latecheck.py run LOG.jsonl.gz [LOG ...] --split dev --bags 2-7 \\
        --engine "proto:./tilefish --lexicon CSW24.kwg --threads 1 --quiet" \\
        --spec base=champion --spec keep=champion:keep=0.015 --movetime 20000 \\
        --parallel 8 --out late-dev.jsonl
    python3 tools/latecheck.py report late-dev.jsonl --base base

The positions are engine A's decisions in the logged games with 2 to 7 tiles in the bag,
exactly as the mover saw them (the CGP in the record: the opponent's rack hidden).  Each
setting searches each position once.  The report compares, per setting: the chosen move's
estimated winning chance against the logged game's result (calibration, with intervals
from a bootstrap over deal pairs), how often the settings choose different moves, and
how the estimate for the same position shifts between settings.  Splits are by deal, as
in tools/keepfit.py: sha256("seed:pair") mod 3, 0 = dev, 1 = val, 2 = test.
"""
import argparse
import collections
import gzip
import hashlib
import json
import multiprocessing as mp
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import referee  # noqa: E402

SPLITS = {"dev": 0, "val": 1, "test": 2}


def positions(paths, split, lo, hi):
    out = []
    for path in paths:
        seed = None
        with gzip.open(path, "rt", encoding="utf-8") as f:
            for line in f:
                d = json.loads(line)
                if "meta" in d:
                    seed = d["meta"].get("seed")
                    continue
                if "record" not in d or seed is None:
                    continue
                if split != "all" and int(hashlib.sha256(f"{seed}:{d['pair']}".encode()).hexdigest(), 16) % 3 != SPLITS[split]:
                    continue
                res = 1.0 if d["sa"] > d["sb"] else 0.5 if d["sa"] == d["sb"] else 0.0
                for i, r in enumerate(d["record"]):
                    if r.get("p") != 0 or "cgp" not in r or not (lo <= r.get("bag", -1) <= hi):
                        continue
                    out.append(dict(src=os.path.basename(path), seed=seed, pair=d["pair"], a_first=d["a_first"],
                                    move_no=i, bag=r["bag"], cgp=r["cgp"], played=r["mv"], result=res))
    return out


def work(job):
    pos, specs, engine, movetime = job
    rows = []
    for name, spec in specs:
        eng = WORKER.setdefault(name, None)
        if eng is None:
            eng = referee.Engine(engine, name, "player " + spec)
            WORKER[name] = eng
        text = eng.best(pos["cgp"], movetime)
        info = eng.info if isinstance(eng.info, dict) else {}
        rows.append(dict(pos, setting=name, spec=spec, move=text, info=info))
    return rows


WORKER = {}


def cmd_run(a):
    specs = [tuple(s.split("=", 1)) for s in a.spec]
    lo, hi = (int(x) for x in a.bags.split("-"))
    ps = positions(a.logs, a.split, lo, hi)
    if a.limit:
        ps = ps[: a.limit]
    print("%d positions, %d settings, %d ms a search" % (len(ps), len(specs), a.movetime), file=sys.stderr)
    referee.WORDS.add("")  # the engines are not refereed here
    jobs = [(p, specs, a.engine, a.movetime) for p in ps]
    done = 0
    with open(a.out, "a") as f, mp.Pool(a.parallel) as pool:
        for rows in pool.imap_unordered(work, jobs):
            for r in rows:
                f.write(json.dumps(r) + "\n")
            f.flush()
            done += 1
            if done % 10 == 0:
                print("%d/%d" % (done, len(ps)), file=sys.stderr)


def boot(groups, stat, B=4000, seed=1):
    keys = list(groups)
    rng = random.Random(seed)
    vals = []
    for _ in range(B):
        pick = [groups[rng.choice(keys)] for _ in keys]
        vals.append(stat([x for g in pick for x in g]))
    vals.sort()
    return vals[int(0.025 * B)], vals[int(0.975 * B) - 1]


def cmd_report(a):
    rows = [json.loads(l) for f in a.files for l in open(f) if l.strip()]
    by = collections.defaultdict(dict)
    for r in rows:
        key = (r["src"], r["seed"], r["pair"], r["a_first"], r["move_no"])
        by[key][r["setting"]] = r
    settings = sorted({r["setting"] for r in rows})
    keys = [k for k, v in by.items() if all(s in v for s in settings)]
    print("%d positions searched by all of: %s" % (len(keys), ", ".join(settings)))
    for s in settings:
        g = collections.defaultdict(list)
        for k in keys:
            r = by[k][s]
            w = (r["info"].get("best") or {}).get("w")
            if w is None or not (a.lo <= w <= a.hi):
                continue
            g[(k[1], k[2])].append(w - r["result"])
        n = sum(len(v) for v in g.values())
        if not n:
            continue
        m = sum(sum(v) for v in g.values()) / n
        lo, hi = boot(g, lambda xs: sum(xs) / len(xs))
        print("%-8s estimate - result, estimates in [%.1f, %.1f]: %+.1f points (95%% %+.1f to %+.1f), %d moves, %d pairs"
              % (s, a.lo, a.hi, 100 * m, 100 * lo, 100 * hi, n, len(g)))
    if a.base in settings:
        for s in settings:
            if s == a.base:
                continue
            same = sum(1 for k in keys if referee_norm(by[k][s]["move"]) == referee_norm(by[k][a.base]["move"]))
            g = collections.defaultdict(list)
            for k in keys:
                wb = (by[k][a.base]["info"].get("best") or {}).get("w")
                ws = (by[k][s]["info"].get("best") or {}).get("w")
                if wb is not None and ws is not None:
                    g[(k[1], k[2])].append(ws - wb)
            n = sum(len(v) for v in g.values())
            m = sum(sum(v) for v in g.values()) / max(1, n)
            lo, hi = boot(g, lambda xs: sum(xs) / len(xs)) if n else (float("nan"), float("nan"))
            print("%-8s vs %s: same move in %d of %d (%.1f%%); estimate shift %+.1f points (95%% %+.1f to %+.1f)"
                  % (s, a.base, same, len(keys), 100 * same / max(1, len(keys)), 100 * m, 100 * lo, 100 * hi))


def referee_norm(move):
    return " ".join(move.split()).replace("(", ".").replace(")", "").upper()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    r = sub.add_parser("run")
    r.add_argument("logs", nargs="+")
    r.add_argument("--split", default="dev", choices=["dev", "val", "test", "all"])
    r.add_argument("--bags", default="2-7")
    r.add_argument("--engine", required=True)
    r.add_argument("--spec", action="append", required=True, help="NAME=SETTING, repeatable")
    r.add_argument("--movetime", type=int, default=20000)
    r.add_argument("--parallel", type=int, default=1)
    r.add_argument("--limit", type=int, default=0)
    r.add_argument("--out", required=True)
    p = sub.add_parser("report")
    p.add_argument("files", nargs="+")
    p.add_argument("--base", default="base")
    p.add_argument("--lo", type=float, default=0.1)
    p.add_argument("--hi", type=float, default=0.9)
    a = ap.parse_args()
    (cmd_run if a.cmd == "run" else cmd_report)(a)


if __name__ == "__main__":
    main()
