// SPDX-License-Identifier: GPL-3.0-or-later
// (Built against Macondo, which is GPL-3.0.  Tilefish itself contains no Macondo code.)
//
// macondo_bot: Macondo's bot (the decision code behind Woogles' BestBot) behind the
// line protocol of Tilefish's tools/referee.py, so the two can play each other
// under a neutral referee.  Build it with tools/build_macondo_bot.sh.
//
// It is set up the way cmd/lambda (BestBot's production entry point) sets it up:
// the same bot type, MinSimPlies 5, and a per-move deadline.  The referee hides
// the opponent's rack, so the unseen tiles are all in the bag, as in production.
//
// Protocol (one command per line on stdin, answers on stdout):
//
//	position cgp <CGP>     set the position (rack of the player to move first)
//	go movetime <ms>       answer "bestmove <move>"  (8D WORD, D8 WORD, exch ABC, pass),
//	                       after an "info <JSON>" line: set-up and search time, Macondo's
//	                       own summary of the search, and whether inference ran
//	isready                answer "readyok"
//	quit
//
// Arguments: DATA_PATH [LEXICON [THREADS [BOT [PLIES]]]]
// BOT: simming (SIMMING_BOT, the default) or infer (SIMMING_INFER_BOT).
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"os"
	"runtime"
	"strconv"
	"strings"
	"time"

	"github.com/rs/zerolog"
	"github.com/rs/zerolog/log"

	aibot "github.com/domino14/macondo/ai/bot"
	"github.com/domino14/macondo/cgp"
	"github.com/domino14/macondo/config"
	"github.com/domino14/macondo/game"
	pb "github.com/domino14/macondo/gen/api/proto/macondo"
	"github.com/domino14/macondo/move"
)

func main() {
	if len(os.Args) < 2 {
		fmt.Fprintln(os.Stderr, "usage: macondo_bot DATA_PATH [LEXICON [THREADS [simming|infer [PLIES]]]]")
		os.Exit(2)
	}
	dataPath := os.Args[1]
	lex := "CSW24"
	threads := 1
	botType := pb.BotRequest_SIMMING_BOT
	plies := 5
	if len(os.Args) > 2 {
		lex = os.Args[2]
	}
	if len(os.Args) > 3 {
		threads, _ = strconv.Atoi(os.Args[3])
	}
	if len(os.Args) > 4 && os.Args[4] == "infer" {
		botType = pb.BotRequest_SIMMING_INFER_BOT
	}
	if len(os.Args) > 5 {
		plies, _ = strconv.Atoi(os.Args[5])
	}
	// The endgame and pre-endgame solvers use every CPU the machine reports;
	// GOMAXPROCS keeps the whole process to its share.
	runtime.GOMAXPROCS(threads)
	zerolog.SetGlobalLevel(zerolog.Disabled)
	if os.Getenv("MACONDO_BOT_LOG") != "" {
		zerolog.SetGlobalLevel(zerolog.InfoLevel)
	}

	cfg := config.DefaultConfig()
	cfg.Set(config.ConfigDataPath, dataPath)
	cfg.Set(config.ConfigDefaultLexicon, lex)
	// The endgame transposition table takes 20% of system memory by default; 4%
	// lets four engines run side by side (the MAGPIE driver does the same).
	cfg.Set(config.ConfigTtableMemFraction, 0.04)

	in := bufio.NewScanner(os.Stdin)
	in.Buffer(make([]byte, 1<<20), 1<<20)
	out := bufio.NewWriter(os.Stdout)
	say := func(s string) {
		out.WriteString(s + "\n")
		out.Flush()
	}
	pos := ""
	for in.Scan() {
		line := strings.TrimSpace(in.Text())
		switch {
		case line == "quit":
			return
		case line == "isready":
			say("readyok")
		case strings.HasPrefix(line, "position cgp "):
			pos = strings.TrimSpace(line[len("position cgp "):])
		case strings.HasPrefix(line, "go"):
			ms := 1000.0
			if i := strings.Index(line, "movetime"); i >= 0 {
				if v, err := strconv.ParseFloat(strings.TrimSpace(line[i+len("movetime"):]), 64); err == nil {
					ms = v
				}
			}
			info := map[string]interface{}{}
			mv := best(cfg, pos, lex, ms, threads, botType, plies, info)
			if b, err := json.Marshal(info); err == nil {
				say("info " + string(b))
			}
			say("bestmove " + mv)
		}
	}
}

func best(cfg *config.Config, pos, lex string, ms float64, threads int, botType pb.BotRequest_BotCode, plies int,
	info map[string]interface{}) string {
	start := time.Now()
	// The deadline covers everything from receiving "go", setup included.
	left := time.Duration(ms*0.97)*time.Millisecond - time.Since(start)
	if m := search(cfg, pos, lex, ms, threads, botType, plies, left, info); m != nil {
		return notation(m)
	}
	info["retry"] = true
	// Macondo's endgame solver can be cut off before its first iteration and
	// return an empty line (elite.go then panics on seq[0]).  Its production
	// budgets are long enough never to see this; here it gets one more search
	// with a fresh budget, and then its best static play, rather than forfeit.
	retry := time.Duration(ms/2) * time.Millisecond
	if retry < time.Second {
		retry = time.Second
	}
	if m := search(cfg, pos, lex, ms, threads, botType, plies, retry, info); m != nil {
		return notation(m)
	}
	info["fallback"] = "static"
	g, err := cgp.ParseCGP(cfg, pos+" lex "+lex+";")
	if err != nil {
		fmt.Fprintln(os.Stderr, "macondo_bot: bad cgp:", err)
		return "pass"
	}
	tp, err := aibot.NewBotTurnPlayerFromGame(g.Game, &aibot.BotConfig{Config: *cfg}, pb.BotRequest_HASTY_BOT)
	if err != nil {
		return "pass"
	}
	if moves := tp.GenerateMoves(1); len(moves) > 0 {
		fmt.Fprintln(os.Stderr, "macondo_bot: fell back to the static best play")
		return notation(moves[0])
	}
	return "pass"
}

// search runs the bot on the position with the given budget.  It returns nil
// when the bot fails or panics.
func search(cfg *config.Config, pos, lex string, ms float64, threads int, botType pb.BotRequest_BotCode, plies int,
	budget time.Duration, info map[string]interface{}) (m *move.Move) {
	defer func() {
		if r := recover(); r != nil {
			fmt.Fprintln(os.Stderr, "macondo_bot: bot panicked:", r)
			m = nil
		}
	}()
	start := time.Now()
	g, err := cgp.ParseCGP(cfg, pos+" lex "+lex+";")
	if err != nil {
		fmt.Fprintln(os.Stderr, "macondo_bot: bad cgp:", err)
		return nil
	}
	// Opponent-rack inference gets a quarter of the move, in whole seconds (the
	// setting's unit); production gives it up to 20 s of a longer move.
	infer := int(ms / 4000)
	if infer < 1 {
		infer = 1
	}
	conf := &aibot.BotConfig{Config: *cfg, MinSimPlies: plies, SimThreads: threads, InferenceTimeSecs: infer}
	tp, err := aibot.NewBotTurnPlayerFromGame(g.Game, conf, botType)
	if err != nil {
		fmt.Fprintln(os.Stderr, "macondo_bot: bot:", err)
		return nil
	}
	tp.SetBackupMode(game.InteractiveGameplayMode)
	tp.SetStateStackLength(1)
	tp.RecalculateBoard()
	info["bag"] = g.Game.Bag().TilesRemaining()
	info["setup_s"] = time.Since(start).Seconds()
	left := budget - time.Since(start)
	if left < 10*time.Millisecond {
		left = 10 * time.Millisecond
	}
	ctx, cancel := context.WithTimeout(context.Background(), left)
	defer cancel()
	if os.Getenv("MACONDO_BOT_LOG") != "" {
		ctx = log.Logger.WithContext(ctx)
	}
	t1 := time.Now()
	m, err = tp.BestPlay(ctx)
	info["search_s"] = time.Since(t1).Seconds()
	// -1: inference not attempted (simming bot), 0: attempted, nothing inferred
	// (a game built from a CGP has no history to infer from).
	info["inferred"] = tp.LastInferenceCount()
	info["details"] = tp.BestPlayDetails(ctx)
	if err != nil {
		fmt.Fprintln(os.Stderr, "macondo_bot: best play:", err)
		return nil
	}
	return m
}

// notation writes a move the way the referee reads it.
func notation(m *move.Move) string {
	switch m.Action() {
	case move.MoveTypePlay:
		return strings.TrimSpace(m.BoardCoords()) + " " + m.TilesString()
	case move.MoveTypeExchange:
		return "exch " + m.TilesStringExchange()
	default:
		return "pass"
	}
}
