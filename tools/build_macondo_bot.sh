#!/bin/sh
# Builds macondo_bot inside a Macondo checkout:   tools/build_macondo_bot.sh /path/to/macondo
# Macondo (github.com/domino14/macondo) is the engine behind Woogles' BestBot; the bot
# plays with BestBot's settings (see tools/macondo_bot/main.go).  Needs Go.
set -e
M=${1:?usage: build_macondo_bot.sh MACONDO_DIR}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$M/cmd/tfbot"
cp "$HERE/macondo_bot/main.go" "$M/cmd/tfbot/main.go"
cd "$M"
# The endgame and pre-endgame solvers start one worker per CPU of the machine; the
# patch makes them use the thread count given to the bot, like the simulation.
grep -q egThreads ai/bot/elite.go || patch -p1 < "$HERE/macondo_bot/threads.patch"
go build -o bin/macondo_bot ./cmd/tfbot
echo "built $M/bin/macondo_bot"
echo "data folder: lexica/gaddag/<LEX>.kwg and <LEX>.klv2, plus Macondo's data/strategy and"
echo "data/letterdistributions (symbolic links are fine)"
