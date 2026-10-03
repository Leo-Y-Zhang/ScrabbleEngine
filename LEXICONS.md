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

## What this means

Tilefish does not distribute either list. It points to a third-party copy and fetches it
when you ask. Many players and tools use that same copy, but neither HarperCollins nor
NASPA has given this project permission, and the MAGPIE-DATA repository states no licence.
If you need certainty, for an event or a commercial use, obtain the list under licence from
its publisher and load it with `--lexicon FILE` or the `lexicon` command. ENABLE needs no
permission. If a rights holder objects, the download links in the scripts and in
`web/worker.js` are the only places to change.
