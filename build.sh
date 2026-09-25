#!/bin/sh
# Builds Tilefish on Linux, macOS, WSL or MinGW.   Usage:  ./build.sh
cd "$(dirname "$0")" || exit 1
CXX=${CXX:-}
if [ -z "$CXX" ]; then
  if command -v g++ >/dev/null 2>&1; then CXX=g++; else CXX=clang++; fi
fi
echo "Compiling with $CXX ..."
if "$CXX" -O3 -march=native -std=c++17 -pthread tilefish.cpp -o tilefish 2>/dev/null; then
  :
else
  # Some compilers (e.g. older Apple clang on ARM) reject -march=native.
  "$CXX" -O3 -std=c++17 -pthread tilefish.cpp -o tilefish || exit 1
fi
echo "Done. Run ./tilefish  (type 'help' for commands, 'play' to play)."
