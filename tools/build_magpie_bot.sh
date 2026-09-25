#!/bin/sh
# Builds magpie_bot inside a MAGPIE checkout:   tools/build_magpie_bot.sh /path/to/MAGPIE
# MAGPIE must already be built with `make magpie BUILD=no_pgo_release`.
set -e
M=${1:?usage: build_magpie_bot.sh MAGPIE_DIR}
HERE=$(cd "$(dirname "$0")" && pwd)
OBJ=$M/obj/no_pgo_release-b15-r7
mkdir -p "$M/tools"
cp "$HERE/magpie_bot.c" "$M/tools/magpie_bot.c"
# 4% instead of 20% of RAM for the endgame table, so parallel matches fit in memory.
sed 's/PLAY_CHOOSER_ENDGAME_TT_FRACTION = 0.2;/PLAY_CHOOSER_ENDGAME_TT_FRACTION = 0.04;/' \
  "$M/src/impl/play_chooser.c" > "$M/tools/play_chooser_bot.c"
grep -q "TT_FRACTION = 0.04" "$M/tools/play_chooser_bot.c"
cd "$M/tools"
OBJS=$(find "$OBJ/src" -name '*.o' ! -name 'play_chooser.o')
cc -O3 -march=native -flto -DNDEBUG -DBOARD_DIM=15 -DRACK_SIZE=7 -w -I"$M/src/impl" \
  magpie_bot.c play_chooser_bot.c $OBJS -pthread -flto -lm -o "$M/bin/magpie_bot"
echo "built $M/bin/magpie_bot"
