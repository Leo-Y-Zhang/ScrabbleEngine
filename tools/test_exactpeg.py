"""Two-tile reference checks; run with python3 -m pytest tools/test_exactpeg.py."""

import collections
import json
import shlex
import subprocess
import sys

import pytest

from tools import exactpeg

referee = exactpeg.referee
RACK = "BCDEFGH"
UNSEEN = "?AAEINRTZ"


def synthetic():
    # 84 board tiles, a seven-tile rack, and nine unseen physical tiles.
    pool = collections.Counter(referee.DIST)
    pool.subtract(RACK + UNSEEN)
    tiles = ["a" if t == "?" else t for t in pool.elements()]
    board = [tiles[i:i + 15] for i in range(0, 75, 15)] + [tiles[75:] + [None] * 6]
    board += [[None] * 15 for _ in range(9)]
    game = referee.Game(set(), 0)
    game.board = board
    game.racks = [list(RACK), []]
    game.scores = [100, 110]
    game.zeros = 3
    cross = "".join(board[r][8] for r in range(6)).upper() + "B"
    words = {"BC", cross}
    # 7I BC: B is on a double-letter square; C is on an ordinary square.
    score = 6 + 3 + 6 + sum(referee.tile_value(board[r][8]) for r in range(6))
    row = dict(src="synthetic", seed=4, pair=2, a_first=True, move_no=20,
               setting="base", bag=2, cgp=game.cgp(False), result=0.5,
               info={"top": [{"m": "7I BC", "w": 0.7}]})
    return row, words, score


def stub(tmp_path, value, solved=True, phase="endgame"):
    path = tmp_path / "stub.py"
    seen = tmp_path / "seen.jsonl"
    info = json.dumps(dict(phase=phase, value=value, solved=solved))
    path.write_text("import json, sys\n"
                    "for line in sys.stdin:\n"
                    "    if line.strip() == 'isready': print('readyok', flush=True)\n"
                    "    elif line.startswith('position cgp '):\n"
                    "        with open(%r, 'a') as f: f.write(json.dumps(line[13:].strip()) + '\\n')\n"
                    "    elif line.startswith('go '):\n"
                    "        print(%r, flush=True)\n"
                    "        print('bestmove pass', flush=True)\n"
                    "    elif line.strip() == 'quit': break\n" % (str(seen), "info " + info))
    return "proto:%s %s" % (shlex.quote(sys.executable), shlex.quote(str(path))), seen


def run_cli(tmp_path, rows, value, solved=True, phase="endgame", settings="", limit=0, spawn=False, words=None):
    if words is None:
        _, words, _ = synthetic()
    lexicon = tmp_path / "words.txt"
    lexicon.write_text("\n".join(words) + "\n")
    runs = tmp_path / "runs.jsonl"
    runs.write_text("\n".join(json.dumps(r) for r in rows) + "\n")
    spec, seen = stub(tmp_path, value, solved, phase)
    out = tmp_path / "exact.jsonl"
    args = ["run", str(runs), "--words", str(lexicon), "--engine", spec,
            "--parallel", "2", "--egtime", "1", "--out", str(out),
            "--limit", str(limit), "--settings", settings]
    if spawn:
        code = ("import multiprocessing as mp, sys; mp.set_start_method('spawn'); "
                "from tools import exactpeg; sys.argv = ['exactpeg'] + sys.argv[1:]; exactpeg.main()")
        command = [sys.executable, "-c", code] + args
    else:
        command = [sys.executable, exactpeg.__file__] + args
    subprocess.run(command, capture_output=True, text=True, check=True)
    return [json.loads(l) for l in out.read_text().splitlines()], seen


def test_splits_and_unseen_weights():
    row, _, _ = synthetic()
    _, unseen = exactpeg.position(row["cgp"])
    assert +unseen == collections.Counter(UNSEEN)
    draws = {"".join(sorted(draw.elements())): weight for draw, weight in exactpeg.splits(unseen)}
    assert len(draws) == 29  # eight tile types: 28 mixed pairs and AA
    assert sum(draws.values()) == pytest.approx(1)
    assert draws["AA"] == pytest.approx(1 / 36)
    assert draws["?A"] == pytest.approx(2 / 36)
    assert draws["?Z"] == pytest.approx(1 / 36)
    assert "??" not in draws


@pytest.mark.parametrize("final,win,spawn", [(1, 1, False), (0, 0.5, True), (-1, 0, False)])
def test_final_spread_and_referee_draws(tmp_path, final, win, spawn):
    row, _, score = synthetic()
    after = -10 + score
    rows, seen = run_cli(tmp_path, [row], after - final, spawn=spawn)
    assert len(rows) == 1
    result = rows[0]
    assert result["exact_w"] == pytest.approx(win)
    assert result["exact_spread"] == pytest.approx(final - (-10))
    assert result["splits"] == result["solved"] == 29
    assert result["top_index"] == 0 and result["w"] == 0.7 and result["result"] == 0.5
    assert all(result[k] == row[k] for k in exactpeg.IDENT)
    cgps = [json.loads(l) for l in seen.read_text().splitlines()]
    assert len(cgps) == 29
    observed = set()
    for cgp in cgps:
        board, racks, scores, zeros = cgp.split()
        opp, mover = racks.split("/")
        assert len(opp) == len(mover) == 7
        assert scores == "110/%d" % (100 + score) and zeros == "0"
        remaining = collections.Counter(mover)
        remaining.subtract("DEFGH")
        draw = +remaining
        assert collections.Counter(opp) + draw == collections.Counter(UNSEEN)
        observed.add("".join(sorted(draw.elements())))
        assert referee.parse_board(board)[6][8:10] == ["B", "C"]
    assert len(observed) == 29


def test_invalid_moves_are_recorded_without_searching(tmp_path):
    row, _, _ = synthetic()
    row["info"]["top"] = [{"m": "7I BD", "w": 0.8}, {"m": "7I ZZ", "w": 0.6}]
    rows, seen = run_cli(tmp_path, [row], 0)
    assert len(rows) == 2
    assert all(r["invalid"] and r["error"] and r["solved"] == 0 for r in rows)
    assert all(r["exact_w"] is None and r["exact_spread"] is None for r in rows)
    assert not seen.exists()


def test_filters_limit_and_original_chosen_rank(tmp_path):
    row, _, score = synthetic()
    row["info"]["top"] = [{"m": "7I B", "w": 0.8}, {"m": "7I BC", "w": 0.7},
                          {"m": "pass", "w": 0.1}, {"m": "exch BC", "w": 0.2}]
    rows, _ = run_cli(tmp_path, [dict(row, bag=3), dict(row, setting="k6"), row, row],
                      score - 10, settings="base", limit=1)
    assert len(rows) == 1 and rows[0]["top_index"] == 1 and rows[0]["input_line"] == 3


@pytest.mark.parametrize("through", ["parentheses", "dots", "letters"])
def test_played_through_tiles_do_not_count_as_rack_tiles(tmp_path, through):
    row, _, _ = synthetic()
    board = referee.parse_board(row["cgp"].split()[0])
    prefix = "".join(board[r][8] for r in range(6))
    token = {"parentheses": "(" + prefix + ")", "dots": "." * 6, "letters": prefix}[through]
    row["info"]["top"] = [{"m": "I1 " + token + "B", "w": 0.8},
                          {"m": "I1 " + token + "BC", "w": 0.7}]
    words = {prefix.upper() + "B", prefix.upper() + "BC"}
    rows, _ = run_cli(tmp_path, [row], 0, words=words)
    assert len(rows) == 1 and rows[0]["top_index"] == 1
    assert rows[0]["solved"] == rows[0]["splits"] == 29


@pytest.mark.parametrize("solved,phase", [(False, "endgame"), (True, "playout")])
def test_engine_info_requires_solved_endgames(tmp_path, capsys, solved, phase):
    row, _, _ = synthetic()
    rows, _ = run_cli(tmp_path, [row], 0, solved=solved, phase=phase)
    assert rows[0]["solved"] == 0
    exactpeg.report(rows)
    output = capsys.readouterr().out
    assert "0 positions included; excluded 1 positions for unsolved splits" in output
    assert "chosen: count 0" in output and "all: count 0" in output


def test_report_filters_entire_positions_and_counts_ties(capsys):
    def row(line, index, w, exact, solved=29, invalid=False):
        return dict(input_file="runs", input_line=line, setting="base", seed=10, pair=line,
                    top_index=index, w=w, exact_w=exact, splits=29, solved=solved, invalid=invalid)

    rows = [row(1, 0, 0.8, 0.6), row(1, 1, 0.4, 0.6),  # exact-best tie
            row(2, 0, 1.0, 0.0), row(2, 1, 1.0, 0.0, solved=28),
            row(3, 1, 0.7, 0.6),  # chosen move was ineligible
            row(4, 0, 1.0, None, solved=0, invalid=True)]
    exactpeg.report(rows)
    output = capsys.readouterr().out
    assert "2 positions included; excluded 1 positions for unsolved splits, 1 invalid positions" in output
    assert "chosen: count 1, mean w - exact_w +0.200000" in output
    assert "all: count 3, mean w - exact_w +0.033333" in output
    assert "mean absolute difference 0.166667" in output
    assert "chosen exact-best agreement: 1/1 (1.000000); 1 positions without" in output


def test_bootstrap_resamples_pairs_as_clusters():
    # Correlated positive/negative moves cancel within each deal pair.
    assert exactpeg.boot({(1, 1): [0.8, -0.8], (1, 2): [0.2, -0.2]}) == (0.0, 0.0)
