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
| Collins Scrabble Words 2024 (`CSW24.kwg`) | HarperCollins | **no** | downloaded from [jvc56/MAGPIE-DATA](https://github.com/jvc56/MAGPIE-DATA), commit `adf2931`, SHA-256 checked | **None granted to this project.** |
| NASPA Word List 2023 (`NWL23.kwg`) | NASPA | **no** | as above | **None granted to this project.** |
| `CSW24.klv2`, `NWL23.klv2` (leave values) | the MAGPIE project | no | as above | MAGPIE-DATA declares no licence. |

## What happens with CSW24 and NWL23

- **On your computer:** `get-lexicon.sh`, `get-lexicon.bat` and `get-lexicon.ps1` download
  the files from MAGPIE-DATA into Tilefish's folder when you run them. They are not in any
  release zip.
- **In the browser version:** your browser downloads the same files directly from
  `raw.githubusercontent.com` (MAGPIE-DATA). The Tilefish site does not host or proxy them.
  The browser keeps a copy in its own cache storage on your device, so the next game does
  not download them again. Clearing the site's data removes it.
- **Exports:** `savewords FILE` writes a loaded list as plain text. A game record (GCG) or
  a Tilefish game file contains the words played in that game, not the list.
- **Strength tests:** `.github/workflows/experiment.yml` downloads CSW24 or NWL23 onto
  GitHub's machines for the duration of a job. It exports the text list there for the
  referee and uploads only the game logs, never the list.

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

## What this means

Tilefish does not distribute either list. It points to a third-party copy and fetches it
when you ask. Many players and tools use that same copy, but neither HarperCollins nor
NASPA has given this project permission, and the MAGPIE-DATA repository states no licence.
If you need certainty, for an event or a commercial use, obtain the list under licence from
its publisher and load it with `--lexicon FILE` or the `lexicon` command. ENABLE needs no
permission, and neither does OXENDICT, under ESDB's licence above. If a rights holder
objects, the download links in the scripts and in `web/worker.js` are the only places to
change.
