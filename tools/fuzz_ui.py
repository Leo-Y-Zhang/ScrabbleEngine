#!/usr/bin/env python3
"""Fuzz the engine's `ui` commands (the ones the browser page sends) under the sanitizers.

    g++ -O1 -g -std=c++17 -pthread -fsanitize=address,undefined -fno-sanitize-recover=all \
        -fno-omit-frame-pointer tilefish.cpp -o tilefish_san
    python3 tools/fuzz_ui.py ./tilefish_san ENABLE.txt [SECONDS=60] [SEED=1] [OUTDIR=fuzz-out]

Drives one engine over stdin (`--quiet`) with games played through legal, mutated and
random moves (`ui move`, `force`, `check`, `judge`, `bot`), reviews and hints, take-backs,
positions (`ui cgp`, mutated from the engine's own `cgpout`) and game records (`ui import`,
mutated from its own `gcg`).  A command is followed by an unknown one as a marker, so each
answer is read in full; an answer that is not JSON, a crash, a hang (3 minutes) or any
AddressSanitizer or UBSan report fails the run.  OUTDIR keeps the commands sent
(commands.txt), which replay the failure: `./tilefish_san --quiet --lexicon ENABLE.txt
--threads 2 < OUTDIR/commands.txt` (the last record imported is OUTDIR/fuzz.gcg).  At the
end it prints, per command, how many of those sent were accepted.
"""
import collections, json, os, random, select, subprocess, sys, time

if len(sys.argv) < 3:
    sys.exit(__doc__)
BIN, LEX = sys.argv[1], sys.argv[2]
SECS = float(sys.argv[3]) if len(sys.argv) > 3 else 60.0
SEED = int(sys.argv[4]) if len(sys.argv) > 4 else 1
OUT = sys.argv[5] if len(sys.argv) > 5 else "fuzz-out"
rnd = random.Random(SEED)
STATS = collections.Counter()
LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
ODD = ["", " ", "(", ")", "((", "?", "*", ".", "-", "#", "0", "99", "-1", "Z", "a", "é", "\t", "\\", '"', "%s", ";", "16",
       "0A", "A0", "15O", "O15", "P16"]


class Engine:
    def __init__(self):
        env = dict(os.environ, ASAN_OPTIONS="detect_leaks=1:abort_on_error=1", UBSAN_OPTIONS="print_stacktrace=1:halt_on_error=1")
        self.p = subprocess.Popen([BIN, "--quiet", "--lexicon", LEX, "--threads", "2"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=open(os.path.join(OUT, "stderr.txt"), "wb"), env=env)
        self.buf = b""
        self.n = 0
        self.mine = True  # the seated side is to move
        self.seat = 0
        self.log = open(os.path.join(OUT, "commands.txt"), "w")
        self.read_until(b"ready", 300)

    def read_until(self, token, timeout):
        end = time.time() + timeout
        while token not in self.buf:
            left = end - time.time()
            if left <= 0:
                raise TimeoutError("no answer in %d s" % timeout)
            if select.select([self.p.stdout], [], [], left)[0]:
                chunk = os.read(self.p.stdout.fileno(), 65536)
                if not chunk:
                    raise EOFError("the engine exited (code %s)" % self.p.wait())
                self.buf += chunk
        i = self.buf.index(token)
        out, self.buf = self.buf[:i], self.buf[i + len(token):]
        return out

    def cmd(self, c):
        self.n += 1
        mark = "zzmark%d" % self.n
        self.log.write(c + "\n")
        self.log.flush()
        self.p.stdin.write((c + "\n" + mark + "\n").encode("utf-8", "surrogateescape"))
        self.p.stdin.flush()
        out = self.read_until(("unknown command '%s'" % mark).encode(), 180).decode("utf-8", "replace")
        js = None
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("{"):
                try:
                    js = json.loads(line)
                except ValueError:
                    raise AssertionError("not JSON: " + line[:300])
        if js is not None:
            st = js.get("state")
            if isinstance(st, dict) and "yourTurn" in st:
                self.mine = st["yourTurn"]
                self.seat = st.get("seat", self.seat)
            words = c.split()
            STATS[(words[1] if len(words) > 1 else c, bool(js.get("ok")))] += 1
        return js


def mutate(s):
    s = list(s)
    for _ in range(rnd.randint(1, 3)):
        k = rnd.random()
        if k < 0.3 and s:
            s[rnd.randrange(len(s))] = rnd.choice(LETTERS + LETTERS.lower() + "?()0123456789 .")
        elif k < 0.5 and s:
            del s[rnd.randrange(len(s))]
        elif k < 0.75:
            s.insert(rnd.randint(0, len(s)), rnd.choice(ODD + list(LETTERS)))
        elif k < 0.85:
            s = s + list(rnd.choice(LETTERS) * rnd.randint(1, 20))
        else:
            s = s[: rnd.randint(0, len(s))]
    return "".join(s)


def coord():
    r, c = rnd.randint(-1, 16), rnd.randint(-1, 16)
    col = chr(ord("A") + c) if 0 <= c < 26 else rnd.choice(["", "@", "["])
    return str(r + 1) + col if rnd.random() < 0.5 else col + str(r + 1)


def garbage_move():
    k = rnd.random()
    if k < 0.3:
        return coord() + " " + "".join(rnd.choice(LETTERS + "?()" + LETTERS.lower()) for _ in range(rnd.randint(0, 17)))
    if k < 0.5:
        return "exch " + "".join(rnd.choice(LETTERS + "?") for _ in range(rnd.randint(0, 9)))
    if k < 0.6:
        return rnd.choice(["pass", "-", "exch", "exch 0", "exch 7", "exch -1", "exch 99999999999", "PASS", "-  -"])
    return "".join(rnd.choice(ODD + list(LETTERS)) for _ in range(rnd.randint(0, 12)))


def mutate_lines(text):
    lines = text.splitlines()
    for _ in range(rnd.randint(0, 4)):
        if lines:
            i = rnd.randrange(len(lines))
            x = rnd.random()
            head, colon, rest = lines[i].partition(": ")
            if x < 0.25 and head.startswith(">") and rest:
                # the rack field: left out, or other tiles (the record still reads)
                t = rest.split(" ", 1)
                if len(t) == 2:
                    other = "".join(rnd.choice(LETTERS + "?") for _ in range(rnd.randint(1, 7)))
                    lines[i] = head + colon + (t[1] if rnd.random() < 0.5 else other + " " + t[1])
            elif x < 0.5:
                lines[i] = mutate(lines[i])
            elif x < 0.6:
                del lines[i]
            elif x < 0.8:
                lines.insert(i, lines[rnd.randrange(len(lines))])
            else:
                lines[i] = lines[i][: rnd.randint(0, len(lines[i]))]
    return "\n".join(lines) + "\n"


def run(e, t0):
    legal, cgp, gcg, nmoves = [], None, None, 0
    prev = None  # (move number, legal moves) of the position before the last move played

    def refresh():
        r = e.cmd("ui moves 30")
        return [m["move"] for m in r["moves"]] if r and r.get("ok") else []

    def num():  # a move number: mostly one of the game's, sometimes anything
        if rnd.random() < 0.75:
            return rnd.randint(0, max(0, nmoves - 1))
        return rnd.choice([nmoves, nmoves + 1, -1, -5, 2**31, 2**63, 10**30, "x", "", "1e9", "0.5", "-0"])

    e.cmd("ui new first %d" % rnd.randint(0, 2**63))
    while time.time() - t0 < SECS:
        k = rnd.random()
        if k < 0.10:
            r = e.cmd("ui moves %s" % rnd.choice([30, 30, 5, 1, 0, -3, 100000, "abc", ""]))
            if r and r.get("ok"):
                legal = [m["move"] for m in r["moves"]]
        elif k < 0.55:
            pick = rnd.random()
            mv = rnd.choice(legal) if legal and pick < 0.7 else mutate(rnd.choice(legal)) if legal and pick < 0.9 else garbage_move()
            verb = rnd.choice(["move", "move", "move", "check", "judge", "force"] if e.mine else ["force"] * 5 + ["move", "judge"])
            if verb == "judge" and prev and rnd.random() < 0.8:
                jm = rnd.choice(prev[1]) if rnd.random() < 0.8 else mutate(rnd.choice(prev[1]))
                c = "ui judge %d %s %s" % (prev[0], jm, rnd.choice(["0", "0.02", "0.02", ""]))
            elif verb == "judge":
                c = "ui judge %s %s %s" % (num(), mv, rnd.choice(["0", "0.02", "", "-1", "x"]))
            else:
                c = "ui %s %s" % (verb, mv)
            r = e.cmd(c)
            if verb in ("move", "force") and r and r.get("ok"):
                nmoves += 1
                prev = (nmoves - 1, legal) if legal and mv in legal else None
                legal = refresh()
        elif k < 0.70:
            if e.mine and rnd.random() < 0.9:
                e.cmd("ui seat %d" % (1 - e.seat))  # `bot` plays for the side not seated
            r = e.cmd("ui bot %s" % rnd.choice(["static"] * 8 + ["", "sim 0.02", "nonsense", "static static"]))
            if r and r.get("ok"):
                nmoves += 1
                legal = refresh()
        elif k < 0.74:
            e.cmd("ui %s %s" % (rnd.choice(["hint", "infer"]), rnd.choice(["0.02", "0.01", "0", "-1", "", "x", "1e-9"])))
        elif k < 0.79:
            e.cmd("ui review %s %s" % (num(), rnd.choice(["0.02", "0", "", "-2"])))
        elif k < 0.87:
            c = rnd.choice(["undo", "rewind %s" % num(), "at %s" % num(), "seat %s" % rnd.choice(["0", "1", "2", "-1", "", "01"]),
                            "record", "state", "cgpout", "gcg", "nonsense"])
            r = e.cmd("ui " + c)
            if r and r.get("ok"):
                if c.startswith(("undo", "rewind")):
                    legal = []
                elif c == "cgpout":
                    cgp = r.get("cgp") or cgp
                elif c == "gcg":
                    gcg = r.get("gcg") or gcg
        elif k < 0.92:
            src = cgp or "15/15/15/15/15/15/15/7QUIZ4/15/15/15/15/15/15/15 AEINRST/ 0/0 0 lex ENABLE;"
            r = e.cmd("ui cgp " + (mutate(src) if rnd.random() < 0.7 else src))
            if r and r.get("ok"):
                legal, nmoves = [], 0
        elif k < 0.97:
            path = os.path.join(OUT, "fuzz.gcg")
            text = gcg or "#character-encoding UTF-8\n#player1 a A\n#player2 b B\n>a: AEINRST 8D RETAINS +66 66\n"
            with open(path, "w", encoding="utf-8", errors="surrogateescape") as f:
                f.write(mutate_lines(text))
            r = e.cmd("ui import " + path)
            if r and r.get("ok"):
                legal, nmoves = [], r["state"]["moves"]
                for _ in range(rnd.randint(0, 2)):  # the review searches from the record's racks
                    e.cmd("ui review %s 0.5" % num())
        else:
            e.cmd("ui new %s %d" % (rnd.choice(["first", "second", ""]), rnd.randint(0, 2**64 - 1)))
            legal, nmoves = [], 0


def main():
    os.makedirs(OUT, exist_ok=True)
    t0 = time.time()
    e = Engine()
    try:
        run(e, t0)
    except (TimeoutError, EOFError, AssertionError) as ex:
        print("FAIL after %d commands (seed %d): %s" % (e.n, SEED, ex))
        print(open(os.path.join(OUT, "stderr.txt"), errors="replace").read()[-4000:])
        sys.exit(1)
    e.p.stdin.close()
    code = e.p.wait(timeout=120)
    err = open(os.path.join(OUT, "stderr.txt"), errors="replace").read()
    if code != 0 or "ERROR" in err or "runtime error" in err:
        print("FAIL at exit (code %d) after %d commands (seed %d)\n%s" % (code, e.n, SEED, err[-4000:]))
        sys.exit(1)
    print("ok: %d commands in %.0f s, seed %d" % (e.n, time.time() - t0, SEED))
    print(" ".join("%s %d/%d" % (v, STATS[(v, True)], STATS[(v, True)] + STATS[(v, False)]) for v in sorted({v for v, _ in STATS})))


main()
