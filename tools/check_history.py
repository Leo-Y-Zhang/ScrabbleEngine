#!/usr/bin/env python3
"""Checks a referee log played with --a-history: engine A inferred the opponent's rack at
every simulated decision where it could (the opponent's last move a play of 1 to 6 tiles)
and at no other, and engine B, which got no history, never inferred.

    python3 tools/check_history.py LOG.jsonl
"""
import json
import re
import sys


def placed(mv):
    parts = (mv or "").split()
    if len(parts) < 2 or parts[0].startswith("-") or parts[0].lower().startswith("exch"):
        return 0
    return sum(c.isalpha() for c in re.sub(r"\([^)]*\)", "", parts[1]).replace(".", ""))


def check(path):
    expected = ran = wrong = b_inferred = 0
    with open(path, encoding="utf-8") as f:
        for line in f:
            d = json.loads(line)
            if "record" not in d:
                continue
            assert not d["errors"], d["errors"]
            rec = d["record"]
            for i, r in enumerate(rec):
                info = r.get("info") or {}
                if info.get("phase") != "sim" or i == 0:
                    continue
                inferred = info.get("inf", -1) > 0
                if r["p"] == 1:
                    b_inferred += inferred
                    continue
                prev = rec[i - 1]
                exp = prev.get("p") == 1 and 1 <= placed(prev.get("mv")) <= 6
                expected += exp
                ran += exp and inferred
                wrong += exp != inferred
    print(f"A: inference expected at {expected} decisions, ran at {ran}, mismatches {wrong}; B inferred {b_inferred} times")
    return expected >= 5 and ran == expected and wrong == 0 and b_inferred == 0


if __name__ == "__main__":
    sys.exit(0 if check(sys.argv[1]) else 1)
