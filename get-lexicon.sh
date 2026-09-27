#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Downloads a tournament word list for Tilefish, with its leave values:
#   CSW24  Collins Scrabble Words 2024: the World Scrabble Championship and most
#          countries outside North America (the default)
#   NWL23  NASPA Word List 2023: North America
# Usage:  sh get-lexicon.sh [CSW24|NWL23]
# The files come from the MAGPIE project's public data (github.com/jvc56/MAGPIE-DATA),
# pinned to one commit and checked against their SHA-256 sums.  The word lists are
# copyrighted by their publishers, which is why they are not part of Tilefish itself.
set -e
LEX=${1:-CSW24}
case "$LEX" in
  CSW24 | NWL23) ;;
  *)
    echo "usage: sh get-lexicon.sh [CSW24|NWL23]"
    exit 1
    ;;
esac
cd "$(dirname "$0")"
URL=https://github.com/jvc56/MAGPIE-DATA/raw/adf29316fcb2d7bd78a832e198c1bdfc112efa89/data/lexica

expected() {
  case "$1" in
    CSW24.kwg) echo 62ca7a84f07429a9976f77a4f74b94911aca5d49cce050dce72dd0e032c0566f ;;
    CSW24.klv2) echo b0ef5f6637cca0cd8e0962d6fb7d35b2d22f9bf648015f48fa8f80e24a60610b ;;
    NWL23.kwg) echo 3e74af981fdd974e107283f686da0fe4b7ec84ad0d825d444330c338c33b91ba ;;
    NWL23.klv2) echo 37dea945c29c3773eb4cd5a4117f3d3256c8b548cd3bf3b5ce8a219cb5e0a3fa ;;
  esac
}

sha256() {
  if command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$1" | cut -d' ' -f1
  else
    shasum -a 256 "$1" | cut -d' ' -f1
  fi
}

for f in "$LEX.kwg" "$LEX.klv2"; do
  if [ -f "$f" ] && [ "$(sha256 "$f")" = "$(expected "$f")" ]; then
    echo "$f is already here"
    continue
  fi
  echo "downloading $f"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL -o "$f.part" "$URL/$f" || true
  else
    wget -q -O "$f.part" "$URL/$f" || true
  fi
  if [ ! -s "$f.part" ]; then
    rm -f "$f.part"
    echo "error: could not download $f (is this computer online?)"
    exit 1
  fi
  if [ "$(sha256 "$f.part")" != "$(expected "$f")" ]; then
    rm -f "$f.part"
    echo "error: $f did not download correctly (checksum mismatch)"
    exit 1
  fi
  mv "$f.part" "$f"
done
if [ "$LEX" = CSW24 ]; then
  echo "Done. Start ./tilefish: it now plays CSW24."
else
  echo "Done. Start ./tilefish --lexicon NWL23.kwg to play NWL23 (CSW24 wins when both are here)."
fi
