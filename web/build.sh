#!/bin/sh
# Builds the browser version of Tilefish into web/dist:   sh web/build.sh
# Needs Emscripten (em++), and g++ or clang++ once, to convert ENABLE's leave values to
# the compact .klv2 format and to compile the free word lists to .kwg.  Try it with   python3 -m http.server -d web/dist   and
# open http://localhost:8000
set -e
cd "$(dirname "$0")/.."
OUT=web/dist
mkdir -p "$OUT/data"
# Two builds of the same engine.  tilefish.js: one thread, for any browser.  tilefish-mt.js:
# a search thread per core (pthreads on SharedArrayBuffer), used where the page is
# cross-origin isolated (web/coi-sw.js).  Both use WebAssembly's own exceptions (smaller
# and faster than the JavaScript-based ones) and an 8 MB stack as on Linux, for every
# thread (the move generator keeps large tables on it).
EM="-O3 -std=c++17 -fwasm-exceptions tilefish.cpp -sMODULARIZE=1 -sENVIRONMENT=worker,node -sALLOW_MEMORY_GROWTH=1
  -sSTACK_SIZE=8MB -sEXPORTED_FUNCTIONS=_tf_run -sEXPORTED_RUNTIME_METHODS=ccall,FS --no-entry"
em++ $EM -sEXPORT_NAME=Tilefish -sMAXIMUM_MEMORY=2GB -o "$OUT/tilefish.js"
em++ $EM -pthread -sEXPORT_NAME=TilefishMT -sINITIAL_MEMORY=256MB -sMAXIMUM_MEMORY=1GB \
  -sDEFAULT_PTHREAD_STACK_SIZE=8MB -o "$OUT/tilefish-mt.js" \
  -sPTHREAD_POOL_SIZE='Math.min(8,(typeof navigator!="undefined"&&navigator.hardwareConcurrency)||2)+1'
# (the engine never starts more search threads at once than worker.js gives it, one per
# core up to 8, so a pool that size plus one never has to start a worker mid-search)
cp web/index.html web/style.css web/app.js web/worker.js web/coi-sw.js "$OUT/"
# The fonts are served from the site itself, never from a third-party font server.
mkdir -p "$OUT/fonts" && cp web/fonts/* "$OUT/fonts/"
cp ENABLE.txt ENABLE.win CSW24.win NWL23.win OXENDICT.txt OXENDICT.klv2 OXENDICT.win "$OUT/data/"
# The free word lists are served compiled (.kwg): building the graph from a text list
# takes the browser several times longer than loading it, every time the engine starts.
stale() { [ ! -f "$OUT/data/$1" ] || [ "$2" -nt "$OUT/data/$1" ] || [ tilefish.cpp -nt "$OUT/data/$1" ]; }
if stale ENABLE.klv2 ENABLE.leaves || stale ENABLE.kwg ENABLE.txt || stale OXENDICT.kwg OXENDICT.txt; then
  CXX=${CXX:-g++}
  $CXX -O2 -std=c++17 -pthread tilefish.cpp -o "$OUT/tilefish-native"
  "$OUT/tilefish-native" --threads 1 --lexicon ENABLE.txt "saveleaves $OUT/data/ENABLE.klv2; savekwg $OUT/data/ENABLE.kwg"
  "$OUT/tilefish-native" --threads 1 --lexicon OXENDICT.txt "savekwg $OUT/data/OXENDICT.kwg"
  rm -f "$OUT/tilefish-native"
fi
# Gzipped copies, a quarter smaller, for browsers that can unpack them (DecompressionStream);
# GitHub Pages does not compress binary files itself.
for f in ENABLE.kwg ENABLE.klv2 OXENDICT.kwg OXENDICT.klv2; do gzip -9 -n -c "$OUT/data/$f" > "$OUT/data/$f.gz"; done
# Versions on every file the page loads (GitHub Pages lets browsers keep a file for ten
# minutes, so without them a browser could mix files from two releases): CODE changes
# with the engine or the page, DATA with the files the site serves.
hash() { if command -v sha256sum >/dev/null 2>&1; then sha256sum; else shasum -a 256; fi | cut -c1-12; }
CODE=$(cat tilefish.cpp web/index.html web/style.css web/app.js web/worker.js web/build.sh | hash)
DATA=$(cd "$OUT/data" && cat ENABLE.kwg ENABLE.klv2 ENABLE.win OXENDICT.kwg OXENDICT.klv2 OXENDICT.win CSW24.win NWL23.win | hash)
sed -e "s|href=\"style.css\"|href=\"style.css?v=$CODE\"|" -e "s|src=\"app.js\"|src=\"app.js?v=$CODE\"|" web/index.html > "$OUT/index.html"
sed -e "s|^const VERSION = \"\";|const VERSION = \"?v=$CODE\&d=$DATA\";|" web/app.js > "$OUT/app.js"
grep -q "style.css?v=$CODE" "$OUT/index.html" && grep -q "app.js?v=$CODE" "$OUT/index.html" &&
  grep -q "^const VERSION = \"?v=$CODE&d=$DATA\";" "$OUT/app.js" || { echo "error: could not set the version"; exit 1; }
echo "built $OUT (version $CODE, data $DATA)"
