#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds CEL.txt, the Common English Lexicon that comes with Tilefish, from the CEL
repository by Eric Smith (Fj00) and Kenji Matsumoto (strataji), the list Woogles
offers for games with everyday words.

    git clone https://github.com/Fj00/CEL && cd CEL
    git checkout 5836e959e56bac99547345837aacbde8f2a623b8
    python3 /path/to/tools/make_cel.py . > CEL.txt

What goes in: cel_2-15.txt, every CEL word of 2 to 15 letters (the longest a 15 by 15
board takes), as plain letters.  CEL is published under the MIT licence, which lets
anyone use, copy, change and share it provided its copyright and permission notice go
with every copy; the notice is printed at the top of the output (Tilefish skips lines
starting with #).
"""
import os
import re
import subprocess
import sys

CEL_COMMIT = "5836e959e56bac99547345837aacbde8f2a623b8"
NOTICE = """\
# CEL: the Common English Lexicon, for Tilefish (tools/make_cel.py).
# Built from github.com/Fj00/CEL commit {commit}: cel_2-15.txt,
# every word of 2 to 15 letters.  CEL was made by Eric Smith (Fj00) and Kenji Matsumoto
# (strataji) from words in common use; it is not made, endorsed or checked by
# HarperCollins, NASPA or Woogles.
#
# MIT License
#
# Copyright (c) 2021-2022 Fj00
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""

PLAIN = re.compile(r"^[a-z]{2,15}$")


def main():
    cel = sys.argv[1] if len(sys.argv) > 1 else "."
    head = subprocess.run(["git", "-C", cel, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != CEL_COMMIT:
        sys.exit("error: %s is at %s, not CEL commit %s" % (cel, head or "?", CEL_COMMIT))
    with open(os.path.join(cel, "cel_2-15.txt"), encoding="utf-8") as f:
        lines = [w.strip().lower() for w in f]
    bad = [w for w in lines if w and not PLAIN.match(w)]
    if bad:
        sys.exit("error: not plain words of 2 to 15 letters: %s" % " ".join(bad[:10]))
    words = sorted(set(w for w in lines if w))
    sys.stdout.write(NOTICE.format(commit=CEL_COMMIT))
    sys.stdout.write("\n".join(words) + "\n")
    print("%d words" % len(words), file=sys.stderr)


if __name__ == "__main__":
    main()
