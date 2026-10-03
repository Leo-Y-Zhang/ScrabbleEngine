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

# Record exactly what was built, so every match can name its opponent (pass this file
# to tools/referee.py with --a-info or --b-info).
{
  echo "engine: Macondo"
  echo "commit: $(git -C "$M" rev-parse HEAD 2>/dev/null || echo unknown)"
  echo "tracked files changed: $(git -C "$M" status --porcelain --untracked-files=no 2>/dev/null | wc -l | tr -d ' ') (threads.patch)"
  echo "toolchain: $(go version 2>&1 | head -n 1)"
  echo "built: $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "settings: BestBot settings from cmd/lambda (bot type and MinSimPlies 5; bot and plies can be overridden on the command line); endgame table 4% of RAM"
} > "$M/bin/macondo_bot.provenance"
cat "$M/bin/macondo_bot.provenance"
echo "data folder: lexica/gaddag/<LEX>.kwg and <LEX>.klv2, plus Macondo's data/strategy and"
echo "data/letterdistributions (symbolic links are fine)"
