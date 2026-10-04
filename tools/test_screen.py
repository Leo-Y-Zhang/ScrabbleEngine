"""Checks of the screen decision tool; run with python3 -m pytest tools/test_screen.py."""

import gzip
import json
import sys

import pytest

from tools import screen


def write_log(path, seed, games):
    with gzip.open(path, "wt", encoding="utf-8") as f:
        f.write(json.dumps({"meta": {"seed": seed}}) + "\n")
        for g in games:
            f.write(json.dumps(g) + "\n")
    return str(path)


def game(pair, a_first, sa, sb, cgp="15/15 ABCDEFG/ 0/0 0"):
    return {"pair": pair, "a_first": a_first, "sa": sa, "sb": sb, "errors": [],
            "record": [{"p": 0 if a_first else 1, "bag": 86, "cgp": cgp, "mv": "8H AB"}]}


def run(monkeypatch, capsys, *argv):
    monkeypatch.setattr(sys, "argv", ["screen.py", *argv])
    code = screen.main()
    return code, capsys.readouterr().out


def test_pair_values_count_draws_half_and_need_both_games():
    pairs = {0: {True: game(0, True, 400, 300), False: game(0, False, 350, 350)},
             1: {True: game(1, True, 300, 400)}}
    assert screen.pair_values(pairs) == {0: (0.75, 50.0)}


def test_bootstrap_is_reproducible_and_centred():
    v = [1.0, 0.0, 0.5, 1.0]
    assert screen.boot(v, 500, 3) == screen.boot(v, 500, 3)
    m, lo, hi = screen.boot(v, 500, 3)
    assert m == pytest.approx(0.625) and lo <= m <= hi


def test_paired_difference_and_rules(tmp_path, monkeypatch, capsys):
    # Candidate: wins both games of pair 0, splits pair 1.  Control: splits pair 0, loses pair 1.
    cand = write_log(tmp_path / "c.jsonl.gz", 9200, [game(0, True, 400, 300), game(0, False, 400, 300),
                                                    game(1, True, 400, 300), game(1, False, 300, 400)])
    ctrl = write_log(tmp_path / "k.jsonl.gz", 9200, [game(0, True, 400, 300), game(0, False, 300, 400),
                                                    game(1, True, 300, 400), game(1, False, 300, 400)])
    code, out = run(monkeypatch, capsys, cand, "--control", ctrl, "--min-score", "0.75", "--bootstrap", "200")
    assert code == 0
    assert "A's score   75.00%" in out
    assert "score 25.00%" in out                       # the control on the same pairs
    assert "score +50.00 points" in out                # (1 - 0.5 + 0.5 - 0) / 2
    assert "score >= 75.00%: met" in out and "PASSES" in out
    code, out = run(monkeypatch, capsys, cand, "--min-score", "0.75", "--strict", "--bootstrap", "200")
    assert "score > 75.00%: NOT met" in out and "FAILS" in out
    code, out = run(monkeypatch, capsys, cand, "--min-spread", "100", "--bootstrap", "200")
    assert "spread > +100.0: NOT met" in out and "FAILS" in out


def test_refuses_different_deals(tmp_path, monkeypatch, capsys):
    cand = write_log(tmp_path / "c.jsonl.gz", 9200, [game(0, True, 400, 300), game(0, False, 400, 300)])
    other_seed = write_log(tmp_path / "s.jsonl.gz", 9201, [game(0, True, 400, 300), game(0, False, 400, 300)])
    other_tiles = write_log(tmp_path / "t.jsonl.gz", 9200, [game(0, True, 400, 300, cgp="15/15 HIJKLMN/ 0/0 0"),
                                                            game(0, False, 400, 300)])
    with pytest.raises(SystemExit, match="seed"):
        run(monkeypatch, capsys, cand, "--control", other_seed)
    with pytest.raises(SystemExit, match="first positions differ"):
        run(monkeypatch, capsys, cand, "--control", other_tiles)
