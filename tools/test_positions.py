"""Recorded-start checks; run with python3 -m pytest tools/test_positions.py."""

import gzip
import hashlib
import json
import random
import subprocess
import sys
import textwrap
from pathlib import Path
from types import SimpleNamespace

import pytest

from tools import handover, positions, referee


# 79 board tiles, two seven-tile racks and seven bag tiles.  'a' is a blank.
BOARD = ("aAAAAAAAABCDDDE/EEEEEEEEEFGGHII/IIIIIILLLMNNNNO/OOOOOOPPRRRRRSS/"
         "SSTTTTTUUUUVVWW/XYYZ11/15/15/15/15/15/15/15/15/15")
RACKS = ["BCDEFGH", "JKLMNOQ"]
BAG = "?AEINRT"
SEED, PAIR = 42, 7


class StubEngine:
    history = True
    info = {"stub": True}
    startup_s = 0

    def __init__(self, name, moves=()):
        self.name = name
        self.moves = iter(moves)
        self.seen = []

    def cpu(self):
        return 0.0

    def best(self, cgp, ms, hist=None):
        self.seen.append((cgp, hist))
        return next(self.moves, "pass")


def cgp(board=BOARD, rack=RACKS[0], scores="190/209", zeros=1):
    return "%s %s/ %s %d" % (board, rack, scores, zeros)


def write_log(path, pair=PAIR):
    early = BOARD[:1] + "1" + BOARD[2:]  # one fewer A on the board: eight bag tiles
    games = []
    for a_first in (True, False):
        mover = 1 if a_first else 0
        games.append(dict(pair=pair, a_first=a_first, sa=300, sb=350, record=[
            dict(p="prefix", bag=8, cgp=cgp(early), mv="pass"),
            dict(p=1 - mover, bag=8, cgp=cgp(early, RACKS[1], "201/190"), mv="8H A"),
            dict(p=mover, bag=7, cgp=cgp(), mv="pass"),
            dict(p=1 - mover, bag=7, cgp=cgp(rack=RACKS[1], scores="209/190", zeros=2), mv="pass"),
            dict(p=mover, bag=7, cgp=cgp(zeros=3), mv="pass"),
            dict(p=mover, end="(BCDEFGH)", sc=-19),
        ]))
    with gzip.open(path, "wt") as f:
        for entry in [dict(meta=dict(seed=SEED))] + games:
            f.write(json.dumps(entry) + "\n")
    return games


def start_position():
    return dict(src="synthetic.jsonl.gz", seed=SEED, pair=PAIR, a_first=True,
                board=BOARD, racks=RACKS, scores=[190, 209], zeros=1, bag=BAG, mover_engine=1)


def run_extract(paths, out, split="all"):
    return subprocess.run([sys.executable, str(Path(positions.__file__)), "extract"] +
                          [str(p) for p in paths] + ["--until-bag", "7", "--split", split, "--out", str(out)],
                          capture_output=True, text=True, check=True)


def test_extract_first_record_and_bag(tmp_path):
    log = tmp_path / "synthetic.jsonl.gz"
    write_log(log)
    out, skipped = positions.extract([str(log)], 7)
    assert skipped == 0
    assert [p["a_first"] for p in out] == [False, True]
    for p in out:
        assert p == dict(start_position(), a_first=p["a_first"], mover_engine=1 if p["a_first"] else 0)
        assert len(p["bag"]) == 7


def test_splits_keep_pairs_and_duplicate_deals_together(tmp_path):
    logs = [tmp_path / name for name in ("z.jsonl.gz", "a.jsonl.gz")]
    for log in logs:
        write_log(log)
    expected = positions.SPLITS[int(hashlib.sha256(b"42:7").hexdigest(), 16) % 3]
    assert positions.split_for(SEED, PAIR) == expected
    for split in positions.SPLITS + ("all",):
        out = tmp_path / (split + ".jsonl")
        result = run_extract(logs, out, split)
        loaded = [json.loads(line) for line in out.read_text().splitlines()]
        assert len(loaded) == (4 if split in (expected, "all") else 0)
        assert [(p["src"], p["a_first"]) for p in loaded] == (
            [("a.jsonl.gz", False), ("a.jsonl.gz", True), ("z.jsonl.gz", False), ("z.jsonl.gz", True)]
            if loaded else [])
        assert "%s=4" % expected in result.stderr
        assert "skipped 0 games" in result.stderr


@pytest.mark.parametrize("bad_bag,bad_rack", [(6, RACKS[0]), (7, "ZZZZZZZ")])
def test_inconsistent_tiles_are_skipped(tmp_path, bad_bag, bad_rack):
    log = tmp_path / "bad.jsonl.gz"
    games = write_log(log)
    for game in games:
        game["record"][2].update(bag=bad_bag, cgp=cgp(rack=bad_rack))
    with gzip.open(log, "wt") as f:
        for entry in [dict(meta=dict(seed=SEED))] + games:
            f.write(json.dumps(entry) + "\n")
    result = run_extract([log], tmp_path / "out.jsonl")
    assert (tmp_path / "out.jsonl").read_text() == ""
    assert "skipped 2 games" in result.stderr


def test_prefix_empty_bag_and_final_entries_are_not_starts(tmp_path):
    log = tmp_path / "filters.jsonl.gz"
    game = dict(pair=PAIR, a_first=True, record=[
        dict(p="prefix", bag=7, cgp=cgp()),
        dict(p=0, bag=0, cgp=cgp()),
        dict(p=1, bag=7, cgp=cgp()),
        dict(p=0, end="(XYZ)", sc=-20),
    ])
    with gzip.open(log, "wt") as f:
        for entry in [dict(meta=dict(seed=SEED)), game]:
            f.write(json.dumps(entry) + "\n")
    assert positions.extract([str(log)], 7) == ([], 0)


def test_game_reproduces_position_and_hides_opponent():
    pos = start_position()
    seed = 123 * 1000003 + 2
    g = referee.Game({"AA"}, seed, pos)
    assert g.board[0][0] == "a"
    assert g.board[5][4:] == [None] * 11
    assert g.cgp(False) == cgp()
    assert g.cgp(True) == cgp().replace(RACKS[0] + "/", "/".join(RACKS))
    assert g.racks == [list(r) for r in RACKS]
    assert g.scores == [190, 209]
    assert g.zeros == 1 and g.turn == 0 and not g.over
    expected = list(BAG)
    random.Random(seed).shuffle(expected)
    assert g.bag == expected
    assert g.moves == []
    g.racks[0].pop()
    g.scores[0] += 1
    assert pos == start_position()  # games of a pair cannot mutate the shared start


def test_load_positions_uses_absolute_pair_index_and_checks_length(tmp_path):
    out = tmp_path / "starts.jsonl"
    out.write_text("\n".join(json.dumps(dict(start_position(), scores=[i, i + 1])) for i in range(3)) + "\n")
    loaded = referee.load_positions(str(out), 1, 2)
    assert loaded[1]["scores"] == [1, 2]
    with pytest.raises(ValueError, match="file has 3 positions; need 4"):
        referee.load_positions(str(out), 2, 2)


def test_play_pair_handover_history_and_summary(monkeypatch):
    args = SimpleNamespace(seed=17, positions="starts.jsonl", position_data=[None, start_position()],
                           prefix="", prefix_until_bag=7, history_lexicon="", movetime=1000, gcg_dir="",
                           a_name="A", b_name="B", single=False)
    engines = [StubEngine("A"), StubEngine("B")]
    monkeypatch.setattr(referee, "WORKER", dict(args=args, engines=engines, prefix=None))
    first, second = referee.run_pair(1)
    assert (first["ha"], first["hb"], first["hbag"]) == (190, 209, 7)
    assert (second["ha"], second["hb"], second["hbag"]) == (209, 190, 7)
    assert first["record"][0]["p"] == 0 and second["record"][0]["p"] == 1
    assert first["record"][0]["cgp"] == second["record"][0]["cgp"] == cgp()
    for game in (first, second):
        assert game["errors"] == [] and game["bingos"] == [0, 0]
        assert sum(game["moves"]) == 5  # five passes reach six zero-score turns
        assert len([r for r in game["record"] if "end" in r]) == 2
        assert all(r["info"] == {"stub": True} and "cpu" in r and "t" in r
                   for r in game["record"] if "cgp" in r)
    assert "recorded positions" in referee.summarize([[first, second]], args)
    assert "gains %+.2f" % ((handover.gain(first) + handover.gain(second)) / 2) in referee.summarize([[first, second]], args)
    assert ">" not in engines[0].seen[0][1]  # no moves before the starting position
    assert len([line for line in engines[1].seen[0][1].split(" | ") if line.startswith(">")]) == 1
    assert engines[1].seen[0][1].split(" | ")[-2].endswith("+0 190")


def test_bingo_errors_and_empty_bag_visibility(monkeypatch):
    board = referee.parse_board(BOARD)
    # Put seven tiles at 6E, extending XYYZ and making seven cross words.
    for c, t in enumerate(RACKS[0], 4):
        board[5][c] = t
    words = {"XYYZ" + RACKS[0]} | {"".join(board[r][c] for r in range(6)) for c in range(4, 11)}
    args = SimpleNamespace(seed=17, positions="starts.jsonl", position_data=[start_position()],
                           history_lexicon="", movetime=1000, gcg_dir="")
    engines = [StubEngine("A", ["6A (XYYZ)" + RACKS[0], "bad move"]), StubEngine("B")]
    monkeypatch.setattr(referee, "WORDS", words)
    monkeypatch.setattr(referee, "WORKER", dict(args=args, engines=engines, prefix=None))
    game = referee.play_game(0, True)
    assert game["bingos"] == [1, 0]
    assert game["record"][0]["sc"] >= 50
    assert game["record"][1]["bag"] == 0
    assert game["record"][1]["cgp"].split()[1].split("/")[1]  # opponent rack now visible
    assert len(game["errors"]) == 1
    assert game["record"][2]["mv"] == "pass" and "err" in game["record"][2]


def test_prefix_still_hands_over_identically(monkeypatch):
    args = SimpleNamespace(seed=17, positions="", prefix="stub", prefix_until_bag=85,
                           history_lexicon="", movetime=1000, gcg_dir="", single=False,
                           a_name="A", b_name="B")
    g = referee.Game(set(), args.seed * 1000003)
    word = referee.rack_str(g.racks[0]).replace("?", "")[:2]
    engines = [StubEngine("A"), StubEngine("B")]
    prefix = StubEngine("prefix", ["8H " + word, "8H " + word])
    monkeypatch.setattr(referee, "WORDS", {word})
    monkeypatch.setattr(referee, "WORKER", dict(args=args, engines=engines, prefix=prefix))
    first, second = referee.run_pair(0)
    assert first["ha"] == second["hb"] > 0
    assert first["hb"] == second["ha"] == 0
    for game in (first, second):
        assert game["hbag"] == 84 and game["record"][0]["p"] == "prefix"
        assert game["errors"] == []
    assert first["record"][1]["cgp"] == second["record"][1]["cgp"]
    assert "from 85 tiles in the bag" in referee.summarize([[first, second]], args)


def test_referee_cli_with_positions_and_stub_protocol(tmp_path):
    out = tmp_path / "starts.jsonl"
    out.write_text(json.dumps(start_position()) + "\n")
    lex = tmp_path / "words.txt"
    lex.write_text("AA\n")
    stub = tmp_path / "stub.py"
    stub.write_text("import sys\nfor line in sys.stdin:\n"
                    "    if line.strip() == 'isready': print('readyok', flush=True)\n"
                    "    elif line.startswith('go '): print('bestmove pass', flush=True)\n"
                    "    elif line.strip() == 'quit': break\n")
    spec = "proto:%s %s" % (sys.executable, stub)
    log = tmp_path / "match.jsonl"
    result = subprocess.run([sys.executable, referee.__file__, "--lexicon", str(lex), "--a", spec, "--b", spec,
                             "--games", "1", "--positions", str(out), "--a-history", "--b-history",
                             "--log", str(log)], capture_output=True, text=True, check=True)
    entries = [json.loads(line) for line in log.read_text().splitlines()]
    assert entries[0]["meta"]["positions"] == out.name
    assert len(entries) == 3
    assert all(g["hbag"] == 7 and g["errors"] == [] for g in entries[1:])
    assert "recorded positions" in result.stdout


def test_run_meta_records_basename_and_hash(tmp_path):
    out = tmp_path / "starts.jsonl"
    out.write_text(json.dumps(start_position()) + "\n")
    lex = tmp_path / "words.txt"
    lex.write_text("AA\n")
    args = SimpleNamespace(positions=str(out), lexicon=str(lex), a="", b="", a_name="A", b_name="B",
                           a_init="", b_init="", a_info="", b_info="", a_history=False, b_history=False,
                           prefix="", prefix_init="", prefix_until_bag=7, games=1, first_pair=0, single=False,
                           movetime=1000, parallel=1, seed=1)
    meta = referee.run_meta(args)
    assert meta["positions"] == out.name
    assert meta["positions_sha256"] == hashlib.sha256(out.read_bytes()).hexdigest()


def test_prefix_and_positions_are_mutually_exclusive():
    result = subprocess.run([sys.executable, referee.__file__, "--lexicon", "unused", "--a", "unused",
                             "--b", "unused", "--prefix", "unused", "--positions", "unused"],
                            capture_output=True, text=True)
    assert result.returncode == 2
    assert "not allowed with argument --prefix" in result.stderr


def test_workflow_play_accepts_positions_and_rejects_prefix():
    workflow = Path(__file__).resolve().parents[1] / ".github/workflows/experiment.yml"
    play = workflow.read_text().split("      - name: Play\n        run: |\n", 1)[1].split("      - uses:", 1)[0]
    play = textwrap.dedent(play)
    subprocess.run(["bash", "-n"], input=play, text=True, check=True)
    result = subprocess.run(["bash"], input=play, text=True, capture_output=True,
                            env=dict(HISTORY="none", POSITIONS="starts.jsonl", PREFIX_BAG="7"))
    assert result.returncode == 1
    assert "positions and prefix_until_bag cannot be combined" in result.stderr
    # Capture the referee's arguments without starting engines or compiling builds.
    stubs = 'python3() { printf "%s\\n" "$@"; }\nnproc() { echo 1; }\n'
    result = subprocess.run(["bash"], input=stubs + play, text=True, capture_output=True, check=True,
                            env=dict(HISTORY="none", POSITIONS="starts with spaces.jsonl", PREFIX_BAG="",
                                     THREADS="1", SHARD="1", PAIRS="2"))
    argv = result.stdout.splitlines()
    assert argv[argv.index("--positions") + 1] == "starts with spaces.jsonl"
    assert argv[argv.index("--first-pair") + 1] == "2"
