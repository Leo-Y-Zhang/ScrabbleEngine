#!/bin/sh
# Builds the browser version of Tilefish into web/dist:   sh web/build.sh
# Needs Emscripten (em++), and g++ or clang++ once, to convert ENABLE's leave values to
# the compact .klv2 format.  Try it with   python3 -m http.server -d web/dist   and
# open http://localhost:8000
set -e
cd "$(dirname "$0")/.."
OUT=web/dist
mkdir -p "$OUT/data"
# One thread; an 8 MB stack as on Linux (the move generator keeps large tables on it).
em++ -O3 -std=c++17 -fexceptions tilefish.cpp -o "$OUT/tilefish.js" \
  -sMODULARIZE=1 -sEXPORT_NAME=Tilefish -sENVIRONMENT=worker,node -sALLOW_MEMORY_GROWTH=1 \
  -sMAXIMUM_MEMORY=2GB -sSTACK_SIZE=8MB -sEXPORTED_FUNCTIONS=_tf_run \
  -sEXPORTED_RUNTIME_METHODS=ccall,FS --no-entry
cp web/index.html web/style.css web/app.js web/worker.js "$OUT/"
# The fonts are served from the site itself, never from a third-party font server.
mkdir -p "$OUT/fonts" && cp web/fonts/* "$OUT/fonts/"
cp ENABLE.txt ENABLE.win CSW24.win NWL23.win "$OUT/data/"
if [ ! -f "$OUT/data/ENABLE.klv2" ]; then
  CXX=${CXX:-g++}
  $CXX -O2 -std=c++17 -pthread tilefish.cpp -o "$OUT/tilefish-native"
  "$OUT/tilefish-native" --lexicon ENABLE.txt "saveleaves $OUT/data/ENABLE.klv2"
  rm -f "$OUT/tilefish-native"
fi
echo "built $OUT"
