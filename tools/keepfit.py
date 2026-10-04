#!/usr/bin/env python3
"""Fit and test a model of the opponent's rack: which unseen tiles a strong player holds.

    python3 tools/keepfit.py LOG.jsonl.gz [LOG ...] --leaves single_leaves.txt [--bags 1-7]

At each decision with tiles in the bag, the mover cannot see the opponent's rack; the
next record (the opponent's move) shows it.  A uniform draw from the unseen tiles is
what Tilefish assumes.  The models compared here weight each tile on the rack by odds
w[L] (Fisher's noncentral hypergeometric distribution, as in tilefish.cpp's RackPrior):

  uniform     w = 1 for every letter
  leave       w[L] = exp(lam * v[L]), v = one-tile leave values (one parameter)
  letters     a free log-odds per letter with a ridge penalty (27 parameters)

Decisions are split by deal (seed and pair number, so both games of a pair, and the
same deal replayed in another log, stay together): sha256("seed:pair") mod 3 gives
0 = development, 1 = validation, 2 = test.  Parameters are fitted on development,
models compared on validation; test is reported only with --test.
"""
import argparse
import collections
import gzip
import hashlib
import json
import math

import numpy as np

LET = "?ABCDEFGHIJKLMNOPQRSTUVWXYZ"
DIST = dict(zip(LET, [2, 9, 2, 2, 4, 12, 2, 3, 2, 9, 1, 1, 4, 2, 6, 8, 2, 1, 6, 4, 6, 4, 2, 2, 1, 2, 1]))


def split_of(seed, pair):
    return int(hashlib.sha256(f"{seed}:{pair}".encode()).hexdigest(), 16) % 3


def counts(s):
    c = collections.Counter(s)
    return np.array([c.get(L, 0) for L in LET], dtype=float)


def board_counts(rows):
    c = collections.Counter("?" if ch.islower() else ch for ch in rows if ch.isalpha())
    return np.array([c.get(L, 0) for L in LET], dtype=float)


def decisions(paths, bag_lo, bag_hi):
    """(split, unseen counts, opponent's rack counts, bag) for every usable decision."""
    total = np.array([DIST[L] for L in LET], dtype=float)
    for path in paths:
        seed = None
        with gzip.open(path, "rt", encoding="utf-8") as f:
            for line in f:
                d = json.loads(line)
                if "meta" in d:
                    seed = d["meta"].get("seed", seed)
                    continue
                rec = d.get("record")
                if not rec or seed is None:
                    continue
                sp = split_of(seed, d["pair"])
                for r, nxt in zip(rec, rec[1:]):
                    if "cgp" not in r or "cgp" not in nxt or r.get("p") == nxt.get("p") or r.get("p") == "prefix":
                        continue
                    f0 = r["cgp"].split()
                    unseen = total - board_counts(f0[0]) - counts(f0[1].split("/")[0])
                    opp = counts(nxt["cgp"].split()[1].split("/")[0])
                    bag = int(unseen.sum() - 7)
                    if not (bag_lo <= bag <= bag_hi) or opp.sum() != 7 or (opp > unseen).any() or (unseen < 0).any():
                        continue
                    yield sp, unseen, opp, bag


CH = np.array([[math.comb(a, b) for b in range(8)] for a in range(13)], dtype=float)


def loglik_grad(theta, data):
    """Fisher log-likelihood of the observed racks and its gradient in theta = log w."""
    w = np.exp(theta)
    ll, grad = 0.0, np.zeros_like(theta)
    for unseen, opp in data:
        n = int(opp.sum())
        # P_L(x) = sum_j C(U_L, j) w_L^j x^j, truncated at degree n
        polys = []
        for L in range(27):
            u = int(unseen[L])
            polys.append(np.array([CH[u, j] * w[L] ** j if j <= u else 0.0 for j in range(n + 1)]))
        pre = [np.zeros(n + 1)]
        pre[0][0] = 1
        for p in polys:
            pre.append(np.convolve(pre[-1], p)[: n + 1])
        suf = [None] * 28
        suf[27] = np.zeros(n + 1)
        suf[27][0] = 1
        for L in range(26, -1, -1):
            suf[L] = np.convolve(suf[L + 1], polys[L])[: n + 1]
        G = pre[27][n]
        ll += sum(math.log(CH[int(unseen[L]), int(opp[L])]) + opp[L] * theta[L] for L in range(27)) - math.log(G)
        for L in range(27):
            rest = np.convolve(pre[L], suf[L + 1])[: n + 1]
            jp = np.arange(n + 1) * polys[L]
            ev = np.convolve(jp, rest)[n] / G
            grad[L] += opp[L] - ev
    return ll, grad


def fit_letters(data, s, iters=300, lr=0.5):
    theta = np.zeros(27)
    for _ in range(iters):
        ll, g = loglik_grad(theta, data)
        g = g / len(data) - theta / (s * s * len(data))
        theta += lr * g
    return theta


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("logs", nargs="+")
    ap.add_argument("--leaves", required=True, help="one-tile leave values: lines 'L value'")
    ap.add_argument("--bags", default="1-7")
    ap.add_argument("--test", action="store_true", help="also report the test split")
    a = ap.parse_args()
    lo, hi = (int(x) for x in a.bags.split("-"))
    v = np.zeros(27)
    for line in open(a.leaves):
        t = line.split()
        if len(t) == 2 and t[0] in LET:
            v[LET.index(t[0])] = float(t[1])
    rows = list(decisions(a.logs, lo, hi))
    split = {k: [(u, o) for s, u, o, _ in rows if s == k] for k in range(3)}
    print("decisions with bag %d-%d: development %d, validation %d, test %d"
          % (lo, hi, len(split[0]), len(split[1]), len(split[2])))

    def mean_ll(theta, k):
        return loglik_grad(theta, split[k])[0] / len(split[k])

    models = {"uniform": np.zeros(27)}
    best_lam, best = 0.0, -1e9
    for lam in np.arange(0.0, 0.0605, 0.0025):
        ll = loglik_grad(lam * v, split[0])[0]
        if ll > best:
            best_lam, best = lam, ll
    models["leave lam=%.4f" % best_lam] = best_lam * v
    for s in (0.1, 0.2, 0.3, 0.5):
        models["letters s=%.1f" % s] = fit_letters(split[0], s)
    print("model                     dev LL/decision   validation LL/decision   (higher is better)")
    for name, th in models.items():
        print("%-24s  %15.4f   %22.4f" % (name, mean_ll(th, 0), mean_ll(th, 1)))
    if a.test:
        print("test split:")
        for name, th in models.items():
            print("%-24s  %15.4f" % (name, mean_ll(th, 2)))
    print()
    print("fitted odds (letters s=0.3):  " + "  ".join("%s %.2f" % (L, math.exp(t))
                                                    for L, t in zip(LET, models["letters s=0.3"])))
    print("leave model odds:             " + "  ".join("%s %.2f" % (L, math.exp(best_lam * x)) for L, x in zip(LET, v)))


if __name__ == "__main__":
    main()
