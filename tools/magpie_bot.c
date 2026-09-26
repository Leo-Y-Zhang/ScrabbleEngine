// magpie_bot: exposes MAGPIE's PlayChooser (its full-strength move picker:
// simulation, pre-endgame and endgame solvers) through the small line protocol
// used by tools/referee.py, so Tilefish can play MAGPIE under a neutral referee.
//
// Build inside a MAGPIE checkout with tools/build_magpie_bot.sh.  The build
// script patches one constant: the chooser's endgame transposition table is cut
// from 20% to 4% of system memory so that four engines can run side by side.
//
// Protocol (one command per line on stdin, answers on stdout):
//   position cgp <CGP>     set the position (rack of the player to move first)
//   go movetime <ms>       answer "bestmove <move>"  (8D WORD, D8 WORD, exch ABC, pass)
//   isready                answer "readyok"
//   quit
// Arguments: LEXICON [THREADS [DATA_DIR [full|sim|static]]]
#include "../src/ent/game.h"
#include "../src/ent/move.h"
#include "../src/impl/config.h"
#include "../src/impl/exec.h"
#include "../src/impl/gameplay.h"
#include "../src/impl/play_chooser.h"
#include "../src/str/move_string.h"
#include "../src/util/io_util.h"
#include "../src/util/string_util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_load_win_pcts(Config *config, ErrorStack *error_stack);

static void die_on_error(ErrorStack *es, const char *what) {
  if (!error_stack_is_empty(es)) {
    fprintf(stderr, "magpie_bot: %s failed\n", what);
    error_stack_print_and_reset(es);
    exit(1);
  }
}

int main(int argc, char **argv) {
  const char *lexicon = argc > 1 ? argv[1] : "CSW24";
  const int threads = argc > 2 ? atoi(argv[2]) : 1;
  const char *data = argc > 3 ? argv[3] : "./data";
  // Mode: "full" (default: sim, pre-endgame and endgame solvers), "sim" (no
  // pre-endgame solver), or "static" (best static equity, no search at all).
  const char *mode = argc > 4 ? argv[4] : "full";
  ErrorStack *es = error_stack_create();
  const ConfigArgs args = {.data_paths = data, .settings_filename = NULL, .use_wmp = true};
  Config *config = config_create(&args, es);
  die_on_error(es, "config_create");
  char setcmd[256];
  snprintf(setcmd, sizeof setcmd, "set -lex %s -threads %d -savesettings false", lexicon, threads);
  char *out = NULL;
  run_str_api_command(config, es, setcmd, &out);
  free(out);
  die_on_error(es, "set");
  config_load_win_pcts(config, es);
  die_on_error(es, "win_pcts");
  uint64_t seed = 12345;
  // One chooser for the whole session: its endgame transposition table is
  // allocated on first use and then reused (as in MAGPIE's own autoplay).
  PlayChooser *pc = NULL;
  double pc_ms = -1;
  char line[4096];
  setvbuf(stdout, NULL, _IOLBF, 0);
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\r\n")] = 0;
    if (!strncmp(line, "quit", 4)) break;
    if (!strcmp(line, "isready")) {
      printf("readyok\n");
      continue;
    }
    if (!strncmp(line, "position cgp ", 13)) {
      char cmd[4200];
      snprintf(cmd, sizeof cmd, "cgp %s", line + 13);
      out = NULL;
      run_str_api_command(config, es, cmd, &out);
      free(out);
      if (!error_stack_is_empty(es)) {
        printf("error bad cgp\n");
        error_stack_print_and_reset(es);
        continue;
      }
      // A CGP from the referee hides the opponent's rack, so MAGPIE would count
      // those tiles as still in the bag.  Deal the opponent a random rack from
      // the unseen tiles: its searches resample that rack and never peek at it,
      // and the bag size (sim vs pre-endgame vs endgame) is then right.
      {
        Game *game = config_get_game(config);
        const int opp = 1 - game_get_player_on_turn_index(game);
        if (rack_is_empty(player_get_rack(game_get_player(game, opp)))) draw_to_full_rack(game, opp);
      }
      continue;
    }
    if (!strncmp(line, "go", 2)) {
      double ms = 1000;
      const char *p = strstr(line, "movetime");
      if (p) ms = atof(p + 8);
      Game *game = config_get_game(config);
      if (!pc || ms != pc_ms) {
        if (pc) play_chooser_destroy(pc);
        PlayChooserStrategy st;
        memset(&st, 0, sizeof st);
        st.pre_endgame_eval = PLAY_CHOOSER_EVAL_PEG;
        st.endgame_eval = PLAY_CHOOSER_EVAL_ENDGAME;
        if (!strcmp(mode, "sim")) st.pre_endgame_eval = PLAY_CHOOSER_EVAL_SIM;
        if (!strcmp(mode, "static")) {
          st.pre_endgame_eval = PLAY_CHOOSER_EVAL_STATIC;
          st.endgame_eval = PLAY_CHOOSER_EVAL_STATIC;
        }
        st.fixed_seconds_per_move = ms / 1000.0;
        st.win_pcts = config_get_win_pcts(config);
        st.num_threads = threads;
        st.seed = seed++;
        pc = play_chooser_create(&st);
        pc_ms = ms;
      }
      Move m;
      play_chooser_choose_move(pc, game, &m, es);
      if (!error_stack_is_empty(es)) {
        error_stack_print_and_reset(es);
        printf("bestmove pass\n");
        continue;
      }
      StringBuilder *sb = string_builder_create();
      string_builder_add_move(sb, game_get_board(game), &m, game_get_ld(game), false);
      const char *s = string_builder_peek(sb);
      if (!strncmp(s, "(exch ", 6)) {
        char buf[64];
        snprintf(buf, sizeof buf, "exch %s", s + 6);
        buf[strcspn(buf, ")")] = 0;
        printf("bestmove %s\n", buf);
      } else {
        printf("bestmove %s\n", s);
      }
      string_builder_destroy(sb);
      continue;
    }
  }
  if (pc) play_chooser_destroy(pc);
  config_destroy(config);
  error_stack_destroy(es);
  return 0;
}
