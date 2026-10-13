# Distributing Tilefish: what is safe, and what needs permission

This page sets out, part by part, what Tilefish may share freely and where someone else's
rights come in, so that the site, the downloads, or a future app can be distributed without
trouble. It is a careful summary, **not legal advice**: before selling Tilefish, putting it
in an app store or making a paid product of it, have an intellectual-property lawyer check
it for the countries it will be sold in. Checked on 13 October 2026, when the owner asked for
no risk and left the decisions below to the project: Collins and NWL are no longer fetched
from anywhere, the repository and site are named TileFish, the board has a palette of its own,
and a lawyer's review is required before any paid or app-store version (not for the free site).

## In short

| Part | Who owns it | Risk as distributed now | What keeps it safe |
|---|---|---|---|
| The engine and the page (`tilefish.cpp`, `web/`, `tools/`) | this project | none | GPL-3.0. All of it is this project's own code. |
| The rules of the game, the board layout, tile values and counts | nobody | none | Rules and methods of play are ideas, which copyright does not protect (the US Copyright Office says so of games in general). |
| The board's and tiles' appearance | Tilefish's own drawing | none | Artwork on a game's board or box can be protected; Tilefish draws its own board, tiles and icons, in its own colours, and copies no artwork. |
| The SCRABBLE® name and logo | Hasbro (USA, Canada); J.W. Spear & Sons Ltd, a Mattel company (elsewhere) | very low | The product, the repository and the site are named Tilefish (TileFish). The mark is used only to say whom the program is for, with ® and the notice. See [Trademarks](#trademarks). |
| ENABLE, OXENDICT, CEL | public domain; Kevin Atkinson (ESDB licence); Fj00 (MIT) | none | Bundled with their notices ([LEXICONS.md](../LEXICONS.md)). |
| Collins Scrabble Words 2024, NWL2023 | HarperCollins; NASPA | none from Tilefish | Neither hosted nor fetched. A player who has a copy loads their own; it stays on their device. See [Tournament word lists](#tournament-word-lists). |
| Leave values and win models for CSW24 and NWL23 | this project (learned by self-play) | none | GPL-3.0; numbers only, no words. |
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

Two further steps were taken on 13 October 2026:

1. **The repository was renamed** by the owner from `ScrabbleEngine` to `TileFish`, so the
   mark is in neither the project's name nor the site's address, now
   `leo-y-zhang.github.io/TileFish/`. GitHub redirects the repository's old address; the old
   site address no longer works, so older links to it (shared game links among them) need
   the new address. Settings and games kept in a browser stay, as the site's origin is the
   same.
2. **The board has its own colours.** The premium squares were coloured the usual way (red,
   pink, dark blue, light blue). They now use Tilefish's palette: terracotta for triple word,
   maple for double word, felt green for triple letter and sage for double letter, each
   still labelled.

## Tournament word lists

Collins Scrabble Words is HarperCollins' copyright and the NASPA Word List is NASPA's.
Neither has given Tilefish permission, so **Tilefish neither hosts nor fetches them**. A
player who has a copy they are entitled to use loads it from their own device (the start card
in the browser, or `--lexicon` on a computer); it is compiled and kept on that device and
never uploaded. Tilefish's own leave values and win models for both lists, learned by
self-play, come with it, so a player's copy plays at full strength. Until 13 October 2026 the
site and the `get-lexicon` scripts fetched the lists from the MAGPIE project's public copy on
GitHub; that has stopped, and copies a browser kept from it are deleted.

The project's own strength tests still fetch that copy onto GitHub's machines for the length
of a job (`tools/research-lexicon.sh`), as matches against other engines need the same list,
and never publish it. The self-play that produced Tilefish's leave values used it the same
way. That is private use for testing, not distribution.

**To offer Collins or NWL built in**, which is what most players would like, the project
needs a licence. Ask:

- **NASPA** ([scrabbleplayers.org](https://www.scrabbleplayers.org/)), for NWL2023;
- **HarperCollins Publishers** (Collins Dictionaries, [collins.co.uk](https://collins.co.uk/)),
  for Collins Scrabble Words 2024.

A letter can say, in your own words:

> I maintain Tilefish (github.com/Leo-Y-Zhang/TileFish), a free, open-source engine for
> practising and analysing the crossword board game, which runs in the browser with no
> accounts, adverts or tracking. Players would like to use [Collins Scrabble Words 2024 /
> the NASPA Word List 2023] in it. I would like a licence to include the list in the free
> browser version and the free downloadable program, kept in a compiled form that cannot be
> browsed or looked up during play, with your copyright notice. Please also tell me your
> terms for a paid app, should there be one. At present Tilefish does not provide the list;
> players load their own copies.

Once licensed, the list can be served from the site like ENABLE: in `web/worker.js` its entry
gets a site file in place of the player's own, and the start card's note changes.

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
  or trackers, and keeps settings, games and any word list a player loads in the browser on
  the device. GitHub hosts the site, so it sees the usual request data (an IP address) as any
  web host does.
- **No help during real games.** Tilefish offers no word lookup, so it cannot act as a word
  finder; its analysis is for practice and review, as tournament rules forbid electronic aids
  during play.

## Keeping the maintainer's name out of it

The code, the data, the page and the downloads hold no personal details of anyone who works
on Tilefish, and the page collects none from players. Two things still tie the project to the
personal GitHub account that holds it:

- **The address.** A repository under a personal account is served at
  `<account>.github.io/<repository>/`, so the account's name is in the site's address and on
  the repository's page.
- **Commit authors.** Commits and merges made through that account show its display name
  (with GitHub's private no-reply address, not a real email).

To separate the project from the person, the owner can create a free GitHub organisation (for
example `tilefish-app`) and transfer the repository to it: on GitHub, Settings, General,
Danger zone, Transfer ownership. GitHub redirects the repository's old addresses; the site
moves to `<organisation>.github.io/TileFish/`, and the address in the files changes again
(CLAUDE.md lists where). Changing the account's public display name also changes how future
commits and merges show. Past commits keep their author names: removing them would mean
rewriting the project's whole history, which breaks every existing copy and link, and GitHub
can keep old commits reachable for some time, so it is not recommended.

## Before a wider release

1. Optionally, move the repository to an organisation (above), to keep a personal name out of
   its address.
2. Nothing else is needed for the free site and downloads: every part in the table above is
   either the project's own, under a licence that allows it, or not provided.
3. A licence from NASPA or HarperCollins is needed only to offer their lists built in (above).
4. Before any paid or app-store version, have an intellectual-property lawyer review this page,
   the store listing and the privacy statement, for the countries it will be sold in.
