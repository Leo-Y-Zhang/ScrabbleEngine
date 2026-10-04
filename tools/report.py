#!/usr/bin/env python3
"""Markdown record of one finished experiment run, under the rule registered for it.

    python3 tools/report.py NAME LOG [--run ID] [--conclusion success]

Used by .github/workflows/report.yml when an experiment run finishes.  Prints nothing and
exits 3 for a run with no registered rule here.  The rules are the ones fixed before any
game in experiments/README.md (sections 9, 10 and 12); this file only applies them to the
point estimates and intervals that tools/screen.py computes.  A run that did not finish
cleanly, or played fewer deal pairs than registered, is recorded as incomplete and gets no
verdict.
"""
import argparse
import datetime
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FROZEN = ["--min-score", "0.5", "--strict", "--min-spread", "0"]
MACONDO = ["--control", "experiments/fresh-9200.jsonl.gz", "--min-score", "0.5225"]
RULES = {
    "tournament-60s-4t": dict(section=9, pairs=416, kind="interval"),
    "deep-screen-frozen": dict(section=10, pairs=200, kind="screen", args=FROZEN, sibling="deep-screen-macondo"),
    "deep-screen-macondo": dict(section=10, pairs=200, kind="screen", args=MACONDO, sibling="deep-screen-frozen"),
    "infer-screen-frozen": dict(section=12, pairs=200, kind="screen", args=FROZEN, sibling="infer-screen-macondo"),
    "infer-screen-macondo": dict(section=12, pairs=200, kind="screen", args=MACONDO, sibling="infer-screen-frozen"),
}


def tool(*args):
    r = subprocess.run([sys.executable, *args], cwd=ROOT, capture_output=True, text=True)
    return (r.stdout + r.stderr).rstrip(), r.returncode


def screen_out(log, args):
    return tool("tools/screen.py", log, *args)[0]


def leg_passes(out):
    return "decision: PASSES" in out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("name")
    ap.add_argument("log")
    ap.add_argument("--run", default="")
    ap.add_argument("--conclusion", default="success")
    a = ap.parse_args()
    rule = RULES.get(a.name)
    if not rule:
        return 3
    sec = rule["section"]
    base = screen_out(a.log, [])
    m = re.search(r"(\d+) deal pairs", base)
    played = int(m.group(1)) if m else 0
    lines = [f"### Recorded automatically: `{a.name}` (section {sec})", "",
             f"Run {a.run or '(manual)'}, recorded {datetime.date.today().isoformat()} by `tools/report.py`; "
             f"log `experiments/{a.name}.jsonl.gz`; {played} of {rule['pairs']} registered deal pairs; "
             f"run conclusion: {a.conclusion}.", ""]
    complete = a.conclusion == "success" and played == rule["pairs"]
    blocks, verdict = [], ""
    if rule["kind"] == "interval":
        blocks.append(base)
        blocks.append(tool("tools/analyze.py", a.log, "--bootstrap", "10000")[0])
        blocks.append(tool("tools/latebias.py", "decided", a.log, "--by", "either")[0])
        blocks.append(tool("tools/latebias.py", "convert", a.log)[0])
        ci = re.search(r"A's score\s+([\d.]+)% \(([\d.]+)% to ([\d.]+)%\)", base)
        if complete and ci:
            lo, hi = float(ci.group(2)), float(ci.group(3))
            word = "ahead" if lo > 50 else "behind" if hi < 50 else "level within the interval"
            verdict = (f"Under the rule registered in section 9: **{word}** at this budget (Tilefish's score "
                       f"{ci.group(1)}%, 95% interval {lo}% to {hi}%). Nothing beyond this opponent, this "
                       f"configuration, CSW24 and this budget follows.")
    else:
        out = screen_out(a.log, rule["args"])
        blocks.append(out)
        if complete:
            mine = leg_passes(out)
            verdict = f"This leg's condition under section {sec}'s screen rule: **{'met' if mine else 'not met'}**."
            sib = os.path.join(ROOT, "experiments", rule["sibling"] + ".jsonl.gz")
            if os.path.exists(sib):
                other = leg_passes(screen_out(sib, RULES[rule["sibling"]]["args"]))
                both = mine and other
                verdict += (f" The other leg (`{rule['sibling']}`, recorded earlier) was "
                            f"{'met' if other else 'not met'}, so **the screen {'PASSES' if both else 'FAILS'}**. "
                            + ("A pass leads only to a registered confirmation on fresh seeds; nothing becomes "
                               "the default from a screen." if both else "The default stays."))
            else:
                verdict += " The screen's decision needs the other leg too; it is added when that run is recorded."
    if not complete:
        verdict = ("**Incomplete run: not a result under the registered rule** (the rule requires the "
                   "registered number of deal pairs, played to full size). Shown for the record only.")
    for b in blocks:
        lines += ["```text", b, "```", ""]
    lines += [verdict, ""]
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
