# Distributing Tilefish: what is safe, and what needs permission

This page sets out, part by part, what Tilefish may share freely and where someone else's
rights come in, so that the site, the downloads, or a future app can be distributed without
trouble. It is a careful summary, **not legal advice**: before selling Tilefish, putting it
in an app store or making a paid product of it, have an intellectual-property lawyer check
it for the countries it will be sold in. Checked on 13 October 2026.

## In short

| Part | Who owns it | Risk as distributed now | What keeps it safe |
|---|---|---|---|
| The engine and the page (`tilefish.cpp`, `web/`, `tools/`) | this project | none | GPL-3.0. All of it is this project's own code. |
| The rules of the game, the board layout, tile values and counts | nobody | none | Rules and methods of play are ideas, which copyright does not protect (the US Copyright Office says so of games in general). |
| The board's and tiles' appearance | Tilefish's own drawing | none | Artwork on a game's board or box can be protected; Tilefish draws its own board, tiles and icons and copies no artwork. |
| The SCRABBLE® name and logo | Hasbro (USA, Canada); J.W. Spear & Sons Ltd, a Mattel company (elsewhere) | low | The name is Tilefish. The mark is used only to say whom the program is for, with ® and the notice. See [Trademarks](#trademarks). |
| ENABLE, OXENDICT, CEL | public domain; Kevin Atkinson (ESDB licence); Fj00 (MIT) | none | Bundled with their notices ([LEXICONS.md](../LEXICONS.md)). |
| Collins Scrabble Words 2024, NWL2023 | HarperCollins; NASPA | **low, not zero** | Never hosted here; the visitor's browser or computer downloads them from MAGPIE-DATA. Only a licence makes it zero. See [Word lists](#tournament-word-lists). |
| Leave values for CSW24 and NWL23 (`.klv2`) | the MAGPIE project, no licence stated | low | Downloaded from MAGPIE-DATA like the lists. Tilefish can learn its own, as it did for ENABLE, OXENDICT and CEL. |
| Fonts | their designers, SIL Open Font Licence | none | Licence files ship with the fonts (`web/fonts/OFL-*.txt`). |
| Association names and rules (NASPA, WESPA, ABSP) | the associations | none | Named only to say which rules a game follows; summaries are in Tilefish's own words, with no logos and no claim to be official or rated. |

## Trademarks

SCRABBLE® is a registered trademark of Hasbro, Inc. in the USA and Canada and of J.W. Spear &
Sons Ltd, a subsidiary of Mattel, Inc., elsewhere. A trademark protects a name or logo as a
sign of who makes a product; it does not protect the game itself. The cases that went
against other programs were about names: in 2008 the Delhi High Court stopped the use of the
name "Scrabulous" (Mattel v. Agarwalla), while the game went on under the name Lexulous, and
Hasbro withdrew its US suit after the game was changed. In 2013 the Court of Appeal in London
held that Mattel's registration of a letter tile was not a valid trade mark (J.W. Spear &
Sons v. Zynga).

What Tilefish does, and must keep doing:

- **Its own name.** Tilefish, never a name containing or sounding like SCRABBLE.
- **The mark only to describe.** "For SCRABBLE® players" or "the crossword game" is fine;
  the mark is never part of a title, logo, icon, app-store name or keywords, and never a
  verb ("let's Scrabble").
- **No logos or artwork** of Hasbro or Mattel: no box art, no logo, no copied tile design.
- **The notice** above, and "not affiliated with, sponsored or endorsed by", on the page,
  in the README, in the downloads and in any store listing.

Two things remain that a cautious owner may want to change before wider distribution:

1. **The repository's name, `ScrabbleEngine`, and so the site's address**
   (`leo-y-zhang.github.io/ScrabbleEngine/`) contain the mark. Renaming the repository to
   `Tilefish` removes it. The cost: GitHub redirects the repository's old address, but not a
   GitHub Pages site, so the site moves to `leo-y-zhang.github.io/Tilefish/` and old links
   (including shared game links and installed copies of the app) stop working. Only the
   owner can rename it (Settings, General, Repository name).
2. **The board's colours.** Like most crossword-game programs, Tilefish colours the
   premium squares in the usual way (red, pink, dark blue, light blue). No ruling against
   that is known, but a palette of Tilefish's own (`--tw`, `--dw`, `--tl`, `--dl` in
   `web/style.css`) would remove even that question.

## Tournament word lists

Collins Scrabble Words is HarperCollins' copyright and the NASPA Word List is NASPA's.
Neither has given Tilefish permission. Tilefish never hosts or ships them: the site and the
`get-lexicon` scripts download them, when a player asks, from the MAGPIE project's public
copy on GitHub, pinned to one commit and checked by SHA-256. Many players and programs use
that copy. This keeps the risk low, but **it does not make it zero**: a rights holder could
still object to a program that fetches its list. If one does, the download links in
`get-lexicon.sh`, `get-lexicon.ps1` and `web/worker.js` are the only places to change, and
removing them ends the matter.

**The only way to zero risk with these lists is a licence.** Ask:

- **NASPA** ([scrabbleplayers.org](https://www.scrabbleplayers.org/)), for NWL2023;
- **HarperCollins Publishers** (Collins Dictionaries, [collins.co.uk](https://collins.co.uk/)),
  for Collins Scrabble Words 2024.

Ask for permission to use the list in a free browser app and a free downloadable program,
and, separately, for terms for a paid app. Once licensed, the files can be served from the
site itself, like ENABLE, and that is also the time to train Tilefish's own leave values for
them (which needs a registered experiment, because new values change how it plays).

## Building a ChessBase or Chessable for this game

Yes, that is allowed in principle, and it has been done: Quackle, Macondo, MAGPIE and Woogles'
online play site (under its own game name, OMGWords) are open-source engines and play sites
for this game made without the trademark owners. What such a
product needs:

- **Its own name and look** (above). App stores enforce this themselves: Apple and Google
  reject apps whose names, icons or keywords use someone else's trademark without permission.
- **Licences for the tournament lists** before selling them, or putting them inside an app.
  A product built on the free lists (ENABLE, OXENDICT, CEL) needs none.
- **Its own teaching content.** Lessons, puzzles and annotated games written for Tilefish
  belong to their authors; rulebooks, study books and word-study material of NASPA, WESPA,
  the ABSP, HarperCollins or anyone else may be referred to but not copied.
- **The code licence.** Tilefish is GPL-3.0. Because all of its code is this project's own,
  the owner may also sell it, or a closed-source app built on it; anyone else may copy and
  share the GPL version, which is the trade the GPL makes. Accepting outside contributions
  under the GPL would mean a closed version needs their agreement too.
- **A privacy statement** for the stores: the page sets no cookies, has no accounts, adverts
  or trackers, and keeps settings and games in the browser on the device. GitHub hosts the
  site, and serves the Collins and NWL downloads, so it sees the usual request data (an IP
  address) as any web host does.
- **No help during real games.** Tilefish offers no word lookup, so it cannot act as a word
  finder; its analysis is for practice and review, as tournament rules forbid electronic aids
  during play.

## Before a wider release

1. Decide on the repository's name (above). Everything else in the table is already done.
2. Write to NASPA and HarperCollins about licences, if Collins and NWL are to be offered
   with no risk at all, and before any paid or app-store version.
3. For an app-store or paid version, have an intellectual-property lawyer review this page,
   the store listing and the privacy statement.
