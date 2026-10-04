#!/usr/bin/env python3
"""Extract late-game starts from the referee's recorded games.

    python3 tools/positions.py extract LOG.jsonl.gz [LOG ...] --until-bag 7 \\
        --split dev --out positions.jsonl

The first non-prefix turn with 1..--until-bag tiles left supplies the board,
mover's rack and scores.  The next turn supplies the opponent's unchanged rack;
subtracting the board and both racks from the tile distribution recovers the bag.
Positions are in mover/opponent order, ready for tools/referee.py --positions.
Pairs are split by sha256("seed:pair") modulo 3 into dev, val and test, even when
the same deal appears in several logs.  --split all writes every valid position.
"""

import argparse
import gzip
import hashlib
import json
import os
import sys

if __package__:
    from .referee import DIST, parse_board
else:
    from referee import DIST, parse_board

SPLITS = ("dev", "val", "test")


def split_for(seed, pair):
    h = hashlib.sha256(("%d:%d" % (seed, pair)).encode()).hexdigest()
    return SPLITS[int(h, 16) % 3]


def extract(paths, until_bag):
    """Return sorted positions and the number of games with inconsistent tiles."""
    positions, skipped = [], 0
    for path in paths:
        seed = None
        with (gzip.open(path, "rt") if path.endswith(".gz") else open(path)) as f:
            for line in f:
                if not line.strip():
                    continue
                game = json.loads(line)
                if "meta" in game:
                    seed = game["meta"]["seed"]
                    continue
                if seed is None:
                    raise ValueError("log is missing its seed header")
                record = game["record"]
                for i, rec in enumerate(record[:-1]):
                    if (rec.get("p") not in (0, 1) or not 0 < rec.get("bag", 0) <= until_bag
                            or "cgp" not in rec or "cgp" not in record[i + 1]):
                        continue
                    board, racks, scores, zeros = rec["cgp"].split()
                    racks = [racks.split("/")[0], record[i + 1]["cgp"].split()[1].split("/")[0]]
                    bag = [t for t, n in DIST.items() for _ in range(n)]
                    try:
                        for row in parse_board(board):
                            for t in row:
                                if t is not None:
                                    bag.remove("?" if t.islower() else t)
                        for rack in racks:
                            for t in rack:
                                bag.remove(t)
                        if len(bag) != rec["bag"]:
                            raise ValueError("bag count mismatch")
                    except ValueError:
                        skipped += 1
                        break
                    positions.append(dict(src=os.path.basename(path), seed=seed, pair=game["pair"],
                                          a_first=game["a_first"], board=board, racks=racks,
                                          scores=[int(s) for s in scores.split("/")], zeros=int(zeros),
                                          bag="".join(sorted(bag)), mover_engine=rec["p"]))
                    break
    positions.sort(key=lambda p: (p["src"], p["seed"], p["pair"], p["a_first"]))
    return positions, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="command", required=True)
    ex = sub.add_parser("extract", help="extract one late-game position per recorded game")
    ex.add_argument("logs", nargs="+", help="referee JSON-lines logs, optionally gzip-compressed")
    ex.add_argument("--until-bag", type=int, default=7, help="take the first eligible turn with this many bag tiles or fewer")
    ex.add_argument("--split", choices=SPLITS + ("all",), default="all", help="deal-pair split to write")
    ex.add_argument("--out", required=True, help="write positions as JSON lines here")
    args = ap.parse_args()
    positions, skipped = extract(args.logs, args.until_bag)
    counts = {s: sum(split_for(p["seed"], p["pair"]) == s for p in positions) for s in SPLITS}
    print("positions: " + ", ".join("%s=%d" % (s, counts[s]) for s in SPLITS), file=sys.stderr)
    print("skipped %d games with inconsistent board/racks/bag tiles" % skipped, file=sys.stderr)
    with open(args.out, "w") as f:
        for p in positions:
            if args.split == "all" or split_for(p["seed"], p["pair"]) == args.split:
                f.write(json.dumps(p) + "\n")


if __name__ == "__main__":
    main()
