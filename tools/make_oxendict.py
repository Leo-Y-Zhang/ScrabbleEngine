#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Builds OXENDICT.txt, the British English word list in Oxford spelling that comes
with Tilefish, from the English Speller Database (ESDB, formerly SCOWL).

    git clone https://github.com/en-wl/wordlist && cd wordlist
    git checkout 1e5b7d3a72f47a71da5d28686c1dd4b397178485 && make
    python3 /path/to/tools/make_oxendict.py . > OXENDICT.txt

What goes in: ESDB size 80 ("a valid word in current usage", the size ESDB's author
describes as holding the unusual words people use in word games), British spellings
with -ize (Oxford, the ESDB spelling code Z) and -ise (B), and their acceptable variants
(variant level 6 and below; archaic, uncommon and invalid variants stay out).  Only
plain lower-case words of 2 to 15 letters, accents removed, are kept, so proper nouns,
possessives, contractions, hyphenated and open compounds, Roman numerals and
programmers' slang are out, and so are abbreviations, including lower-case unit
symbols such as "kg" that ESDB knows only from one old source (Moby).  The racial slurs
ESDB marks as offensive are left out too, as NASPA did for its word list in 2020.

Sizes above 80 and the Australian spelling are not used, so only ESDB's own MIT-like
notice applies (printed at the top of the output; Tilefish skips lines starting with #).
"""
import os
import re
import sqlite3
import subprocess
import sys

ESDB_COMMIT = "1e5b7d3a72f47a71da5d28686c1dd4b397178485"
SIZE = 80
NOTICE = """\
# OXENDICT: British English in Oxford spelling, for Tilefish (tools/make_oxendict.py).
# Built from the English Speller Database (ESDB, formerly SCOWL) at
# github.com/en-wl/wordlist commit {commit}: size {size}, British -ize
# and -ise spellings and acceptable variants, plain words of 2 to 15 letters, no
# abbreviations, proper nouns or slurs.  It follows the spelling of the Oxford English
# Dictionary but is not that dictionary's word list, and it is not made, endorsed or
# checked by Oxford University Press.
#
# Copyright 2000-2026 by Kevin Atkinson
#
# Permission to use, copy, modify, distribute, and sell any part of the English
# Speller Database (ESDB, previously known as SCOWLv2), or word lists
# created from it, is hereby granted without fee, provided that the above
# copyright notice appears in all copies and that both the above copyright
# notice and this notice appear in supporting documentation.  Kevin Atkinson
# makes no representations about the suitability of this database for any
# purpose.  It is provided "as is" without express or implied warranty.
#
# ESDB is derived from many sources, most of which are in the Public Domain.
# The primary source of words comes from 12dicts and ENABLE2K, both in the Public
# Domain; Alan Beale deserves special credit as the author of 12dicts and a major
# contributor to ENABLE2K.
"""

PLAIN = re.compile(r"^[a-z]{2,15}$")


def main():
    esdb = sys.argv[1] if len(sys.argv) > 1 else "."
    head = subprocess.run(["git", "-C", esdb, "rev-parse", "HEAD"], capture_output=True, text=True).stdout.strip()
    if head != ESDB_COMMIT:
        sys.exit("error: %s is at %s, not ESDB commit %s" % (esdb, head or "?", ESDB_COMMIT))
    db = os.path.join(esdb, "scowl.db")
    out = subprocess.run(
        [sys.executable, os.path.join(esdb, "scowl"), "--db", db, "word-list", str(SIZE), "B,Z", "6", "--categories=",
         "--wo-poses=abbr,s,x,pre,suf,wp,we", "--wo-usage-notes=offensive-1,offensive-2", "--deaccent"],
        capture_output=True, text=True, check=True, cwd=esdb).stdout
    words = {w for w in out.split("\n") if PLAIN.match(w)}

    # Every entry by its letters, to find abbreviations that the part-of-speech filter
    # above cannot see: one spelled in capitals or with dots (CD, c.f.) whose lower-case
    # form ESDB has from Moby alone, and lower-case unit symbols without a vowel (kb, dg).
    # A word is kept anyway when a lower-case entry from another source supports it.
    con = sqlite3.connect(db)
    abbr, support, slur = set(), set(), set()
    for word, pos, tag, note in con.execute(
            "select word, pos, tag, usage_note from scowl_v0 where size <= ?", (SIZE,)):
        letters = word.replace(".", "").lower()
        if note.startswith("offensive"):
            slur.add(letters)
        if pos == "abbr" or "." in word or (len(word) > 1 and word.isupper()):
            abbr.add(letters)
        elif word == letters and pos not in ("x", "s") and tag != "[moby]":
            support.add(word)
    vowel = re.compile(r"[aeiouy]")
    keep = sorted(w for w in words
                  if w not in slur and (w in support or (w not in abbr and vowel.search(w))))
    sys.stdout.write(NOTICE.format(commit=ESDB_COMMIT, size=SIZE))
    sys.stdout.write("\n".join(keep) + "\n")
    print("%d words" % len(keep), file=sys.stderr)


if __name__ == "__main__":
    main()
