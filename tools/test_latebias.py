"""Checks of the estimate/result analysis; run with python3 -m pytest tools/test_latebias.py."""

import gzip
import json

import pytest

from tools import latebias

EMPTY = "/".join(["15"] * 15)


def tf(w, phase="playout"):
    return {"phase": phase, "best": {"w": w}}


def mac(pct):
    return {"details": f"1)  4K VAG ({pct}%)  2)  4K VUG (12.0%)"}


def rec(p, bag, info=None, mv="8H AB", scores="0/0", rack="ABCDEFG/"):
    r = {"p": p, "bag": bag, "cgp": f"{EMPTY} {rack} {scores} 0", "mv": mv}
    if info is not None:
        r["info"] = info
    return r


def write_log(path, games_):
    with gzip.open(path, "wt", encoding="utf-8") as f:
        f.write(json.dumps({"meta": {"seed": 1}}) + "\n")
        for g in games_:
            f.write(json.dumps(g) + "\n")
    return str(path)


def game(pair, a_first, sa, sb, record):
    return {"pair": pair, "a_first": a_first, "sa": sa, "sb": sb, "record": record}


def test_estimates_of_both_engines():
    assert latebias.estimate(rec(1, 5, mac("93.0"))) == ("macondo", pytest.approx(0.93))
    assert latebias.estimate(rec(1, 0, mac("93.0"))) is None  # Macondo's endgame line is a spread
    assert latebias.estimate(rec(0, 5, tf(0.25))) == ("tilefish", 0.25)
    assert latebias.estimate(rec(0, 1, tf(0.5, "peg"))) == ("tilefish", 0.5)
    assert latebias.estimate(rec(0, 30, tf(0.5, "sim"))) == ("tilefish", 0.5)
    assert latebias.estimate(rec(0, 0, {"phase": "endgame", "value": 40})) is None
    assert latebias.estimate(rec(0, 5)) is None
    assert latebias.estimate(rec(1, 5, {"details": "no percentage"})) is None


def test_result_from_the_movers_side():
    g = game(0, True, 400, 300, [])
    assert latebias.mover_result(g, 0) == 1.0 and latebias.mover_result(g, 1) == 0.0
    g = game(0, True, 300, 400, [])
    assert latebias.mover_result(g, 0) == 0.0 and latebias.mover_result(g, 1) == 1.0
    g = game(0, True, 350, 350, [])
    assert latebias.mover_result(g, 0) == latebias.mover_result(g, 1) == 0.5


@pytest.mark.parametrize("mv,n", [("A4 (J)IBED", 4), ("2G A.NULATE", 7), ("11E Q.N", 2), ("8D ZOEAE", 5),
                                  ("-ABC", 0), ("-", 0), ("exch ABC", 0), ("", 0), (None, 0)])
def test_tiles_placed(mv, n):
    assert latebias.tiles_placed(mv) == n


def test_unseen_counts_blanks_on_the_board():
    board = "aBC12/" + "/".join(["15"] * 14)  # 'a' is a blank played as A
    pool = latebias.unseen(board, "AE?")
    full = sum(latebias.DIST.values())
    assert len(pool) == full - 3 - 3
    assert pool.count("?") == 0  # one blank on the board, one on the rack
    assert pool.count("A") == 8 and pool.count("B") == 1 and pool.count("E") == 11


def test_lead_of_a_from_either_movers_cgp():
    assert latebias.lead_of_a(rec(0, 5, scores="300/250")) == 50
    assert latebias.lead_of_a(rec(1, 5, scores="300/250")) == -50


def test_bootstrap_mean_and_reproducible():
    items = [("a", 1.0), ("a", 3.0), ("b", 5.0), ("c", -1.0)]
    b1 = latebias.bootstrap(items, 500, 7)
    b2 = latebias.bootstrap(items, 500, 7)
    assert b1 == b2
    assert b1[0] == pytest.approx(2.0) and b1[3] == 4 and b1[4] == 3
    assert b1[1] <= b1[0] <= b1[2]
    assert latebias.bootstrap([], 10, 1) is None


def test_consecutive_estimates_stay_inside_one_game(tmp_path):
    # The two games of a deal pair share pair and move numbers; the pairing must not mix them.
    g1 = game(3, True, 400, 300, [rec(0, 5, tf(0.6)), rec(1, 3, mac("70.0")), rec(0, 2, tf(0.5))])
    g2 = game(3, False, 300, 400, [rec(0, 5, tf(0.2)), rec(1, 3, mac("10.0")), rec(0, 2, tf(0.1))])
    rows = latebias.decisions([write_log(tmp_path / "x.jsonl.gz", [g1, g2])])
    nxt, drop = latebias.consecutive(rows)
    tf_next = sorted(round(v, 6) for e, _, v in nxt if e == "tilefish")
    assert tf_next == [round(0.2 + 0.1 - 1, 6), round(0.6 + 0.7 - 1, 6)]
    mac_next = sorted(round(v, 6) for e, _, v in nxt if e == "macondo")
    assert mac_next == [round(0.1 + 0.1 - 1, 6), round(0.7 + 0.5 - 1, 6)]
    tf_drop = sorted((round(a, 6), round(b, 6)) for e, _, a, b in drop if e == "tilefish")
    assert tf_drop == sorted([(round(0.2 - 0.1, 6), round(0.2 - 0.0, 6)), (round(0.6 - 0.5, 6), round(0.6 - 1.0, 6))])


def test_late_selection_and_empties(tmp_path):
    g = game(0, True, 400, 300, [rec(0, 9, tf(0.5), mv="8H ABCD"), rec(1, 5, mac("50.0"), mv="8H ABCDE"),
                                 rec(0, 2, tf(0.95)), rec(1, 2, mac("40.0"), mv="8H A"), rec(0, 1, tf(0.5))])
    rows = latebias.decisions([write_log(tmp_path / "x.jsonl.gz", [g])])
    late = latebias.late(rows)
    assert [(x["eng"], x["bag"], x["empties"]) for x in late] == [("macondo", 5, True), ("macondo", 2, False)]


def test_rack_quality_and_rows(tmp_path):
    lv = {"ABCDEF": 6.0, "ABCDEG": 0.0}
    assert latebias.rack_quality("ABCDEFG", lv) == pytest.approx(3.0)  # only two of seven leaves known
    assert latebias.rack_quality("QQQQQQQ", lv) == 0.0


def test_decided_split_by_whose_estimate(tmp_path):
    # Game 1: A's own estimate decides it at +50 (A's 0.995); A wins by 100, so +50 after.
    g1 = game(0, True, 400, 300, [rec(0, 20, tf(0.995), scores="100/50"), rec(1, 15, mac("50.0"))])
    # Game 2: B's estimate (0.5%, so A's chance 0.995) decides it with A ahead 260-200; A wins by 10.
    g2 = game(1, True, 300, 290, [rec(0, 20, tf(0.5)), rec(1, 10, mac("0.5"), scores="200/260")])
    path = write_log(tmp_path / "x.jsonl.gz", [g1, g2])
    either = latebias.decided_rows([path], "either", 0.01, 0.99)
    assert [r["at"] for r in either] == [(50, True), (60, True)]
    assert [r["final"] - r["at"][0] for r in either] == [50, -50]
    only_a = latebias.decided_rows([path], "a", 0.01, 0.99)
    assert [r["at"] for r in only_a] == [(50, True), None]
    only_b = latebias.decided_rows([path], "b", 0.01, 0.99)
    assert [r["at"] for r in only_b] == [None, (60, True)]


def test_conversion_of_late_leads(tmp_path):
    games_ = [game(0, True, 400, 300, [rec(0, 9), rec(1, 6, scores="200/250")]),   # A leads 50, wins
              game(1, True, 300, 400, [rec(0, 7, scores="300/250")]),             # A leads 50, loses
              game(2, True, 300, 400, [rec(0, 5, scores="250/300")])]             # B leads 50, wins
    rows = latebias.late_leads([write_log(tmp_path / "x.jsonl.gz", games_)])
    assert [lead for _, lead, _ in rows] == [50, 50, -50]
    t, nt, m, nm = latebias.conversion(rows, 41, 100)
    assert (t, nt, m, nm) == (0.5, 2, 1.0, 1)
