# Word lists and other data: what is used, and on what terms

Tilefish needs a word list and, to play well, leave values (`.klv2`) and a win model
(`.win`). This page lists each data file, where it comes from and on what basis it is
used. **A public download link or a credit to the publisher is not a licence**, and this
page does not claim one where none exists.

| Data | Owner | In this repository or release? | How Tilefish gets it | Permission |
|---|---|---|---|---|
| `ENABLE.txt` | public domain | yes | bundled | Public domain: free to use and share. |
| `ENABLE.leaves`, `ENABLE.win` | this project (learned by self-play) | yes | bundled | GPL-3.0, like the code. |
| `CSW24.win`, `NWL23.win` | this project (fitted by self-play) | yes | bundled | GPL-3.0. They hold win probabilities only, not words. |
| `OXENDICT.txt` (British English, Oxford spelling) | Kevin Atkinson (English Speller Database) | yes | bundled | ESDB's licence: free to use, copy, change, share and sell, keeping its notice ([below](#oxendict-british-english-in-oxford-spelling)). |
| `OXENDICT.klv2`, `OXENDICT.win` | this project (learned by self-play) | yes | bundled | GPL-3.0. |
| `CEL.txt` (Common English Lexicon) | Eric Smith (Fj00) and Kenji Matsumoto | yes | bundled | MIT licence: free to use, copy, change, share and sell, keeping its notice ([below](#cel-the-common-english-lexicon)). |
| `CEL.klv2`, `CEL.win` | this project (learned by self-play) | yes | bundled | GPL-3.0. |
| `CSW24.klv2`, `NWL23.klv2` | this project (learned by self-play) | yes | bundled | GPL-3.0. Leave values only, not words. |
| Collins Scrabble Words 2024 | HarperCollins | **no** | **not provided**: a player who has a copy loads their own | None granted to this project, and none needed: Tilefish neither hosts nor fetches it. |
| NASPA Word List 2023 | NASPA | **no** | **not provided**: as above | As above. |

## Collins and NWL: the player's own copy

Since 13 October 2026 Tilefish neither hosts nor downloads Collins Scrabble Words or the
NASPA Word List, from anywhere. Earlier versions fetched them, when a player asked, from the
MAGPIE project's public copy on GitHub; that copy states no licence, and neither
HarperCollins nor NASPA has given this project permission, so it stopped.

- **In the browser version:** choose Collins 2024 or NWL 2023 on the start card and load your
  own copy (a text file, one word per line, or a `.kwg` file). The page compiles it and keeps
  it in the browser's storage on your device; it is never uploaded. Clearing the site's data
  removes it. Copies that earlier versions downloaded from the third-party copy are deleted
  from the browser's storage.
- **On your computer:** save your copy next to the program as `CSW24.txt` or `NWL2023.txt`
  (or `.kwg`), or start `tilefish --lexicon /path/to/your/list`.
- **Leave values and win models:** Tilefish's own (`CSW24.klv2`, `NWL23.klv2`, `CSW24.win`,
  `NWL23.win`), learned by self-play, come with it. They are numbers, not words.
- **Exports:** `savewords FILE` writes a loaded list as plain text, for the player's own use.
  A game record (GCG) or a Tilefish game file contains the words played in that game, not the
  list.
- **Strength tests:** the project's registered matches (`.github/workflows/experiment.yml`)
  fetch the lists and MAGPIE's leave values onto GitHub's test machines with
  `tools/research-lexicon.sh`, for the length of a job, so that both engines play under the
  same conditions as in every earlier match. The lists are used there and never published;
  only game logs are kept. The self-play that produced Tilefish's own leave values used a
  copy in the same way.

A copy you are entitled to use is one from its publisher or an association that may give it
to you, or one you obtained under the publisher's terms. Tilefish does not check where a
file came from, and it cannot give anyone a right to a list.

## OXENDICT: British English in Oxford spelling

`OXENDICT.txt` is made by `tools/make_oxendict.py` from the English Speller Database (ESDB,
formerly SCOWL), [github.com/en-wl/wordlist](https://github.com/en-wl/wordlist), pinned to
commit `1e5b7d3`. ESDB's own licence allows using, copying, changing, sharing and selling
word lists made from it, provided its copyright notice goes with every copy and its notice
is in the documentation. Both are met: the notice is at the top of `OXENDICT.txt` (Tilefish
skips lines starting with `#`) and is copied below. ESDB adds two further notices for some
of its data, Australian spellings and word lists larger than size 80; the script uses
neither (British spellings, size 80), so they do not apply.

The list follows the spelling of the Oxford English Dictionary (the `en-GB-oxendict`
spelling, which is where its name comes from). It is **not** the Oxford English Dictionary's
word list and was not made, checked or endorsed by Oxford University Press; "Oxford" names
the spelling standard only. Oxford University Press owns the Oxford English Dictionary and
its trademarks. Racial slurs that ESDB marks as offensive are left out.

> Copyright 2000-2026 by Kevin Atkinson
>
> Permission to use, copy, modify, distribute, and sell any part of the English
> Speller Database (ESDB, previously known as SCOWLv2), or word lists
> created from it, is hereby granted without fee, provided that the above
> copyright notice appears in all copies and that both the above copyright
> notice and this notice appear in supporting documentation.  Kevin Atkinson
> makes no representations about the suitability of this database for any
> purpose.  It is provided "as is" without express or implied warranty.

ESDB is derived from many sources, most of them in the public domain; its primary sources
are 12dicts and ENABLE2K, by Alan Beale and others. In the browser version the page serves
`OXENDICT.txt` and its leave values from the Tilefish site itself, like ENABLE.

## CEL: the Common English Lexicon

`CEL.txt` is made by `tools/make_cel.py` from the Common English Lexicon,
[github.com/Fj00/CEL](https://github.com/Fj00/CEL), pinned to commit `5836e95`: every word of
2 to 15 letters (68,135). Eric Smith (Fj00) and Kenji Matsumoto (strataji) chose its words by
how often they are used in books, articles and on the web, keeping words that are also valid
in tournament play and leaving out expletives, slurs and obscure words. It is published under
the MIT licence, which allows using, copying, changing, sharing and selling it provided the
copyright and permission notice go with every copy. The notice is at the top of `CEL.txt` and
is copied below. CEL is not made, checked or endorsed by HarperCollins, NASPA or Woogles. In
the browser version the page serves `CEL.txt` (compiled) and its leave values from the
Tilefish site itself, like ENABLE.

> MIT License
>
> Copyright (c) 2021-2022 Fj00
>
> Permission is hereby granted, free of charge, to any person obtaining a copy
> of this software and associated documentation files (the "Software"), to deal
> in the Software without restriction, including without limitation the rights
> to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
> copies of the Software, and to permit persons to whom the Software is
> furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all
> copies or substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
> IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
> FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
> AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
> LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
> SOFTWARE.

## Which lists are current

Checked on 13 October 2026. Collins Scrabble Words 2024 (CSW24, seventh edition, in WESPA
tournament play since 1 January 2025) and the NASPA Word List 2023 (NWL2023, in NASPA play
since 29 February 2024) are the latest editions of the two tournament lists; MAGPIE-DATA's
latest commit (8 October 2026) has no newer English list. When a new edition appears, Tilefish
needs leave values learned for it (`train`), a name on the start card (`web/app.js`,
`web/index.html`, `web/worker.js`), and for the strength tests the pinned commit and
checksums in `tools/research-lexicon.sh`. Older editions (CSW21, NWL2020, TWL06 and earlier)
are left out on purpose: no rated tournament uses them now.

## What this means

Tilefish distributes no word list it lacks the right to: it ships ENABLE (public domain),
OXENDICT (ESDB's licence) and CEL (MIT licence), each with its notice, and data of its own.
Collins and NWL reach the engine only as a copy the player supplies, which stays on their
device. To offer Collins or NWL built in, as many players would like, the project needs a
licence from HarperCollins or NASPA; [docs/DISTRIBUTING.md](docs/DISTRIBUTING.md) says how to
ask, and what changes once one is granted.
