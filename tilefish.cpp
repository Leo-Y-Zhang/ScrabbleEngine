// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * =====================================================================================
 *   TILEFISH 2.1  -  a championship-style Scrabble(R) engine in one C++17 file
 * =====================================================================================
 *
 *  "Stockfish, but for tiles."  Everything lives in this single file: the lexicon
 *  compiler, move generator, evaluation, Monte-Carlo simulation, endgame and
 *  pre-endgame solvers, opponent-rack inference, self-play training, engine-vs-engine
 *  matches and an interactive terminal UI.  No dependencies beyond the C++ standard
 *  library.
 *
 *  BUILD
 *    Linux / macOS / WSL / MinGW:
 *        g++ -O3 -march=native -std=c++17 -pthread tilefish.cpp -o tilefish
 *    (clang++ works the same way.)
 *    Windows (Visual Studio "x64 Native Tools" prompt):
 *        cl /O2 /std:c++17 /EHsc tilefish.cpp
 *
 *  QUICK START
 *        ./tilefish                 (finds ENABLE.txt + trained data in the folder)
 *    then type `help`.  Highlights:
 *        play                      play a game against the engine
 *        review game.gcg 10        chess-style game review: every move vs the engine
 *        gcg game.gcg 14 / cgp ... set up a position;  go / sim / endgame / peg
 *        autoplay 1000 sim static  engine-vs-engine match with statistics
 *        train games=100000        self-play training for the loaded lexicon
 *        selftest                  correctness checks against brute force
 *    For GUIs, broadcast overlays and scripts: `tilefish --quiet`, then send
 *    commands on stdin (`cgp ...`, `go 10 json`, `isready`); answers are single lines
 *    (JSON for `go ... json`).
 *
 *  A LEXICON IS REQUIRED.  Any plain word list works (one word per line, extra
 *  columns such as definitions are ignored), and so does a binary .kwg lexicon as
 *  used by wolges, MAGPIE and Macondo.  Tournament play uses CSW (Collins, WESPA /
 *  world championship) or NWL (NASPA, North America).  Those lists are copyrighted,
 *  so they are not bundled: load your own copy with --lexicon, together with leave
 *  values for it (FILE.klv2 or FILE.leaves next to it is picked up automatically),
 *  or run `train` so the engine learns leave values for that exact dictionary.
 *
 *  HOW THE ENGINE THINKS  (chess analogies in brackets)
 *    1. Move generation   GADDAG (Gordon 1994) compiled into a compact node array
 *                         generates every legal play, exchange and pass.  The best
 *                         play alone (the inner loop of simulation) is found
 *                         best-first: every span of every anchor gets an upper
 *                         bound, spans are searched best bound first across the
 *                         board, and the words for each subset of the rack come
 *                         from anagram maps.   [movegen]
 *    2. Static equity     score + value(tiles kept) + end-of-game adjustments.
 *                         Leave values are *learned by self-play*, because face
 *                         values mis-price tiles (S and ? are worth far more than
 *                         their points, Q/V/U far less).   [evaluation function]
 *    3. Simulation        For the best candidates, sample the unseen tiles many
 *                         times, play the game forward a few turns and measure
 *                         both spread and WIN PROBABILITY.  Candidates that are
 *                         statistically beaten get pruned early (successive
 *                         elimination), so time goes to the moves that matter.
 *                         Common random numbers: every candidate sees the same
 *                         bags, so luck cancels out.   [search]
 *    4. Endgame           Bag empty = perfect information.  Negamax + alpha-beta,
 *                         iterative deepening, transposition table: exact
 *                         solutions.  Each side's plays are generated once and
 *                         filtered as tiles land; at the depth limit both sides
 *                         play greedily to the end.   [tablebase-like]
 *    5. Pre-endgame       One tile in the bag: every possible draw is enumerated,
 *                         each resulting endgame is valued by greedy play-outs,
 *                         and the leading candidates are searched deeper.
 *    6. Inference         The opponent's last play tells us about the tiles they
 *                         kept; the sampler weights their possible racks.
 *    7. Self-play         `train` plays thousands of games against itself and
 *                         re-learns leave values + the win model. [AlphaZero-ish]
 *
 *  LICENCE
 *    Copyright (C) 2026 the Tilefish authors.  Tilefish is free software: you can
 *    redistribute it and/or modify it under the terms of the GNU General Public
 *    License as published by the Free Software Foundation, either version 3 of the
 *    License, or (at your option) any later version.  It is distributed WITHOUT ANY
 *    WARRANTY; see the LICENSE file for details.
 *
 *  Scrabble is a registered trademark of Hasbro (North America) and Mattel
 *  (elsewhere).  This program is an independent implementation of the published
 *  algorithms; it contains no code from other engines.
 * =====================================================================================
 */

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace tf {

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i16 = int16_t;

// =====================================================================================
// §1  Utilities: clock, random numbers, strings
// =====================================================================================

inline double now_s() {
  using namespace std::chrono;
  return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline u64 mix64(u64 z) {
  z += 0x9E3779B97F4A7C15ULL;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

// xoshiro256** : fast, high quality, reproducible across platforms.
struct Rng {
  u64 s[4];
  explicit Rng(u64 seed = 0x243F6A8885A308D3ULL) { seed_with(seed); }
  void seed_with(u64 seed) {
    u64 x = seed;
    for (int i = 0; i < 4; ++i) {
      x = mix64(x + (u64)i);
      s[i] = x;
    }
    if (!(s[0] | s[1] | s[2] | s[3])) s[0] = 1;
  }
  static inline u64 rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }
  inline u64 next() {
    const u64 result = rotl(s[1] * 5, 7) * 9;
    const u64 t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
  }
  // Uniform integer in [0, n).
  inline u32 below(u32 n) { return (u32)(((next() >> 32) * (u64)n) >> 32); }
  inline double uniform() { return (double)(next() >> 11) * (1.0 / 9007199254740992.0); }
};

// Index of the highest set bit (x != 0).
inline int highest_bit(uint32_t x) {
#if defined(_MSC_VER)
  unsigned long idx;
  _BitScanReverse(&idx, x);
  return (int)idx;
#else
  return 31 - __builtin_clz(x);
#endif
}

// Index of the lowest set bit (x != 0).
inline int lowest_bit64(u64 x) {
#if defined(_MSC_VER)
  unsigned long idx;
  _BitScanForward64(&idx, x);
  return (int)idx;
#else
  return __builtin_ctzll(x);
#endif
}

inline u64 time_seed() {
  return mix64((u64)std::chrono::high_resolution_clock::now().time_since_epoch().count());
}

inline std::string trim(const std::string& s) {
  size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  size_t b = s.find_last_not_of(" \t\r\n");
  return s.substr(a, b - a + 1);
}

inline std::vector<std::string> split_ws(const std::string& s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  std::string t;
  while (is >> t) out.push_back(t);
  return out;
}

inline std::string to_upper(std::string s) {
  for (auto& c : s) c = (char)std::toupper((unsigned char)c);
  return s;
}

inline std::string to_lower(std::string s) {
  for (auto& c : s) c = (char)std::tolower((unsigned char)c);
  return s;
}

inline bool starts_with(const std::string& s, const std::string& p) {
  return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

inline std::string fmt(const char* f, ...) {
  char buf[1024];
  va_list ap;
  va_start(ap, f);
  vsnprintf(buf, sizeof buf, f, ap);
  va_end(ap);
  return buf;
}

// Parses "key=value" style options ("iters=500", "plies=2").
inline std::map<std::string, std::string> parse_kv(const std::string& s, char sep = ',') {
  std::map<std::string, std::string> kv;
  std::string item;
  std::istringstream is(s);
  while (std::getline(is, item, sep)) {
    auto p = item.find('=');
    if (p == std::string::npos) kv[trim(item)] = "1";
    else kv[trim(item.substr(0, p))] = trim(item.substr(p + 1));
  }
  return kv;
}

// =====================================================================================
// §2  Tiles, alphabet and board geometry (standard English Scrabble)
// =====================================================================================

constexpr int N = 15;             // board is 15 x 15 = 225 squares
constexpr int NSQ = N * N;
constexpr int RACK_SIZE = 7;
constexpr int BINGO_BONUS = 50;   // using all 7 tiles
constexpr int NLET = 27;          // rack alphabet: 0 = blank '?', 1..26 = A..Z
constexpr int BLANK = 0;
constexpr u8 BLANK_BIT = 0x80;    // on the board, a blank is stored as letter | BLANK_BIT
constexpr int SEP = 0;            // GADDAG separator label (sorts before every letter)
constexpr u32 ALL_LETTERS = 0x7FFFFFEu;  // bits 1..26
constexpr int CENTER = 7 * N + 7;
constexpr int TOTAL_TILES = 100;
constexpr int MAX_UNSEEN = TOTAL_TILES - RACK_SIZE;

//                               ?  A  B  C  D  E   F  G  H  I  J  K  L  M  N  O  P  Q   R  S  T  U  V  W  X  Y  Z
constexpr int TILE_COUNT[NLET] = {2, 9, 2, 2, 4, 12, 2, 3, 2, 9, 1, 1, 4, 2, 6, 8, 2, 1, 6, 4, 6, 4, 2, 2, 1, 2, 1};
constexpr int TILE_SCORE[NLET] = {0, 1, 3, 3, 2, 1, 4, 2, 4, 1, 8, 5, 1, 3, 1, 1, 3, 10, 1, 1, 1, 1, 4, 4, 8, 4, 10};

inline bool is_vowel(int L) { return L == 1 || L == 5 || L == 9 || L == 15 || L == 21; }

inline char rack_char(int L) { return L == BLANK ? '?' : char('A' + L - 1); }
// '?' or '_' is a blank; letters are case-insensitive for racks.
inline int char_to_rack(char c) {
  if (c == '?' || c == '_') return BLANK;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
  if (c >= 'a' && c <= 'z') return c - 'a' + 1;
  return -1;
}
inline int tile_letter(u8 t) { return t & 31; }
inline bool tile_blank(u8 t) { return (t & BLANK_BIT) != 0; }
inline int tile_face(u8 t) { return (t & BLANK_BIT) ? 0 : TILE_SCORE[t & 31]; }
// Upper case = real tile, lower case = blank standing for that letter.
inline char tile_char(u8 t) {
  char c = char('A' + (t & 31) - 1);
  return (t & BLANK_BIT) ? char(c - 'A' + 'a') : c;
}
// Which rack tile a board tile consumes.
inline int tile_rack_code(u8 t) { return (t & BLANK_BIT) ? BLANK : (t & 31); }

// '=' triple word, '-' double word, '"' triple letter, '\'' double letter.
static const char* const LAYOUT[N] = {
    "=  '   =   '  =",
    " -   \"   \"   - ",
    "  -   ' '   -  ",
    "'  -   '   -  '",
    "    -     -    ",
    " \"   \"   \"   \" ",
    "  '   ' '   '  ",
    "=  '   -   '  =",
    "  '   ' '   '  ",
    " \"   \"   \"   \" ",
    "    -     -    ",
    "'  -   '   -  '",
    "  -   ' '   -  ",
    " -   \"   \"   - ",
    "=  '   =   '  =",
};

struct PremiumTable {
  u8 lm[NSQ];  // letter multiplier
  u8 wm[NSQ];  // word multiplier
  PremiumTable() {
    for (int r = 0; r < N; ++r)
      for (int c = 0; c < N; ++c) {
        const char ch = LAYOUT[r][c];
        const int s = r * N + c;
        lm[s] = 1;
        wm[s] = 1;
        if (ch == '\'') lm[s] = 2;
        else if (ch == '"') lm[s] = 3;
        else if (ch == '-') wm[s] = 2;
        else if (ch == '=') wm[s] = 3;
      }
  }
};
static const PremiumTable PREM;

inline std::string square_name(int r, int c) { return std::string(1, char('A' + c)) + std::to_string(r + 1); }

// A multiset of tiles (a rack, a leave, the bag, the unseen pool...).
struct Rack {
  int8_t c[NLET];
  int n;
  Rack() { clear(); }
  void clear() {
    std::memset(c, 0, sizeof c);
    n = 0;
  }
  void add(int L, int k = 1) {
    c[L] = (int8_t)(c[L] + k);
    n += k;
  }
  void sub(int L, int k = 1) {
    c[L] = (int8_t)(c[L] - k);
    n -= k;
  }
  int face() const {
    int s = 0;
    for (int L = 1; L < NLET; ++L) s += c[L] * TILE_SCORE[L];
    return s;
  }
  bool contains(const Rack& o) const {
    for (int L = 0; L < NLET; ++L)
      if (o.c[L] > c[L]) return false;
    return true;
  }
  void add_all(const Rack& o) {
    for (int L = 0; L < NLET; ++L) c[L] = (int8_t)(c[L] + o.c[L]);
    n += o.n;
  }
  void sub_all(const Rack& o) {
    for (int L = 0; L < NLET; ++L) c[L] = (int8_t)(c[L] - o.c[L]);
    n -= o.n;
  }
  // Letters in alphabetical order, blanks last ("AEINRST?").
  std::string str() const {
    std::string s;
    for (int L = 1; L < NLET; ++L)
      for (int k = 0; k < c[L]; ++k) s += rack_char(L);
    for (int k = 0; k < c[0]; ++k) s += '?';
    return s;
  }
  static bool parse(const std::string& s, Rack& r) {
    r.clear();
    for (char ch : s) {
      const int L = char_to_rack(ch);
      if (L < 0) return false;
      r.add(L);
    }
    return true;
  }
  bool operator==(const Rack& o) const { return n == o.n && std::memcmp(c, o.c, sizeof c) == 0; }
  bool operator!=(const Rack& o) const { return !(*this == o); }
  static Rack full_distribution() {
    Rack r;
    for (int L = 0; L < NLET; ++L) r.add(L, TILE_COUNT[L]);
    return r;
  }
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §3  Lexicon: a minimised DAWG + GADDAG packed into one array of 32-bit arcs
// =====================================================================================
//
//  Every arc is one u32:
//      bits  0..21  index of the child's arc list (0 = no children)
//      bit   22     last arc of its sibling list
//      bit   23     "accepts": the path ending with this arc spells a complete entry
//      bits 24..31  label (0 = GADDAG separator, 1..26 = A..Z)
//  Sibling lists are sorted by label, so the separator (label 0) always comes first.
//
//  DAWG   : holds the words themselves; used to validate words and build cross-checks.
//  GADDAG : for a word w and each split point i (1..n) holds  reverse(w[0..i)) ^ w[i..n)
//           (the separator is omitted when the suffix is empty).  Starting from the
//           anchor square it lets the generator grow words leftwards, then turn right.
//
//  Both automata are built from sorted input with Daciuk et al.'s incremental
//  minimisation and share identical sub-structures.

// A set of letter multisets ("which bags of letters spell some word?") used to
// tighten the move generator's upper bounds.  A multiset's key is the sum of a
// random 64-bit number per letter, so the key of a union is the sum of the keys.
// Only 16-bit fingerprints are stored, in a cache-friendly open-addressing table:
// a lookup can wrongly answer "yes" (harmless: a looser bound) but never "no"
// for a key that was inserted.
class WordFilter {
 public:
  void build(std::vector<u64>& keys) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    int bits = 10;
    while ((1ull << bits) < keys.size() * 2) ++bits;
    shift_ = 64 - bits;
    mask_ = (1u << bits) - 1;
    t_.assign((size_t)1 << bits, 0);
    for (u64 k : keys) {
      const u16 f = fp(k);
      u32 i = (u32)(k >> shift_);
      while (t_[i] && t_[i] != f) i = (i + 1) & mask_;
      t_[i] = f;
    }
  }
  inline bool has(u64 k) const {
    const u16 f = fp(k);
    for (u32 i = (u32)(k >> shift_);; i = (i + 1) & mask_) {
      const u16 v = t_[i];
      if (v == f) return true;
      if (!v) return false;
    }
  }
  bool empty() const { return t_.empty(); }

 private:
  std::vector<u16> t_;
  int shift_ = 64;
  u32 mask_ = 0;
  static inline u16 fp(u64 k) {
    const u16 f = (u16)(k >> 5);
    return f ? f : 1;
  }
};

// Anagram map: letter-multiset key -> the words spelled by exactly those letters.
// The one-blank variant maps key(word - x) -> (word, x) for every distinct letter x
// of the word, i.e. "these tiles plus a blank as x spell this word".
class AnagramMap {
 public:
  // Values are word ids (optionally | letter << 24).  Builds from (key, value) pairs.
  void build(std::vector<std::pair<u64, u32>>& kv) {
    std::sort(kv.begin(), kv.end());
    size_t distinct = 0;
    for (size_t i = 0; i < kv.size(); ++i) distinct += (i == 0 || kv[i].first != kv[i - 1].first);
    int bits = 10;
    while ((1ull << bits) < distinct + distinct / 2) ++bits;
    shift_ = 64 - bits;
    mask_ = (1u << bits) - 1;
    slots_.assign((size_t)1 << bits, Slot());  // empty slots hold the largest key
    vals_.resize(kv.size());
    for (size_t i = 0; i < kv.size();) {
      size_t j = i;
      while (j < kv.size() && kv[j].first == kv[i].first) {
        vals_[j] = kv[j].second;
        ++j;
      }
      u32 h = (u32)(kv[i].first >> shift_);
      while (slots_[h].n) h = (h + 1) & mask_;
      slots_[h] = Slot{kv[i].first, (u32)i, (u32)(j - i)};
      i = j;
    }
  }
  // Returns the number of values for `key` and sets `out` to them.  Keys went in in
  // ascending order, so every slot a probe passes before reaching its key holds a
  // smaller key: the first slot holding a key >= `key` decides.
  inline int find(u64 key, const u32*& out) const {
    for (u32 h = (u32)(key >> shift_);; h = (h + 1) & mask_) {
      const Slot& sl = slots_[h];
      if (sl.key >= key) {
        if (sl.key != key || !sl.n) return 0;
        out = vals_.data() + sl.off;
        return (int)sl.n;
      }
    }
  }
  bool empty() const { return slots_.empty(); }
  size_t bytes() const { return slots_.size() * sizeof(Slot) + vals_.size() * 4; }

 private:
  struct Slot {
    u64 key = ~0ull;
    u32 off = 0, n = 0;
  };
  std::vector<Slot> slots_;
  std::vector<u32> vals_;
  int shift_ = 64;
  u32 mask_ = 0;
};

class Lexicon {
 public:
  std::vector<u32> nodes;
  u32 dawg_root = 0;    // index of the DAWG root arc list
  u32 gaddag_root = 0;  // index of the GADDAG root arc list
  size_t nwords = 0;
  std::string name;

  static constexpr u32 CHILD_MASK = 0x3FFFFFu;
  static constexpr u32 END_BIT = 1u << 22;
  static constexpr u32 ACCEPT_BIT = 1u << 23;
  static inline u32 child(u32 v) { return v & CHILD_MASK; }
  static inline bool is_end(u32 v) { return (v & END_BIT) != 0; }
  static inline bool accepts(u32 v) { return (v & ACCEPT_BIT) != 0; }
  static inline int label(u32 v) { return int(v >> 24); }

  bool loaded() const { return !nodes.empty(); }

  // Anagram index used to rule out impossible bingos quickly (move-gen pruning):
  //   bingo7 : sorted letters of every 7-letter word
  //   bingo8 : for every 8-letter word and every letter X in it, sorted(word - X) -> bit X
  std::unordered_set<u64> bingo7;
  std::unordered_map<u64, u32> bingo8;
  // Multiset filters: words (spell0) and words with one letter removed (spell1,
  // i.e. "plus one blank spells a word").  lkey[L] is letter L's random key.
  u64 lkey[NLET] = {0};
  WordFilter spell0, spell1;
  // Word maps for anagram-based move generation (words of 2..15 letters).
  AnagramMap amap0, amap1;
  std::vector<u8> wl_letters;  // all words' letters, concatenated
  std::vector<u32> wl_off;     // word id -> offset into wl_letters (size = words + 1)
  static u64 sorted_key(const int* cnt /* 27 counts, letters 1..26 only */) {
    u64 k = 0;
    for (int L = 1; L < 27; ++L)
      for (int i = 0; i < cnt[L]; ++i) k = (k << 5) | (u64)L;
    return k;
  }
  // For a 7-tile rack (blanks allowed): can it form a 7-letter word, and which
  // board letters X would let it form an 8-letter word?
  void bingo_info(const int* rack_cnt, bool& b7, u32& b8) const {
    b7 = false;
    b8 = 0;
    int cnt[27];
    for (int i = 0; i < 27; ++i) cnt[i] = rack_cnt[i];
    const int blanks = cnt[0];
    cnt[0] = 0;
    auto probe = [&]() {
      const u64 k = sorted_key(cnt);
      if (!b7 && bingo7.count(k)) b7 = true;
      auto it = bingo8.find(k);
      if (it != bingo8.end()) b8 |= it->second;
    };
    if (blanks == 0) {
      probe();
    } else if (blanks == 1) {
      for (int X = 1; X < 27; ++X) {
        ++cnt[X];
        probe();
        --cnt[X];
      }
    } else {
      b7 = true;  // two or more blanks: assume anything is possible
      b8 = ALL_LETTERS;
    }
  }

  // Index of the arc labelled L in the list starting at `list`, or 0.
  inline u32 find(u32 list, int L) const {
    if (!list) return 0;
    for (u32 i = list;; ++i) {
      const u32 v = nodes[i];
      const int t = label(v);
      if (t == L) return i;
      if (t > L || is_end(v)) return 0;
    }
  }

  // w holds letters 1..26.
  bool is_word(const u8* w, int len) const {
    if (len < 2) return false;
    u32 list = dawg_root, arc = 0;
    for (int i = 0; i < len; ++i) {
      arc = find(list, w[i] & 31);
      if (!arc) return false;
      list = child(nodes[arc]);
    }
    return accepts(nodes[arc]);
  }
  bool is_word(const std::string& s) const {
    u8 w[32];
    if (s.size() > 31) return false;
    for (size_t i = 0; i < s.size(); ++i) {
      const int L = char_to_rack(s[i]);
      if (L <= 0) return false;
      w[i] = (u8)L;
    }
    return is_word(w, (int)s.size());
  }

  // Reads a plain word list.  The first token of each line is the word; anything
  // after it (definitions, probabilities) is ignored.  Lines starting with '#' are
  // comments.  Words must consist of letters only and have 2..15 letters.
  bool load_word_list(const std::string& path, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      err = "cannot open " + path;
      return false;
    }
    std::vector<std::string> words;
    std::string line;
    while (std::getline(in, line)) {
      size_t a = 0;
      while (a < line.size() && std::isspace((unsigned char)line[a])) ++a;
      if (a == line.size() || line[a] == '#') continue;
      size_t b = a;
      while (b < line.size() && !std::isspace((unsigned char)line[b]) && line[b] != ',' && line[b] != ';') ++b;
      std::string w;
      bool ok = true;
      for (size_t i = a; i < b; ++i) {
        const int L = char_to_rack(line[i]);
        if (L <= 0) {
          ok = false;
          break;
        }
        w += (char)L;
      }
      if (!ok || w.size() < 2 || w.size() > (size_t)N) continue;  // not a playable word
      words.push_back(std::move(w));
    }
    if (words.empty()) {
      err = "no usable words found in " + path;
      return false;
    }
    if (!build(words, err)) return false;
    name = base_name(path);
    return true;
  }

  // Builds from words encoded as bytes 1..26.  Sorts/deduplicates in place.
  bool build(std::vector<std::string>& words, std::string& err);

  // Loads a KWG (Kurnia Word Graph, the binary lexicon format of wolges, MAGPIE and
  // Macondo).  Its arcs use exactly our bit layout; arc 0 points at the DAWG root
  // and arc 1 at the GADDAG root.  Only the English alphabet (tiles 1..26) is
  // accepted.
  bool load_kwg(const std::string& path, std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      err = "cannot open " + path;
      return false;
    }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (buf.size() < 8 || buf.size() % 4) {
      err = "not a KWG file (size)";
      return false;
    }
    std::vector<u32> nd(buf.size() / 4);
    for (size_t i = 0; i < nd.size(); ++i)
      nd[i] = (u32)buf[4 * i] | ((u32)buf[4 * i + 1] << 8) | ((u32)buf[4 * i + 2] << 16) | ((u32)buf[4 * i + 3] << 24);
    for (size_t i = 2; i < nd.size(); ++i)
      if (child(nd[i]) >= nd.size() || label(nd[i]) > 26) {
        err = "not an English KWG file (bad arc " + std::to_string(i) + ")";
        return false;
      }
    const u32 d = child(nd[0]), g = child(nd[1]);
    if (!d || !g || d >= nd.size() || g >= nd.size()) {
      err = "KWG without both a DAWG and a GADDAG";
      return false;
    }
    nodes = std::move(nd);
    dawg_root = d;
    gaddag_root = g;
    std::vector<std::string> words;
    for_each_word([&](const std::string& w) {
      if (w.size() <= (size_t)N) words.push_back(w);  // KWGs may hold longer, unplayable words
    });
    nwords = words.size();
    build_bingo_index(words);
    name = base_name(path);
    return true;
  }

  // Writes the graph as a KWG file, which load_kwg reads far faster than a word list is
  // built.  A graph built here keeps index 0 as the null list, so its arcs move up one
  // slot behind the two root arcs; a graph loaded from a KWG is written as it is.
  bool save_kwg(const std::string& path) const {
    if (nodes.empty() || nodes.size() + 1 > CHILD_MASK) return false;
    std::vector<u32> out;
    if (nodes[0] == 0) {
      out.reserve(nodes.size() + 1);
      out.push_back(END_BIT | (dawg_root + 1));
      out.push_back(END_BIT | (gaddag_root + 1));
      for (size_t i = 1; i < nodes.size(); ++i) out.push_back(child(nodes[i]) ? nodes[i] + 1 : nodes[i]);
    } else {
      out = nodes;
    }
    std::ofstream f(path, std::ios::binary);
    for (u32 x : out) {
      const unsigned char b[4] = {(unsigned char)x, (unsigned char)(x >> 8), (unsigned char)(x >> 16), (unsigned char)(x >> 24)};
      f.write((const char*)b, 4);
    }
    return (bool)f;
  }

  static std::string base_name(std::string base) {
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    return base;
  }

  void build_bingo_index(const std::vector<std::string>& words) {
    {
      Rng r(0x5EED5EED1234ULL);
      for (int L = 0; L < NLET; ++L) lkey[L] = L ? r.next() : 0;
      std::vector<u64> k0, k1;
      k0.reserve(words.size());
      k1.reserve(words.size() * 7);
      for (const auto& w : words) {
        if (w.size() > (size_t)N) continue;
        u64 k = 0;
        u32 seen = 0;
        for (char ch : w) k += lkey[(u8)ch];
        k0.push_back(k);
        for (char ch : w) {
          if ((seen >> (u8)ch) & 1u) continue;
          seen |= 1u << (u8)ch;
          k1.push_back(k - lkey[(u8)ch]);
        }
      }
      spell0.build(k0);
      spell1.build(k1);
      std::vector<std::pair<u64, u32>> kv0, kv1;
      wl_letters.clear();
      wl_off.assign(1, 0);
      kv0.reserve(words.size());
      kv1.reserve(words.size() * 7);
      for (const auto& w : words) {
        if (w.size() > (size_t)N || w.size() < 2) continue;
        const u32 id = (u32)wl_off.size() - 1;
        u64 k = 0;
        for (char ch : w) {
          k += lkey[(u8)ch];
          wl_letters.push_back((u8)ch);
        }
        wl_off.push_back((u32)wl_letters.size());
        kv0.push_back({k, id});
        u32 seen = 0;
        for (char ch : w) {
          if ((seen >> (u8)ch) & 1u) continue;
          seen |= 1u << (u8)ch;
          kv1.push_back({k - lkey[(u8)ch], id | ((u32)(u8)ch << 24)});
        }
      }
      amap0.build(kv0);
      amap1.build(kv1);
    }
    bingo7.clear();
    bingo8.clear();
    for (const auto& w : words) {
      if (w.size() != 7 && w.size() != 8) continue;
      int cnt[27] = {0};
      for (char ch : w) cnt[(u8)ch]++;
      if (w.size() == 7) {
        bingo7.insert(sorted_key(cnt));
      } else {
        for (int X = 1; X < 27; ++X) {
          if (!cnt[X]) continue;
          --cnt[X];
          bingo8[sorted_key(cnt)] |= 1u << X;
          ++cnt[X];
        }
      }
    }
  }

  // Enumerates every word (DAWG order).  Used by self-tests.
  void for_each_word(const std::function<void(const std::string&)>& f) const {
    std::string cur;
    walk_words(dawg_root, cur, f);
  }

 private:
  void walk_words(u32 list, std::string& cur, const std::function<void(const std::string&)>& f) const {
    if (!list) return;
    for (u32 i = list;; ++i) {
      const u32 v = nodes[i];
      cur.push_back((char)label(v));
      if (accepts(v)) f(cur);
      walk_words(child(v), cur, f);
      cur.pop_back();
      if (is_end(v)) break;
    }
  }
};

// Incremental construction of a minimal acyclic automaton from sorted strings
// (Daciuk, Mihov, Watson & Watson 2000).
class LexBuilder {
 public:
  struct State {
    std::vector<std::pair<u8, int>> arcs;
    bool fin = false;
  };
  std::vector<State> st;

  LexBuilder() : reg_(1 << 16, Hash{this}, Eq{this}) {}
  LexBuilder(const LexBuilder&) = delete;
  LexBuilder& operator=(const LexBuilder&) = delete;

  int new_state() {
    if (!free_.empty()) {
      const int id = free_.back();
      free_.pop_back();
      st[id].arcs.clear();
      st[id].fin = false;
      return id;
    }
    st.emplace_back();
    return (int)st.size() - 1;
  }

  void add(int root, const u8* w, int len) {
    int s = root, i = 0;
    while (i < len && !st[s].arcs.empty() && st[s].arcs.back().first == w[i]) {
      s = st[s].arcs.back().second;
      ++i;
    }
    if (!st[s].arcs.empty()) replace_or_register(s);
    for (; i < len; ++i) {
      const int ns = new_state();
      st[s].arcs.push_back({w[i], ns});
      s = ns;
    }
    st[s].fin = true;
  }

  void finish(int root) {
    if (!st[root].arcs.empty()) replace_or_register(root);
  }

 private:
  struct Hash {
    const LexBuilder* b;
    size_t operator()(int id) const {
      const State& s = b->st[id];
      u64 h = s.fin ? 0x9E3779B97F4A7C15ULL : 0x632BE59BD9B4E019ULL;
      for (const auto& a : s.arcs) h = mix64(h ^ ((u64)a.first << 40) ^ (u64)(u32)a.second);
      return (size_t)h;
    }
  };
  struct Eq {
    const LexBuilder* b;
    bool operator()(int x, int y) const { return b->st[x].fin == b->st[y].fin && b->st[x].arcs == b->st[y].arcs; }
  };
  std::unordered_set<int, Hash, Eq> reg_;
  std::vector<int> free_;

  void replace_or_register(int s) {
    const int ch = st[s].arcs.back().second;
    if (!st[ch].arcs.empty()) replace_or_register(ch);
    auto it = reg_.find(ch);
    if (it != reg_.end()) {
      st[s].arcs.back().second = *it;
      st[ch].arcs.clear();
      st[ch].arcs.shrink_to_fit();
      free_.push_back(ch);
    } else {
      reg_.insert(ch);
    }
  }
};

inline bool Lexicon::build(std::vector<std::string>& words, std::string& err) {
  std::sort(words.begin(), words.end());
  words.erase(std::unique(words.begin(), words.end()), words.end());
  nwords = words.size();
  if (!nwords) {
    err = "empty lexicon";
    return false;
  }
  std::unique_ptr<LexBuilder> B(new LexBuilder());
  const int droot = B->new_state();
  const int groot = B->new_state();
  for (const auto& w : words) B->add(droot, (const u8*)w.data(), (int)w.size());
  B->finish(droot);

  {
    std::vector<std::string> g;
    size_t total = 0;
    for (const auto& w : words) total += w.size();
    g.reserve(total);
    for (const auto& w : words) {
      const int n = (int)w.size();
      for (int i = 1; i <= n; ++i) {
        std::string s;
        s.reserve(n + 1);
        for (int k = i - 1; k >= 0; --k) s += w[k];
        if (i < n) {
          s += (char)SEP;
          s.append(w, i, n - i);
        }
        g.push_back(std::move(s));
      }
    }
    std::sort(g.begin(), g.end());
    g.erase(std::unique(g.begin(), g.end()), g.end());
    for (const auto& s : g) B->add(groot, (const u8*)s.data(), (int)s.size());
    B->finish(groot);
  }

  nodes.assign(1, 0u);  // index 0 is the null list
  std::vector<u32> memo(B->st.size(), UINT32_MAX);
  std::unordered_map<std::string, u32> seen;
  bool overflow = false;
  std::function<u32(int)> emit = [&](int s) -> u32 {
    if (B->st[s].arcs.empty()) return 0;
    if (memo[s] != UINT32_MAX) return memo[s];
    const size_t k = B->st[s].arcs.size();
    std::vector<u32> packed(k);
    for (size_t i = 0; i < k; ++i) {
      const int t = B->st[s].arcs[i].second;
      const u32 ch = emit(t);
      packed[i] = ((u32)B->st[s].arcs[i].first << 24) | (B->st[t].fin ? ACCEPT_BIT : 0u) | ch;
    }
    packed.back() |= END_BIT;
    std::string key((const char*)packed.data(), packed.size() * sizeof(u32));
    auto it = seen.find(key);
    u32 idx;
    if (it != seen.end()) {
      idx = it->second;
    } else {
      idx = (u32)nodes.size();
      if (idx + k > CHILD_MASK) overflow = true;
      nodes.insert(nodes.end(), packed.begin(), packed.end());
      seen.emplace(std::move(key), idx);
    }
    memo[s] = idx;
    return idx;
  };
  dawg_root = emit(droot);
  gaddag_root = emit(groot);
  if (overflow) {
    nodes.clear();
    err = "lexicon too large for the 22-bit node format";
    return false;
  }
  build_bingo_index(words);
  return true;
}

}  // namespace tf
namespace tf {

// =====================================================================================
// §4  Moves
// =====================================================================================

enum MoveType : u8 { MT_PLACE = 0, MT_EXCHANGE = 1, MT_PASS = 2 };

struct Move {
  u8 type = MT_PASS;
  u8 row = 0, col = 0;  // first square of the main word
  u8 dir = 0;           // 0 = across, 1 = down
  u8 len = 0;           // PLACE: squares spanned by the main word; EXCHANGE: tiles exchanged
  u8 ntiles = 0;        // tiles taken from the rack
  u8 tiles[N];          // PLACE: 0 = existing tile played through, else letter (|BLANK_BIT)
                        // EXCHANGE: rack codes (0 = blank)
  i16 score = 0;
  float equity = 0;

  Move() { std::memset(tiles, 0, sizeof tiles); }
  bool is_place() const { return type == MT_PLACE; }
  bool is_pass() const { return type == MT_PASS; }
  bool is_exchange() const { return type == MT_EXCHANGE; }
  int square(int i) const { return dir == 0 ? row * N + col + i : (row + i) * N + col; }

  // Tiles this move removes from the rack.
  Rack used() const {
    Rack r;
    if (type == MT_PLACE) {
      for (int i = 0; i < len; ++i)
        if (tiles[i]) r.add(tile_rack_code(tiles[i]));
    } else if (type == MT_EXCHANGE) {
      for (int i = 0; i < len; ++i) r.add(tiles[i]);
    }
    return r;
  }
  int played_face() const {
    int s = 0;
    if (type == MT_PLACE)
      for (int i = 0; i < len; ++i)
        if (tiles[i]) s += tile_face(tiles[i]);
    return s;
  }
  u32 hash() const {
    u64 h = mix64(((u64)type << 32) | ((u64)row << 24) | ((u64)col << 16) | ((u64)dir << 8) | len);
    for (int i = 0; i < len; ++i) h = mix64(h ^ tiles[i]);
    return (u32)(h ^ (h >> 32));
  }
  bool same_as(const Move& o) const {
    if (type != o.type) return false;
    if (type == MT_PASS) return true;
    if (type == MT_EXCHANGE) return used() == o.used();
    return row == o.row && col == o.col && dir == o.dir && len == o.len && std::memcmp(tiles, o.tiles, len) == 0;
  }
};

// =====================================================================================
// §5  Board with cross-checks
// =====================================================================================
//
//  xchk[d][s] : for an empty square s, the letters that may be placed there by a play in
//               direction d (d = 0 across, 1 down) without forming an invalid word in the
//               perpendicular direction.
//  xsc[d][s]  : face value of the perpendicular tiles touching s, or -1 when a tile placed
//               at s forms no perpendicular word.

struct Board {
  u8 sq[NSQ];
  u32 xchk[2][NSQ];
  i16 xsc[2][NSQ];
  int count = 0;

  Board() { clear(); }
  void clear() {
    std::memset(sq, 0, sizeof sq);
    for (int d = 0; d < 2; ++d)
      for (int s = 0; s < NSQ; ++s) {
        xchk[d][s] = ALL_LETTERS;
        xsc[d][s] = -1;
      }
    count = 0;
  }
  bool empty() const { return count == 0; }
  u8 at(int r, int c) const { return sq[r * N + c]; }

  bool has_neighbor(int s) const {
    const int r = s / N, c = s % N;
    return (r > 0 && sq[s - N]) || (r < N - 1 && sq[s + N]) || (c > 0 && sq[s - 1]) || (c < N - 1 && sq[s + 1]);
  }

  void compute_xchk(const Lexicon& lex, int s, int d) {
    if (sq[s]) {
      xchk[d][s] = 0;
      xsc[d][s] = -1;
      return;
    }
    const int r = s / N, c = s % N;
    // Perpendicular direction: across plays (d=0) form vertical cross-words.
    const int dr = (d == 0) ? 1 : 0, dc = (d == 0) ? 0 : 1;
    int pr = r - dr, pc = c - dc;
    while (pr >= 0 && pc >= 0 && sq[pr * N + pc]) {
      pr -= dr;
      pc -= dc;
    }
    pr += dr;
    pc += dc;  // first square of the prefix (== (r,c) when there is no prefix)
    int er = r + dr, ec = c + dc;
    while (er < N && ec < N && sq[er * N + ec]) {
      er += dr;
      ec += dc;
    }
    // suffix squares: (r+dr, c+dc) .. (er-dr, ec-dc)
    const bool has_prefix = (pr != r || pc != c);
    const bool has_suffix = (er != r + dr || ec != c + dc);
    if (!has_prefix && !has_suffix) {
      xchk[d][s] = ALL_LETTERS;
      xsc[d][s] = -1;
      return;
    }
    int score = 0;
    u32 list = lex.dawg_root;
    bool ok = true;
    for (int rr = pr, cc = pc; rr != r || cc != c; rr += dr, cc += dc) {
      const u8 t = sq[rr * N + cc];
      score += tile_face(t);
      if (ok) {
        const u32 a = lex.find(list, t & 31);
        if (!a) ok = false;
        else list = Lexicon::child(lex.nodes[a]);
      }
    }
    for (int rr = r + dr, cc = c + dc; rr != er || cc != ec; rr += dr, cc += dc) score += tile_face(sq[rr * N + cc]);
    u32 mask = 0;
    if (ok && list) {
      for (u32 i = list;; ++i) {
        const u32 v = lex.nodes[i];
        const int L = Lexicon::label(v);
        if (!has_suffix) {
          if (Lexicon::accepts(v)) mask |= 1u << L;
        } else {
          u32 l2 = Lexicon::child(v), a = 0;
          bool good = true;
          for (int rr = r + dr, cc = c + dc; rr != er || cc != ec; rr += dr, cc += dc) {
            a = lex.find(l2, sq[rr * N + cc] & 31);
            if (!a) {
              good = false;
              break;
            }
            l2 = Lexicon::child(lex.nodes[a]);
          }
          if (good && Lexicon::accepts(lex.nodes[a])) mask |= 1u << L;
        }
        if (Lexicon::is_end(v)) break;
      }
    }
    xchk[d][s] = mask & ALL_LETTERS;
    xsc[d][s] = (i16)score;
  }

  void recompute_all(const Lexicon& lex) {
    count = 0;
    for (int s = 0; s < NSQ; ++s) {
      if (sq[s]) ++count;
      compute_xchk(lex, s, 0);
      compute_xchk(lex, s, 1);
    }
  }

  // Places a PLACE move and incrementally refreshes the affected cross-checks.
  void place(const Lexicon& lex, const Move& m) {
    if (m.type != MT_PLACE) return;
    const int d = m.dir;
    for (int i = 0; i < m.len; ++i)
      if (m.tiles[i]) {
        const int s = m.square(i);
        sq[s] = m.tiles[i];
        ++count;
        xchk[0][s] = xchk[1][s] = 0;
        xsc[0][s] = xsc[1][s] = -1;
      }
    // Squares just before / after the main word: their cross-word (along d) changed.
    const int first = m.square(0), last = m.square(m.len - 1);
    const int fr = first / N, fc = first % N, lr = last / N, lc = last % N;
    if (d == 0) {
      if (fc > 0) compute_xchk(lex, first - 1, 1);
      if (lc < N - 1) compute_xchk(lex, last + 1, 1);
    } else {
      if (fr > 0) compute_xchk(lex, first - N, 0);
      if (lr < N - 1) compute_xchk(lex, last + N, 0);
    }
    // For every new tile: the ends of its perpendicular run.
    for (int i = 0; i < m.len; ++i) {
      if (!m.tiles[i]) continue;
      const int s = m.square(i);
      const int r = s / N, c = s % N;
      if (d == 0) {  // perpendicular = vertical
        int rr = r;
        while (rr > 0 && sq[(rr - 1) * N + c]) --rr;
        if (rr > 0) compute_xchk(lex, (rr - 1) * N + c, 0);
        rr = r;
        while (rr < N - 1 && sq[(rr + 1) * N + c]) ++rr;
        if (rr < N - 1) compute_xchk(lex, (rr + 1) * N + c, 0);
      } else {  // perpendicular = horizontal
        int cc = c;
        while (cc > 0 && sq[r * N + cc - 1]) --cc;
        if (cc > 0) compute_xchk(lex, r * N + cc - 1, 1);
        cc = c;
        while (cc < N - 1 && sq[r * N + cc + 1]) ++cc;
        if (cc < N - 1) compute_xchk(lex, r * N + cc + 1, 1);
      }
    }
  }

  Rack tiles_on_board() const {
    Rack r;
    for (int s = 0; s < NSQ; ++s)
      if (sq[s]) r.add(tile_rack_code(sq[s]));
    return r;
  }

  std::string ascii(bool color = false) const {
    std::ostringstream o;
    o << "     A B C D E F G H I J K L M N O\n";
    o << "   +-------------------------------+\n";
    for (int r = 0; r < N; ++r) {
      o << std::setw(2) << (r + 1) << " | ";
      for (int c = 0; c < N; ++c) {
        const u8 t = sq[r * N + c];
        if (t) {
          if (color) o << (tile_blank(t) ? "\x1b[1;35m" : "\x1b[1m") << tile_char(t) << "\x1b[0m";
          else o << tile_char(t);
        } else {
          const char p = LAYOUT[r][c];
          char shown = '.';
          if (p == '=') shown = '=';
          else if (p == '-') shown = '-';
          else if (p == '"') shown = '"';
          else if (p == '\'') shown = '\'';
          if (r * N + c == CENTER) shown = '*';
          if (color) o << "\x1b[2m" << shown << "\x1b[0m";
          else o << shown;
        }
        o << (c < N - 1 ? " " : "");
      }
      o << " | " << (r + 1) << "\n";
    }
    o << "   +-------------------------------+\n";
    o << "     A B C D E F G H I J K L M N O\n";
    return o.str();
  }
};

// =====================================================================================
// §6  Notation: printing, parsing, independent scoring and validation
// =====================================================================================
//
//  Standard notation: "8D WORD" = row 8, column D, across.  "D8 WORD" = column D, row 8,
//  down.  Lower-case letters are blanks.  Tiles already on the board are written in
//  parentheses "WO(R)D" or as dots "WO.D".  Exchanges: "exch ABC" or "-ABC".  Pass: "pass".

inline std::string move_coord(const Move& m) {
  if (m.dir == 0) return std::to_string(m.row + 1) + char('A' + m.col);
  return std::string(1, char('A' + m.col)) + std::to_string(m.row + 1);
}

inline std::string move_word(const Board& b, const Move& m) {
  std::string s;
  bool open = false;
  for (int i = 0; i < m.len; ++i) {
    if (m.tiles[i]) {
      if (open) {
        s += ')';
        open = false;
      }
      s += tile_char(m.tiles[i]);
    } else {
      if (!open) {
        s += '(';
        open = true;
      }
      s += tile_char(b.sq[m.square(i)]);
    }
  }
  if (open) s += ')';
  return s;
}

// GCG-style word: played-through tiles as '.'.
inline std::string move_word_dots(const Move& m) {
  std::string s;
  for (int i = 0; i < m.len; ++i) s += m.tiles[i] ? tile_char(m.tiles[i]) : '.';
  return s;
}

inline std::string move_str(const Board& b, const Move& m) {
  if (m.type == MT_PASS) return "pass";
  if (m.type == MT_EXCHANGE) return "exch " + m.used().str();
  return move_coord(m) + " " + move_word(b, m);
}

inline bool parse_coord(const std::string& tok, int& row, int& col, int& dir) {
  if (tok.empty()) return false;
  std::string t = to_upper(tok);
  if (std::isdigit((unsigned char)t[0])) {
    size_t i = 0;
    int r = 0;
    while (i < t.size() && std::isdigit((unsigned char)t[i])) r = r * 10 + (t[i++] - '0');
    if (i + 1 != t.size() || t[i] < 'A' || t[i] > 'O') return false;
    row = r - 1;
    col = t[i] - 'A';
    dir = 0;
  } else {
    if (t[0] < 'A' || t[0] > 'O' || t.size() < 2) return false;
    int r = 0;
    for (size_t i = 1; i < t.size(); ++i) {
      if (!std::isdigit((unsigned char)t[i])) return false;
      r = r * 10 + (t[i] - '0');
    }
    col = t[0] - 'A';
    row = r - 1;
    dir = 1;
  }
  return row >= 0 && row < N && col >= 0 && col < N;
}

inline bool parse_move(const Board& b, const std::string& text, Move& m, std::string& err) {
  m = Move();
  std::vector<std::string> tok = split_ws(text);
  if (tok.empty()) {
    err = "empty move";
    return false;
  }
  std::string t0 = to_lower(tok[0]);
  if (t0 == "pass" || (t0 == "-" && tok.size() == 1)) {
    m.type = MT_PASS;
    return true;
  }
  std::string exch;
  if (t0 == "exch" || t0 == "exchange" || t0 == "x" || t0 == "swap" || t0 == "-") {
    if (tok.size() < 2) {
      err = "exchange needs tiles, e.g. exch QVU";
      return false;
    }
    exch = tok[1];
  } else if (t0.size() > 1 && t0[0] == '-') {
    exch = tok[0].substr(1);
  }
  if (!exch.empty()) {
    if (exch.size() > (size_t)RACK_SIZE) {
      err = "too many tiles to exchange";
      return false;
    }
    m.type = MT_EXCHANGE;
    m.len = 0;
    for (char ch : exch) {
      const int L = char_to_rack(ch);
      if (L < 0) {
        err = std::string("bad tile '") + ch + "'";
        return false;
      }
      m.tiles[m.len++] = (u8)L;
    }
    m.ntiles = m.len;
    return true;
  }
  if (tok.size() < 2) {
    err = "expected e.g. '8D WORD', 'D8 WORD', 'exch ABC' or 'pass'";
    return false;
  }
  int row, col, dir;
  if (!parse_coord(tok[0], row, col, dir)) {
    err = "bad coordinate '" + tok[0] + "' (use 8D for across, D8 for down)";
    return false;
  }
  const std::string& w = tok[1];
  std::vector<std::pair<char, bool>> letters;  // (char, forced-through)
  bool paren = false;
  for (char ch : w) {
    if (ch == '(') {
      paren = true;
      continue;
    }
    if (ch == ')') {
      paren = false;
      continue;
    }
    letters.push_back({ch, paren});
  }
  if (letters.empty() || (int)letters.size() > N) {
    err = "bad word";
    return false;
  }
  m.type = MT_PLACE;
  m.row = (u8)row;
  m.col = (u8)col;
  m.dir = (u8)dir;
  m.len = (u8)letters.size();
  const int end_r = dir == 0 ? row : row + m.len - 1;
  const int end_c = dir == 0 ? col + m.len - 1 : col;
  if (end_r >= N || end_c >= N) {
    err = "word runs off the board";
    return false;
  }
  for (int i = 0; i < m.len; ++i) {
    const int s = m.square(i);
    const char ch = letters[i].first;
    const u8 on = b.sq[s];
    if (ch == '.' || letters[i].second) {
      if (!on) {
        err = "square " + square_name(s / N, s % N) + " is empty but marked as played-through";
        return false;
      }
      if (ch != '.' && std::toupper((unsigned char)ch) != std::toupper((unsigned char)tile_char(on))) {
        err = "letter mismatch at " + square_name(s / N, s % N);
        return false;
      }
      m.tiles[i] = 0;
      continue;
    }
    int L = char_to_rack(ch);
    if (L <= 0) {
      err = std::string("bad letter '") + ch + "' (use lower case for blanks)";
      return false;
    }
    if (on) {
      if (tile_letter(on) != L) {
        err = "square " + square_name(s / N, s % N) + " already holds " + std::string(1, tile_char(on));
        return false;
      }
      m.tiles[i] = 0;
      continue;
    }
    m.tiles[i] = (u8)(std::islower((unsigned char)ch) ? (L | BLANK_BIT) : L);
    m.ntiles++;
  }
  if (!m.ntiles) {
    err = "the move places no tiles";
    return false;
  }
  const int first = m.square(0), last = m.square(m.len - 1);
  const int before = dir == 0 ? (col > 0 ? first - 1 : -1) : (row > 0 ? first - N : -1);
  const int after = dir == 0 ? (end_c < N - 1 ? last + 1 : -1) : (end_r < N - 1 ? last + N : -1);
  if ((before >= 0 && b.sq[before]) || (after >= 0 && b.sq[after])) {
    err = "the word must include the tiles touching both of its ends";
    return false;
  }
  return true;
}

// Scores a PLACE move from scratch (independent of the move generator).
inline int score_move(const Board& b, const Move& m) {
  if (m.type != MT_PLACE) return 0;
  int main_sum = 0, wmul = 1, cross = 0;
  for (int i = 0; i < m.len; ++i) {
    const int s = m.square(i);
    if (!m.tiles[i]) {
      main_sum += tile_face(b.sq[s]);
      continue;
    }
    const int ls = tile_face(m.tiles[i]) * PREM.lm[s];
    main_sum += ls;
    wmul *= PREM.wm[s];
    const int r = s / N, c = s % N;
    const int dr = m.dir == 0 ? 1 : 0, dc = m.dir == 0 ? 0 : 1;
    int sum = 0;
    bool any = false;
    for (int rr = r - dr, cc = c - dc; rr >= 0 && cc >= 0 && b.sq[rr * N + cc]; rr -= dr, cc -= dc) {
      sum += tile_face(b.sq[rr * N + cc]);
      any = true;
    }
    for (int rr = r + dr, cc = c + dc; rr < N && cc < N && b.sq[rr * N + cc]; rr += dr, cc += dc) {
      sum += tile_face(b.sq[rr * N + cc]);
      any = true;
    }
    if (any) cross += (sum + ls) * PREM.wm[s];
  }
  return main_sum * wmul + cross + (m.ntiles == RACK_SIZE ? BINGO_BONUS : 0);
}

// Full legality check of a PLACE move against the lexicon (independent of cross-checks).
inline bool validate_move(const Lexicon& lex, const Board& b, const Move& m, std::string& err,
                          std::vector<std::string>* words = nullptr) {
  if (m.type != MT_PLACE) return true;
  if (m.ntiles < 1 || m.len < 1) {
    err = "no tiles placed";
    return false;
  }
  bool touches = false, covers_center = false;
  for (int i = 0; i < m.len; ++i) {
    const int s = m.square(i);
    if (m.tiles[i]) {
      if (b.sq[s]) {
        err = "square occupied";
        return false;
      }
      if (s == CENTER) covers_center = true;
      if (b.has_neighbor(s)) touches = true;
    } else if (!b.sq[s]) {
      err = "played-through square is empty";
      return false;
    }
  }
  if (b.empty() ? !covers_center : !touches) {
    err = b.empty() ? "the first play must cover the centre square" : "the play is not connected";
    return false;
  }
  // Main word = maximal run through the placed tiles.
  Board nb = b;
  for (int i = 0; i < m.len; ++i)
    if (m.tiles[i]) nb.sq[m.square(i)] = m.tiles[i];
  auto run_word = [&](int s, int d, std::string& out) {
    const int step = d == 0 ? 1 : N;
    int a = s;
    while (true) {
      const int r = a / N, c = a % N;
      const bool can = d == 0 ? c > 0 : r > 0;
      if (can && nb.sq[a - step]) a -= step;
      else break;
    }
    out.clear();
    int z = a;
    while (true) {
      out += tile_char(nb.sq[z]);
      const int r = z / N, c = z % N;
      const bool can = d == 0 ? c < N - 1 : r < N - 1;
      if (can && nb.sq[z + step]) z += step;
      else break;
    }
  };
  std::string w;
  int placed_idx = 0;
  while (!m.tiles[placed_idx]) ++placed_idx;
  run_word(m.square(placed_idx), m.dir, w);
  std::vector<std::string> formed;
  if (w.size() >= 2) formed.push_back(w);
  for (int i = 0; i < m.len; ++i) {
    if (!m.tiles[i]) continue;
    run_word(m.square(i), 1 - m.dir, w);
    if (w.size() >= 2) formed.push_back(w);
  }
  if (formed.empty()) {
    err = "a single tile must form a word";
    return false;
  }
  if (words) *words = formed;
  for (const auto& fw : formed)
    if (!lex.is_word(to_upper(fw))) {
      err = "not in the lexicon: " + to_upper(fw);
      return false;
    }
  return true;
}

}  // namespace tf
namespace tf {

// =====================================================================================
// §7  Leave values ("what the tiles you keep are worth")
// =====================================================================================
//
//  The single most important piece of static knowledge.  A leave is the multiset of
//  1..6 tiles kept after a play.  Its value is how many points (on average, over the
//  rest of the game) holding those tiles is worth compared with an average draw.
//  Face values are a poor guide: ? is worth ~25 points, S ~8, while Q, V, UU, IIII...
//  cost you points.  Values are stored for every feasible leave (~900k of them) in an
//  open-addressing hash table.  Key = the leave's tiles sorted, 5 bits each (blank =
//  27), so a leave of <= 6 tiles fits in 30 bits.

class LeaveTable {
 public:
  LeaveTable() { init_default(); }

  static u32 key_of(const int8_t* cnt) {
    u32 key = 0;
    int sh = 0;
    for (int L = 1; L < NLET; ++L)
      for (int k = 0; k < cnt[L]; ++k) {
        key |= (u32)L << sh;
        sh += 5;
      }
    for (int k = 0; k < cnt[BLANK]; ++k) {
      key |= 27u << sh;
      sh += 5;
    }
    return key;
  }
  static void counts_of(u32 key, int8_t* cnt) {
    std::memset(cnt, 0, NLET);
    while (key) {
      const int code = key & 31;
      cnt[code == 27 ? BLANK : code]++;
      key >>= 5;
    }
  }
  static std::string key_str(u32 key) {
    int8_t cnt[NLET];
    counts_of(key, cnt);
    std::string s;
    for (int L = 1; L < NLET; ++L)
      for (int k = 0; k < cnt[L]; ++k) s += rack_char(L);
    for (int k = 0; k < cnt[BLANK]; ++k) s += '?';
    return s;
  }

  float get(u32 key) const {
    if (!key) return 0.f;
    size_t i = slot(key);
    while (true) {
      const u32 k = keys_[i];
      if (k == key) return vals_[i];
      if (!k) return 0.f;
      i = (i + 1) & mask_;
    }
  }
  bool has(u32 key) const {
    if (!key) return false;
    size_t i = slot(key);
    while (true) {
      const u32 k = keys_[i];
      if (k == key) return true;
      if (!k) return false;
      i = (i + 1) & mask_;
    }
  }
  float value(const Rack& r) const {
    if (r.n <= 0 || r.n > 6) return 0.f;
    return get(key_of(r.c));
  }
  void set(u32 key, float v) {
    if (!key) return;
    if ((count_ + 1) * 2 > keys_.size()) grow();
    size_t i = slot(key);
    while (keys_[i] && keys_[i] != key) i = (i + 1) & mask_;
    if (!keys_[i]) {
      keys_[i] = key;
      ++count_;
    }
    vals_[i] = v;
  }
  size_t size() const { return count_; }

  template <class F>
  void for_each(F f) const {
    for (size_t i = 0; i < keys_.size(); ++i)
      if (keys_[i]) f(keys_[i], vals_[i]);
  }

  // Calls f(counts, size) for every feasible leave of 1..maxsize tiles.
  template <class F>
  static void enumerate(int maxsize, F f) {
    int8_t cnt[NLET] = {0};
    std::function<void(int, int)> rec = [&](int L, int size) {
      if (L == NLET) {
        if (size > 0) f(cnt, size);
        return;
      }
      for (int k = 0; k <= TILE_COUNT[L] && size + k <= maxsize; ++k) {
        cnt[L] = (int8_t)k;
        rec(L + 1, size + k);
      }
      cnt[L] = 0;
    };
    rec(0, 0);
  }

  // Built-in model used until a trained table is loaded (see `train`).
  static float model_value(const int8_t* c);

  void init_default() {
    keys_.assign(1u << 21, 0u);
    vals_.assign(1u << 21, 0.f);
    mask_ = (u32)keys_.size() - 1;
    count_ = 0;
    enumerate(6, [&](const int8_t* cnt, int) { set(key_of(cnt), model_value(cnt)); });
  }

  static bool parse_leave(const std::string& s, int8_t* cnt, int& n) {
    std::memset(cnt, 0, NLET);
    n = 0;
    for (char ch : s) {
      const int L = char_to_rack(ch);
      if (L < 0) return false;
      cnt[L]++;
      n++;
    }
    for (int L = 0; L < NLET; ++L)
      if (cnt[L] > TILE_COUNT[L]) return false;
    return n >= 1 && n <= 6;
  }

  // Binary KLV / KLV2 leave files (the format used by wolges and Macondo): a u32 node
  // count, that many u32 DAWG nodes (same bit layout as our lexicon graph: label in the
  // top 8 bits, accepts = bit 23, end-of-list = bit 22, 22-bit child index; node 0
  // points at the root), a u32 value count, then the values: f32 (KLV2) or i16/256
  // (KLV).  Leaves are the DAWG's words in order, tiles sorted with blank = 0 first.
  // The file is only accepted if every check passes.
  bool load_klv(const std::string& path, std::string& err, size_t* loaded = nullptr) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
      err = "cannot open " + path;
      return false;
    }
    std::vector<unsigned char> buf((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto rd32 = [&](size_t off) -> u32 {
      return (u32)buf[off] | ((u32)buf[off + 1] << 8) | ((u32)buf[off + 2] << 16) | ((u32)buf[off + 3] << 24);
    };
    if (buf.size() < 8) {
      err = "file too small for KLV";
      return false;
    }
    const u32 nn = rd32(0);
    if (nn < 2 || nn > (1u << 22) || 4 + (size_t)nn * 4 + 4 > buf.size()) {
      err = "not a KLV file (bad node count)";
      return false;
    }
    std::vector<u32> nodes(nn);
    for (u32 i = 0; i < nn; ++i) nodes[i] = rd32(4 + (size_t)i * 4);
    size_t off = 4 + (size_t)nn * 4;
    const u32 nv = rd32(off);
    off += 4;
    const size_t rest = buf.size() - off;
    bool f32 = false;
    if (rest == (size_t)nv * 4) f32 = true;
    else if (rest != (size_t)nv * 2) {
      err = "not a KLV/KLV2 file (value block has the wrong size)";
      return false;
    }
    // Enumerate the DAWG's words in order.
    std::vector<std::string> words;
    std::string cur;
    bool bad = false;
    std::function<void(u32, int)> walk = [&](u32 list, int depth) {
      if (!list || bad) return;
      if (depth > 7) {
        bad = true;
        return;
      }
      for (u32 i = list;; ++i) {
        if (i >= nn) {
          bad = true;
          return;
        }
        const u32 v = nodes[i];
        cur.push_back((char)(v >> 24));
        if (v & (1u << 23)) words.push_back(cur);
        walk(v & 0x3FFFFFu, depth + 1);
        cur.pop_back();
        if (v & (1u << 22)) break;
      }
    };
    walk(nodes[0] & 0x3FFFFFu, 0);
    if (bad || words.size() != nv) {
      err = fmt("not a KLV file (%zu leaves in the graph, %u values)", words.size(), nv);
      return false;
    }
    std::vector<std::pair<u32, float>> parsed;
    parsed.reserve(nv);
    for (size_t i = 0; i < words.size(); ++i) {
      int8_t cnt[NLET] = {0};
      int n = 0;
      for (char ch : words[i]) {
        const int L = (u8)ch;
        if (L >= NLET) {
          err = "KLV contains tiles outside A-Z/blank (other alphabet?)";
          return false;
        }
        cnt[L]++;
        ++n;
      }
      float v;
      if (f32) {
        const u32 raw = rd32(off + i * 4);
        std::memcpy(&v, &raw, 4);
      } else {
        const int16_t raw = (int16_t)((u16)buf[off + i * 2] | ((u16)buf[off + i * 2 + 1] << 8));
        v = (float)raw / 256.0f;
      }
      if (!std::isfinite(v) || std::fabs(v) > 200.f) {
        err = "KLV values out of range";
        return false;
      }
      if (n < 1 || n > 6) continue;
      bool feasible = true;
      for (int L = 0; L < NLET; ++L) feasible &= cnt[L] <= TILE_COUNT[L];
      if (feasible) parsed.push_back({key_of(cnt), v});
    }
    for (const auto& kv : parsed) set(kv.first, kv.second);
    if (loaded) *loaded = parsed.size();
    return true;
  }

  // Text format: one leave per line, "LEAVE value" or "LEAVE,value" (letter order free).
  bool load(const std::string& path, std::string& err, size_t* loaded = nullptr) {
    {
      const std::string low = to_lower(path);
      if (low.size() > 4 && (low.compare(low.size() - 4, 4, ".klv") == 0 || low.compare(low.size() - 5, 5, ".klv2") == 0))
        return load_klv(path, err, loaded);
    }
    std::ifstream in(path);
    if (!in) {
      err = "cannot open " + path;
      return false;
    }
    std::string line;
    size_t n_ok = 0;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty() || line[0] == '#') continue;
      for (auto& ch : line)
        if (ch == ',' || ch == '\t' || ch == ';') ch = ' ';
      std::istringstream is(line);
      std::string leave;
      double v;
      if (!(is >> leave >> v)) continue;
      int8_t cnt[NLET];
      int n;
      if (!parse_leave(leave, cnt, n)) continue;
      set(key_of(cnt), (float)v);
      ++n_ok;
    }
    if (loaded) *loaded = n_ok;
    if (!n_ok) {
      err = "no leave values found in " + path;
      return false;
    }
    return true;
  }
  bool save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    std::vector<std::pair<std::string, float>> rows;
    for_each([&](u32 k, float v) { rows.push_back({key_str(k), v}); });
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
      return a.first.size() != b.first.size() ? a.first.size() < b.first.size() : a.first < b.first;
    });
    out << "# Tilefish leave values: LEAVE VALUE (points)\n";
    for (const auto& r : rows) out << r.first << ' ' << std::fixed << std::setprecision(3) << r.second << '\n';
    return (bool)out;
  }

 private:
  std::vector<u32> keys_;
  std::vector<float> vals_;
  u32 mask_ = 0;
  size_t count_ = 0;

  size_t slot(u32 key) const { return (size_t)((key * 2654435761u) ^ (key >> 15)) & mask_; }
  void grow() {
    std::vector<u32> ok = std::move(keys_);
    std::vector<float> ov = std::move(vals_);
    keys_.assign(ok.size() * 2, 0u);
    vals_.assign(ok.size() * 2, 0.f);
    mask_ = (u32)keys_.size() - 1;
    count_ = 0;
    for (size_t i = 0; i < ok.size(); ++i)
      if (ok[i]) set(ok[i], ov[i]);
  }
};

// A compact parametric leave model: per-tile values, duplicate penalties,
// vowel/consonant balance and a few synergies.  It is only the starting point; `train`
// replaces it with values learned from self-play for the loaded lexicon.
inline float LeaveTable::model_value(const int8_t* c) {
  //                               ?     A     B     C     D     E     F     G     H     I     J     K     L     M
  static const float single[NLET] = {25.0f, 1.0f, -2.0f, 0.5f, 0.5f, 4.0f, -2.0f, -2.5f, 1.0f, -0.5f, -2.5f, -1.5f, 0.0f, 0.3f,
                                     //  N     O     P     Q     R     S     T     U     V     W     X     Y     Z
                                     0.5f, -1.5f, -0.5f, -7.0f, 1.5f, 8.0f, 0.5f, -3.5f, -5.5f, -3.0f, 3.0f, -0.5f, 2.0f};
  float v = 0.f;
  int vow = 0, con = 0, n = 0;
  for (int L = 0; L < NLET; ++L) {
    const int k = c[L];
    if (!k) continue;
    n += k;
    v += single[L] * (float)k;
    if (L != BLANK) {
      if (is_vowel(L)) vow += k;
      else con += k;
    }
    if (k >= 2 && L != BLANK) {
      float base = is_vowel(L) ? 4.0f : 3.5f;
      if (L == 19) base = 2.5f;                               // SS is not so bad
      if (L == 9 || L == 21 || L == 15) base = 6.0f;          // II, UU, OO are awful
      v -= base * (float)(k - 1) * (float)k * 0.5f;
    }
  }
  // Q wants a U; a U without Q is mildly bad.
  if (c[17]) v += c[21] ? 5.0f : -2.0f;
  // Balance: a good leave has roughly 40% vowels.
  const int diff = vow - con;
  if (diff > 1) v -= 3.0f * (float)((diff - 1) * (diff - 1));
  if (diff < -2) v -= 1.8f * (float)((-diff - 2) * (-diff - 2));
  if (n >= 2 && vow == 0 && c[BLANK] == 0) v -= 1.5f * (float)(n - 1);
  // Bingo-friendly synergies.
  if (c[5] && c[18]) v += 1.5f;               // ER
  if (c[5] && c[19]) v += 1.5f;               // ES
  if (c[9] && c[14] && c[7]) v += 3.0f;       // ING
  if (c[5] && c[4]) v += 0.8f;                // ED
  return v;
}

// =====================================================================================
// §8  Win-probability model
// =====================================================================================
//
//  P(win) for the player to move, given spread S (my score - opponent's) and the number
//  T of tiles I cannot see (bag + opponent's rack).  Logistic in S with separate
//  intercept/slope per T:  P = 1 / (1 + exp(-(a[T] + b[T]*S))).  `train` refits it from
//  self-play for the loaded lexicon.  a[T] > 0 encodes the advantage of being on turn.

struct WinModel {
  float a[MAX_UNSEEN + 1];
  float b[MAX_UNSEEN + 1];
  WinModel() { set_default(); }

  void set_default() {
    for (int t = 0; t <= MAX_UNSEEN; ++t) {
      const float sigma = std::sqrt(10.0f + 50.0f * (float)t);
      b[t] = 1.7f / sigma;
      a[t] = b[t] * (3.0f + 0.08f * (float)t);
    }
  }
  float win(float spread, int unseen) const {
    if (unseen <= 0) return spread > 0 ? 1.f : (spread < 0 ? 0.f : 0.5f);
    if (unseen > MAX_UNSEEN) unseen = MAX_UNSEEN;
    const float z = a[unseen] + b[unseen] * spread;
    return 1.f / (1.f + std::exp(-z));
  }
  bool load(const std::string& path, std::string& err) {
    std::ifstream in(path);
    if (!in) {
      err = "cannot open " + path;
      return false;
    }
    std::string line;
    int n = 0;
    while (std::getline(in, line)) {
      line = trim(line);
      if (line.empty() || line[0] == '#') continue;
      std::istringstream is(line);
      int t;
      float aa, bb;
      if (!(is >> t >> aa >> bb)) continue;
      if (t < 0 || t > MAX_UNSEEN) continue;
      a[t] = aa;
      b[t] = bb;
      ++n;
    }
    if (!n) {
      err = "no rows in " + path;
      return false;
    }
    return true;
  }
  bool save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    out << "# Tilefish win model: UNSEEN a b   (P(win) = 1/(1+exp(-(a + b*spread))))\n";
    for (int t = 0; t <= MAX_UNSEEN; ++t) out << t << ' ' << std::setprecision(6) << a[t] << ' ' << b[t] << '\n';
    return (bool)out;
  }
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §9  Move generation (Gordon's GADDAG algorithm) and static equity
// =====================================================================================
//
//  For every line (row for across plays, column for down plays) and every anchor
//  square (an empty square next to a tile, or the centre on the first move) the
//  generator walks the GADDAG: first leftwards from the anchor, then through the
//  separator rightwards.  Letters are only tried where the cross-check allows them,
//  so every generated play is legal by construction.  A play is generated from the
//  leftmost anchor it covers (leftward growth stops at the previous anchor), so each
//  play is produced exactly once; single-tile plays are produced by the across pass
//  when they form an across word.
//
//  Static equity  = score + leave value + end-of-game adjustments.
//  The leave value is looked up once per distinct subset of the rack (at most 128
//  per call): the rack tiles get bit positions, and removing a letter always clears
//  the highest remaining bit of that letter, so every leave has one canonical mask.

enum GenMode { GEN_ALL = 0, GEN_BEST = 1 };

struct EvalCtx {
  int bag = 0;                 // tiles in the bag before the move
  int opp_face = 0;            // face value of the opponent's rack (used when bag == 0)
  bool allow_exchange = true;  // exchanges also need bag >= 7
  bool use_leaves = true;
  bool add_pass = true;
};

// Tunable static-evaluation constants.
struct StaticParams {
  float not_out_const = 10.f;  // endgame: penalty for a play that does not go out ...
  float not_out_mult = 2.f;    // ... plus this many times the face value kept
  float pass_penalty = 20.f;   // passing while tiles remain in the bag
};
inline StaticParams STATIC_PARAMS;

class MoveGen {
 public:
  explicit MoveGen(const Lexicon* lex = nullptr, const LeaveTable* leaves = nullptr) : lex_(lex), leaves_(leaves) {}
  void set(const Lexicon* lex, const LeaveTable* leaves) {
    lex_ = lex;
    leaves_ = leaves;
    rcache_.clear();  // the per-rack tables depend on both
    wmask_.clear();
  }
  const LeaveTable* leaves() const { return leaves_; }
  const Lexicon* lexicon() const { return lex_; }

  // Appends every legal move (plays, then exchanges if allowed, then pass) to `out`.
  void generate_all(const Board& b, const Rack& rack, const EvalCtx& ctx, std::vector<Move>& out) {
    setup(b, rack, ctx);
    mode_ = GEN_ALL;
    out_ = &out;
    run();
  }
  // Like generate_all, but only plays that cover at least one of the squares in
  // `anchors` (each play once, from its leftmost such square); no exchanges or pass.
  void generate_near(const Board& b, const Rack& rack, const EvalCtx& ctx, const bool* anchors, std::vector<Move>& out) {
    setup(b, rack, ctx);
    mode_ = GEN_ALL;
    out_ = &out;
    near_ = anchors;
    EvalCtx c = ctx;
    c.allow_exchange = false;
    c.add_pass = false;
    ctx_ = c;
    run();
    near_ = nullptr;
  }
  // Returns the move with the highest static equity.
  Move generate_best(const Board& b, const Rack& rack, const EvalCtx& ctx) {
    setup(b, rack, ctx);
    mode_ = GEN_BEST;
    out_ = nullptr;
    run();
    return best_;
  }
  float leave_value(const Rack& leave) const { return leaves_ ? leaves_->value(leave) : 0.f; }

  // Static equity of an arbitrary move (e.g. a human's), consistent with the generator.
  float equity_of(const Move& m, const Rack& rack, const EvalCtx& ctx) const {
    Rack leave = rack;
    if (m.type != MT_PASS) leave.sub_all(m.used());
    if (m.type == MT_PASS) {
      if (ctx.bag > 0) return (leave.n <= 6 && ctx.use_leaves ? leave_value(leave) : 0.f) - STATIC_PARAMS.pass_penalty;
      return -(STATIC_PARAMS.not_out_mult * (float)leave.face() + STATIC_PARAMS.not_out_const);
    }
    if (m.type == MT_EXCHANGE) return ctx.use_leaves ? leave_value(leave) : 0.f;
    if (ctx.bag > 0) return (float)m.score + (ctx.use_leaves ? leave_value(leave) : 0.f);
    if (leave.n == 0) return (float)(m.score + 2 * ctx.opp_face);
    return (float)m.score - STATIC_PARAMS.not_out_mult * (float)leave.face() - STATIC_PARAMS.not_out_const;
  }

 private:
  const Lexicon* lex_;
  const LeaveTable* leaves_;
  const Board* B_ = nullptr;
  EvalCtx ctx_;
  GenMode mode_ = GEN_ALL;
  std::vector<Move>* out_ = nullptr;
  Move best_;
  float best_eq_ = -1e30f;

  // rack state
  int nr_ = 0;
  u8 rt_[RACK_SIZE];
  int rstart_[NLET];
  int rk_[NLET];
  int rack_left_ = 0;
  u32 rack_mask_ = 0;  // letters (1..26) currently on the rack
  int tiles_played_ = 0;
  u32 mask_ = 0, full_mask_ = 0;
  float lv_[128];
  int lf_[128];
  u8 lv_done_[128];

  // current line
  int dir_ = 0, line_ = 0;
  u8 lt_[N];
  u32 lx_[N];
  i16 lxs_[N];
  u8 llm_[N], lwm_[N];
  int anchor_ = 0, last_anchor_ = -1;
  bool no_right_ = true;
  u8 strip_[N];
  bool is_anchor_[NSQ];

  // shadow (upper-bound) pruning for GEN_BEST
  struct AnchorInfo {
    float bound;
    u8 dir, line, col;
    int8_t last;
    bool refined;
    bool operator<(const AnchorInfo& o) const { return bound < o.bound; }
  };
  AnchorInfo anchors_[2 * NSQ];
  // Leave and score are coupled: for each number k of tiles played, the Pareto
  // frontier of (scores of the tiles played, descending; rest of the equity).  A rack
  // has at most C(7,3) = 35 distinct subsets of any one size.
  struct ShadowSubset {
    float rest;
    u8 sc[RACK_SIZE];
    u8 blanks;  // blanks among the tiles played
    bool ok0;   // do the tiles played (alone) spell a word?
    u64 key;    // multiset key of the non-blank tiles played
  };
  static constexpr int MAX_SUBSETS = 36;
  ShadowSubset front_[RACK_SIZE + 1][MAX_SUBSETS];
  ShadowSubset frontw_[RACK_SIZE + 1][MAX_SUBSETS];  // frontier of the subsets that spell a word on their own
  int nfrontw_[RACK_SIZE + 1];
  ShadowSubset allsub_[RACK_SIZE + 1][MAX_SUBSETS];
  int nallsub_[RACK_SIZE + 1];
  int nfront_[RACK_SIZE + 1];
  float rest1_[NLET];  // rest of the equity after playing just this one tile (-inf if not on the rack)
  u32 rack_letters_ = 0;
  u32 cap_mask_[RACK_SIZE];  // rack letters grouped by tile score, highest score first
  int cap_val_[RACK_SIZE];
  int ncap_ = 0;
  bool has_blank_ = false;
  bool use_shadow_ = true;
  bool use_refine_ = true;
  bool use_wmp_ = true;
  const bool* near_ = nullptr;  // generate_near: the allowed anchor squares
  bool span_prune_ = false;
  float span_lmax_[N];
  float span_rmax_[N][N];
  bool bingo7_ = true;      // can the rack form a 7-letter word?
  u32 bingo8_ = ALL_LETTERS;  // board letters that complete an 8-letter word
  // Word-map search: for k tiles played through tiles of multiset key kt, the subsets
  // (bits = indices into allsub_[k]) that spell at least one word with them.  Memoized
  // per rack: entries carry the stamp of the rack tables they were computed for.
  struct WordMask {
    u64 kt = 0, mask = 0;
    u32 stamp = 0;
    u8 k = 0, through = 0;
  };
  std::vector<WordMask> wmask_;  // allocated on first use
  u32 wstamp_ = 0;               // stamp of the current rack's tables
  // Everything the best-play search derives from the rack (and the context the equity
  // uses), kept for the last few racks: in a simulation every candidate hands the
  // opponent the same rack, and candidates with the same leave draw the same tiles.
  struct RackTables {
    int8_t cnt[NLET];
    int bagc = -1, opp_face = 0;
    bool use_leaves = false;
    u32 stamp = 0, used = 0;  // stamp 0: empty; used: LRU clock
    bool exch_known = false;  // best exchange (GEN_BEST), computed when first allowed
    float exch_eq = -1e30f;
    Move exch;
    int nallsub[RACK_SIZE + 1], nfront[RACK_SIZE + 1], nfrontw[RACK_SIZE + 1];
    ShadowSubset allsub[RACK_SIZE + 1][MAX_SUBSETS], front[RACK_SIZE + 1][MAX_SUBSETS], frontw[RACK_SIZE + 1][MAX_SUBSETS];
    float rest1[NLET];
    u32 rack_letters, cap_mask[RACK_SIZE], bingo8;
    int cap_val[RACK_SIZE], ncap;
    bool has_blank, bingo7;
  };
  std::vector<RackTables> rcache_;  // allocated on first use
  u32 rclock_ = 0, rstamp_ = 0;

 public:
  void set_shadow(bool on) { use_shadow_ = on; }
  void set_refine(bool on) { use_refine_ = on; }
  void set_wmp(bool on) { use_wmp_ = on; }
  long anchors_searched = 0, anchors_total = 0;  // with word maps: spans

 private:

  void setup(const Board& b, const Rack& rack, const EvalCtx& ctx) {
    B_ = &b;
    ctx_ = ctx;
    nr_ = 0;
    for (int L = 0; L < NLET; ++L) {
      rstart_[L] = nr_;
      rk_[L] = rack.c[L];
      for (int k = 0; k < rack.c[L] && nr_ < RACK_SIZE; ++k) rt_[nr_++] = (u8)L;
    }
    rack_left_ = nr_;
    rack_mask_ = 0;
    for (int L = 1; L < NLET; ++L)
      if (rk_[L]) rack_mask_ |= 1u << L;
    tiles_played_ = 0;
    full_mask_ = mask_ = (nr_ >= 32) ? 0xFFFFFFFFu : ((1u << nr_) - 1);
    std::memset(lv_done_, 0, sizeof lv_done_);
    best_eq_ = -1e30f;
    best_ = Move();
  }

  inline float leave_val(u32 m) {
    if (!lv_done_[m]) {
      int8_t cnt[NLET] = {0};
      int size = 0, face = 0;
      for (int i = 0; i < nr_; ++i)
        if ((m >> i) & 1u) {
          cnt[rt_[i]]++;
          ++size;
          face += TILE_SCORE[rt_[i]];
        }
      lv_[m] = (size == 0 || size > 6 || !ctx_.use_leaves || !leaves_) ? 0.f : leaves_->get(LeaveTable::key_of(cnt));
      lf_[m] = face;
      lv_done_[m] = 1;
    }
    return lv_[m];
  }

  inline float equity(int score, u32 m) {
    if (ctx_.bag > 0) return (float)score + leave_val(m);
    if (m == 0) return (float)(score + 2 * ctx_.opp_face);
    leave_val(m);
    return (float)score - STATIC_PARAMS.not_out_mult * (float)lf_[m] - STATIC_PARAMS.not_out_const;
  }

  inline void take(int L) {
    if (--rk_[L] == 0 && L) rack_mask_ &= ~(1u << L);
    --rack_left_;
    ++tiles_played_;
    mask_ &= ~(1u << (rstart_[L] + rk_[L]));
  }
  inline void untake(int L) {
    mask_ |= 1u << (rstart_[L] + rk_[L]);
    if (rk_[L]++ == 0 && L) rack_mask_ |= 1u << L;
    ++rack_left_;
    --tiles_played_;
  }

  void load_line(int d, int line) {
    dir_ = d;
    line_ = line;
    const Board& b = *B_;
    for (int k = 0; k < N; ++k) {
      const int s = d == 0 ? line * N + k : k * N + line;
      lt_[k] = b.sq[s];
      lx_[k] = b.xchk[d][s];
      lxs_[k] = b.xsc[d][s];
      llm_[k] = PREM.lm[s];
      lwm_[k] = PREM.wm[s];
    }
  }

  void run() {
    const Board& b = *B_;
    const bool can_play = nr_ > 0 && lex_ && lex_->loaded();
    if (mode_ == GEN_BEST && use_shadow_ && can_play) select_rack_tables();  // and the best exchange
    else if (ctx_.allow_exchange && ctx_.bag >= RACK_SIZE && nr_ > 0) gen_exchanges();
    if (can_play) {
      if (b.empty()) {
        load_line(0, 7);
        anchor_ = 7;
        last_anchor_ = -1;
        no_right_ = true;
        if (mode_ == GEN_BEST && use_shadow_ && use_wmp_ && !lex_->amap0.empty()) {
          spans_.clear();
          save_line();
          collect_spans(7);  // the opening: every span through the centre square
          search_spans();
        } else {
          rec(7, lex_->gaddag_root, 0, 1, 0, 7);
        }
      } else if (mode_ == GEN_BEST && use_shadow_) {
        run_best_shadow();
      } else {
        u32 rows = 0, cols = 0;  // lines holding an anchor
        if (near_) {
          for (int s = 0; s < NSQ; ++s) {
            is_anchor_[s] = near_[s] && !b.sq[s] && b.has_neighbor(s);
            if (is_anchor_[s]) {
              rows |= 1u << (s / N);
              cols |= 1u << (s % N);
            }
          }
        } else {
          for (int s = 0; s < NSQ; ++s) is_anchor_[s] = !b.sq[s] && b.has_neighbor(s);
          rows = cols = (1u << N) - 1;
        }
        for (int d = 0; d < 2; ++d)
          for (int line = 0; line < N; ++line) {
            if (!(((d == 0 ? rows : cols) >> line) & 1u)) continue;
            bool any = false;
            for (int k = 0; k < N && !any; ++k) any = is_anchor_[d == 0 ? line * N + k : k * N + line];
            if (!any) continue;
            load_line(d, line);
            last_anchor_ = -1;
            for (int k = 0; k < N; ++k) {
              if (!is_anchor_[d == 0 ? line * N + k : k * N + line]) continue;
              anchor_ = k;
              no_right_ = (k == N - 1) || lt_[k + 1] == 0;
              if (lx_[k]) rec(k, lex_->gaddag_root, 0, 1, 0, k);
              last_anchor_ = k;
            }
          }
      }
    }
    if (mode_ == GEN_ALL) {
      if (ctx_.add_pass) {
        Move p;
        p.type = MT_PASS;
        p.equity = pass_equity();
        out_->push_back(p);
      }
    } else if (best_eq_ <= -1e29f || ctx_.add_pass) {
      // Passing, when nothing else is possible or it has the best equity (as in the
      // full move list, e.g. a lone blank kept while the bag still has tiles).
      const float pe = pass_equity();
      if (best_eq_ <= -1e29f || pe > best_eq_) {
        best_ = Move();
        best_.type = MT_PASS;
        best_.equity = pe;
        best_eq_ = pe;
      }
    }
  }

  // --- Shadow pruning -----------------------------------------------------------------
  // For each anchor compute an upper bound on the equity of any play generated from it:
  // the highest rack tile scores are matched with the highest effective multipliers
  // (rearrangement inequality) for every feasible span, plus the best possible leave
  // for that many tiles played.  Anchors are then searched best-bound-first and the
  // search stops once no remaining anchor can beat the best play found.

  // Makes the rack tables current: restored if this rack was seen recently, else
  // computed (prepare_shadow) and kept.  Then applies the best exchange, if allowed.
  void select_rack_tables() {
    if (rcache_.empty()) {
      rcache_.resize(4);
      wmask_.assign(2048, WordMask());
    }
    int8_t cnt[NLET];
    for (int L = 0; L < NLET; ++L) cnt[L] = (int8_t)rk_[L];
    const int bagc = std::min(ctx_.bag, 16 + RACK_SIZE);  // with more in the bag, its size does not matter
    const int oface = ctx_.bag == 0 ? ctx_.opp_face : 0;  // used only when the bag is empty
    RackTables* T = nullptr;
    RackTables* victim = &rcache_[0];
    for (auto& E : rcache_) {
      if (E.stamp && E.bagc == bagc && E.opp_face == oface && E.use_leaves == ctx_.use_leaves &&
          std::memcmp(E.cnt, cnt, NLET) == 0) {
        T = &E;
        break;
      }
      if (E.used < victim->used) victim = &E;
    }
    if (T) {
      std::memcpy(nallsub_, T->nallsub, sizeof nallsub_);
      std::memcpy(nfront_, T->nfront, sizeof nfront_);
      std::memcpy(nfrontw_, T->nfrontw, sizeof nfrontw_);
      for (int k = 1; k <= RACK_SIZE; ++k) {
        std::memcpy(allsub_[k], T->allsub[k], nallsub_[k] * sizeof(ShadowSubset));
        std::memcpy(front_[k], T->front[k], nfront_[k] * sizeof(ShadowSubset));
        std::memcpy(frontw_[k], T->frontw[k], nfrontw_[k] * sizeof(ShadowSubset));
      }
      std::memcpy(rest1_, T->rest1, sizeof rest1_);
      rack_letters_ = T->rack_letters;
      std::memcpy(cap_mask_, T->cap_mask, sizeof cap_mask_);
      std::memcpy(cap_val_, T->cap_val, sizeof cap_val_);
      ncap_ = T->ncap;
      has_blank_ = T->has_blank;
      bingo7_ = T->bingo7;
      bingo8_ = T->bingo8;
    } else {
      T = victim;
      prepare_shadow();
      if (++rstamp_ == 0) {  // stamps wrapped: forget every memo and table
        for (auto& e : wmask_) e.stamp = 0;
        for (auto& E : rcache_) E.stamp = 0;
        rstamp_ = 1;
      }
      std::memcpy(T->cnt, cnt, NLET);
      T->bagc = bagc;
      T->opp_face = oface;
      T->use_leaves = ctx_.use_leaves;
      T->stamp = rstamp_;
      T->exch_known = false;
      std::memcpy(T->nallsub, nallsub_, sizeof nallsub_);
      std::memcpy(T->nfront, nfront_, sizeof nfront_);
      std::memcpy(T->nfrontw, nfrontw_, sizeof nfrontw_);
      for (int k = 1; k <= RACK_SIZE; ++k) {
        std::memcpy(T->allsub[k], allsub_[k], nallsub_[k] * sizeof(ShadowSubset));
        std::memcpy(T->front[k], front_[k], nfront_[k] * sizeof(ShadowSubset));
        std::memcpy(T->frontw[k], frontw_[k], nfrontw_[k] * sizeof(ShadowSubset));
      }
      std::memcpy(T->rest1, rest1_, sizeof rest1_);
      T->rack_letters = rack_letters_;
      std::memcpy(T->cap_mask, cap_mask_, sizeof cap_mask_);
      std::memcpy(T->cap_val, cap_val_, sizeof cap_val_);
      T->ncap = ncap_;
      T->has_blank = has_blank_;
      T->bingo7 = bingo7_;
      T->bingo8 = bingo8_;
    }
    T->used = ++rclock_;
    wstamp_ = T->stamp;
    if (ctx_.allow_exchange && ctx_.bag >= RACK_SIZE && nr_ > 0) {
      if (!T->exch_known) {
        gen_exchanges();  // from best_eq_ = -inf: depends on the rack alone
        T->exch_known = true;
        T->exch_eq = best_eq_;
        T->exch = best_;
      } else if (T->exch_eq > best_eq_) {
        best_eq_ = T->exch_eq;
        best_ = T->exch;
      }
    }
  }

  inline bool placeable(int k) const {
    const u32 x = lx_[k];
    return (x & rack_letters_) != 0 || (has_blank_ && x != 0);
  }

  void prepare_shadow() {
    rack_letters_ = 0;
    for (int L = 1; L < NLET; ++L)
      if (rk_[L]) rack_letters_ |= 1u << L;
    ncap_ = 0;
    for (int L = 1; L < NLET; ++L) {
      if (!((rack_letters_ >> L) & 1u)) continue;
      const int v = TILE_SCORE[L];
      int i = 0;
      while (i < ncap_ && cap_val_[i] > v) ++i;
      if (i < ncap_ && cap_val_[i] == v) {
        cap_mask_[i] |= 1u << L;
        continue;
      }
      for (int j = ncap_; j > i; --j) {
        cap_mask_[j] = cap_mask_[j - 1];
        cap_val_[j] = cap_val_[j - 1];
      }
      cap_mask_[i] = 1u << L;
      cap_val_[i] = v;
      ++ncap_;
    }
    has_blank_ = rk_[BLANK] > 0;
    bingo7_ = true;
    bingo8_ = ALL_LETTERS;
    if (nr_ == RACK_SIZE) lex_->bingo_info(rk_, bingo7_, bingo8_);
    for (int k = 0; k <= RACK_SIZE; ++k) nfront_[k] = 0;
    for (int L = 0; L < NLET; ++L) rest1_[L] = -1e30f;
    auto& all_sub = allsub_;
    int* nall = nallsub_;
    for (int k = 0; k <= RACK_SIZE; ++k) nall[k] = 0;
    // Enumerate canonical leaves (for each letter keep a prefix of its instances).
    int dl[NLET], dc[NLET], nd = 0;
    for (int L = 0; L < NLET; ++L)
      if (rk_[L]) {
        dl[nd] = L;
        dc[nd] = rk_[L];
        ++nd;
      }
    int keep[NLET] = {0};
    while (true) {
      u32 m = 0;
      int kept = 0;
      for (int j = 0; j < nd; ++j) {
        m |= ((1u << keep[j]) - 1u) << rstart_[dl[j]];
        kept += keep[j];
      }
      const int played = nr_ - kept;
      if (played > 0) {
        float rest;
        if (ctx_.bag > 0) {
          rest = leave_val(m);
        } else if (m == 0) {
          rest = (float)(2 * ctx_.opp_face);
        } else {
          leave_val(m);
          rest = -STATIC_PARAMS.not_out_mult * (float)lf_[m] - STATIC_PARAMS.not_out_const;
        }
        ShadowSubset& ss = all_sub[played][nall[played]++];
        ss.rest = rest;
        int q = 0;
        for (int j = 0; j < nd; ++j)
          for (int t = keep[j]; t < dc[j]; ++t) ss.sc[q++] = (u8)TILE_SCORE[dl[j]];
        std::sort(ss.sc, ss.sc + q, [](u8 x, u8 y) { return x > y; });
        ss.key = 0;
        ss.blanks = 0;
        for (int j = 0; j < nd; ++j) {
          const int np = dc[j] - keep[j];
          if (dl[j] == BLANK) ss.blanks = (u8)np;
          else ss.key += (u64)np * lex_->lkey[dl[j]];
        }
        ss.ok0 = played < 2 || ss.blanks >= 2 || (ss.blanks ? lex_->spell1.has(ss.key) : lex_->spell0.has(ss.key));
        if (played == 1)
          for (int j = 0; j < nd; ++j)
            if (keep[j] < dc[j]) rest1_[dl[j]] = rest;
      }
      int i = 0;
      while (i < nd) {
        if (keep[i] < dc[i]) {
          ++keep[i];
          break;
        }
        keep[i] = 0;
        ++i;
      }
      if (i == nd) break;
    }
    for (int k = 0; k <= RACK_SIZE; ++k) nfrontw_[k] = 0;
    for (int k = 1; k <= nr_; ++k) {
      for (int pass = 0; pass < 2; ++pass) {
        // pass 0: all subsets; pass 1: only those that spell a word by themselves
        for (int x = 0; x < nall[k]; ++x) {
          const ShadowSubset& X = all_sub[k][x];
          if (pass == 1 && !X.ok0) continue;
          bool dominated = false;
          for (int y = 0; y < nall[k] && !dominated; ++y) {
            if (y == x) continue;
            const ShadowSubset& Y = all_sub[k][y];
            if (Y.rest < X.rest || (pass == 1 && !Y.ok0)) continue;
            bool ge = true, gt = Y.rest > X.rest;
            for (int t = 0; t < k && ge; ++t) {
              if (Y.sc[t] < X.sc[t]) ge = false;
              else if (Y.sc[t] > X.sc[t]) gt = true;
            }
            dominated = ge && (gt || y < x);  // ties: keep the first copy only
          }
          if (dominated) continue;
          if (pass == 0) front_[k][nfront_[k]++] = X;
          if (pass == 1) frontw_[k][nfrontw_[k]++] = X;
        }
      }
    }
  }

  // Highest-scoring rack tile that may be placed on square k of the loaded line.
  inline int square_cap(int k) const {
    const u32 x = lx_[k];
    for (int i = 0; i < ncap_; ++i)
      if (x & cap_mask_[i]) return cap_val_[i];
    return 0;
  }

  // Quick bound on what the rack tiles of a k-tile span add (tile scores times the
  // span's effective multipliers `eff`, sorted descending, capped at `capped`, plus
  // the leave kept), from the Pareto frontier of tile subsets; a == the anchor square.
  float quick_var(int k, const int* eff, int capped, int a) const {
    float var = -1e30f;
    if (k == 1) {
      // exactly one tile, on the anchor square: only rack letters that fit there
      const u32 x = lx_[a];
      for (u32 m = x & rack_letters_; m; m &= m - 1) {
        const int L = lowest_bit64(m);
        var = std::max(var, (float)(TILE_SCORE[L] * eff[0]) + rest1_[L]);
      }
      if (x && rest1_[BLANK] > -1e29f) var = std::max(var, rest1_[BLANK]);
      return var;
    }
    for (int f = 0; f < nfront_[k]; ++f) {
      const ShadowSubset& F = front_[k][f];
      int dot = 0;
      for (int j = 0; j < k; ++j) dot += F.sc[j] * eff[j];
      var = std::max(var, (float)std::min(dot, capped) + F.rest);
    }
    return var;
  }

  // Max over the rack subsets of k tiles that can spell a word together with the
  // tiles played through (multiset key kt), of min(score bound, capped) + rest.  Any
  // value at or below `floor` may be returned for spans that cannot beat it.
  float refined_var(int k, const int* eff, int capped, u64 kt, bool through, int a, float floor) const {
    if (k == 1) {
      // One tile, on the anchor square, plus the tiles played through.
      const u32 x = lx_[a];
      float var = -1e30f;
      for (int L = 1; L < NLET; ++L)
        if (rest1_[L] > -1e29f && ((x >> L) & 1u) && lex_->spell0.has(kt + lex_->lkey[L]))
          var = std::max(var, (float)(TILE_SCORE[L] * eff[0]) + rest1_[L]);
      if (x && rest1_[BLANK] > -1e29f && lex_->spell1.has(kt)) var = std::max(var, rest1_[BLANK]);
      return var;
    }
    if (!through) {
      // Exact: the best subset that spells a word by itself (Pareto frontier).
      float var = -1e30f;
      for (int f = 0; f < nfrontw_[k]; ++f) {
        const ShadowSubset& F = frontw_[k][f];
        int dot = 0;
        for (int j = 0; j < k; ++j) dot += F.sc[j] * eff[j];
        var = std::max(var, (float)std::min(dot, capped) + F.rest);
      }
      return var;
    }
    const int n = nallsub_[k];
    float val[130];
    for (int f = 0; f < n; ++f) {
      const ShadowSubset& F = allsub_[k][f];
      int dot = 0;
      for (int j = 0; j < k; ++j) dot += F.sc[j] * eff[j];
      val[f] = (float)std::min(dot, capped) + F.rest;
    }
    while (true) {
      int bi = -1;
      float bv = -1e30f;
      for (int f = 0; f < n; ++f)
        if (val[f] > bv) {
          bv = val[f];
          bi = f;
        }
      // At or below the floor (or nothing left): bv still bounds every subset not yet
      // ruled out, which is all that matters.
      if (bi < 0 || bv <= floor) return bv;
      const ShadowSubset& F = allsub_[k][bi];
      if (F.blanks >= 2 || (F.blanks ? lex_->spell1.has(F.key + kt) : lex_->spell0.has(F.key + kt))) return bv;
      val[bi] = -1e30f;
    }
  }

  // Upper bound for anchor a of the loaded line (last_anchor_ must be set).  With
  // `refine`, spans whose quick bound exceeds `threshold` are bounded again using
  // only tile subsets that spell a word with the tiles played through.  With `vtab`
  // (and no refinement), every span's quick bound is recorded in vtab[lo * N + hi].
  float shadow_bound(int a, bool refine = false, float threshold = 0.f, float* vtab = nullptr) const {
    float best = -1e30f;
    if (!placeable(a)) return best;
    int plm[N], pwm[N], pxs[N], pcap[N];  // placed squares: left part (from anchor leftwards), then right part
    int lk = 1;
    plm[0] = llm_[a];
    pwm[0] = lwm_[a];
    pxs[0] = lxs_[a];
    pcap[0] = square_cap(a);
    int l_through = 0, l_tc = 0, l_tl = 0;  // through-tile face sum, count, last letter
    u64 l_key = 0;                          // multiset key of the through tiles
    struct SpanRec {
      float coarse;
      int fixed, k, capped;
      u64 kt;
      bool through;
      int eff[RACK_SIZE];
    };
    const bool fill = vtab != nullptr;  // record every span's bound (for pruning inside the anchor)
    refine = refine && !fill;           // a refined span would be missing from vtab
    constexpr int MAX_SPANS = 96;
    SpanRec spans[MAX_SPANS];
    int nspan = 0;
    int L = a;
    while (true) {
      if (L == 0 || lt_[L - 1] == 0) {
        int k = lk;
        int r_through = 0, r_tc = 0, r_tl = 0;
        u64 r_key = 0;
        int R = a;
        while (true) {
          bool feasible = true;
          if (k == RACK_SIZE) {
            // A 7-tile play spells exactly this span: rule out impossible bingos.
            const int tc = l_tc + r_tc;
            if (tc == 0) feasible = bingo7_;
            else if (tc == 1) feasible = (bingo8_ >> (l_tc ? l_tl : r_tl)) & 1u;
          }
          if (feasible && (R == N - 1 || lt_[R + 1] == 0)) {
            // evaluate span
            int wmt = 1, cross = 0;
            for (int j = 0; j < k; ++j) {
              wmt *= pwm[j];
              if (pxs[j] >= 0) cross += pxs[j] * pwm[j];
            }
            int eff[N];
            int capped = 0;
            for (int j = 0; j < k; ++j) {
              eff[j] = plm[j] * wmt + (pxs[j] >= 0 ? plm[j] * pwm[j] : 0);
              capped += pcap[j] * eff[j];
            }
            // insertion sort descending
            for (int x = 1; x < k; ++x) {
              const int v = eff[x];
              int y = x - 1;
              while (y >= 0 && eff[y] < v) {
                eff[y + 1] = eff[y];
                --y;
              }
              eff[y + 1] = v;
            }
            const int fixed = (l_through + r_through) * wmt + cross + (k == RACK_SIZE ? BINGO_BONUS : 0);
            const float var = quick_var(k, eff, capped, a);
            const int tc = l_tc + r_tc;
            const float v = (float)fixed + var;
            if (refine && var > -1e29f && v > threshold && v > best && (k >= 2 || tc > 0) && nspan < MAX_SPANS) {
              // Refined later, best first.
              SpanRec& S = spans[nspan++];
              S.coarse = v;
              S.fixed = fixed;
              S.k = k;
              S.capped = capped;
              S.kt = l_key + r_key;
              S.through = tc > 0;
              for (int j = 0; j < k; ++j) S.eff[j] = eff[j];
            } else {
              if (v > best) best = v;
              if (fill && v > vtab[L * N + R]) vtab[L * N + R] = v;
            }
          }
          if (R == N - 1) break;
          const int nx = R + 1;
          if (lt_[nx]) {
            r_through += tile_face(lt_[nx]);
            r_key += lex_->lkey[lt_[nx] & 31];
            ++r_tc;
            r_tl = lt_[nx] & 31;
            R = nx;
            continue;
          }
          if (k + 1 > nr_ || !placeable(nx)) break;
          plm[k] = llm_[nx];
          pwm[k] = lwm_[nx];
          pxs[k] = lxs_[nx];
          pcap[k] = square_cap(nx);
          ++k;
          R = nx;
        }
      }
      if (L == 0) break;
      const int nx = L - 1;
      if (lt_[nx]) {
        l_through += tile_face(lt_[nx]);
        l_key += lex_->lkey[lt_[nx] & 31];
        ++l_tc;
        l_tl = lt_[nx] & 31;
        L = nx;
        continue;
      }
      if (nx == last_anchor_ || lk + 1 > nr_ || !placeable(nx)) break;
      // shift right-part slots: left part occupies [0, lk)
      plm[lk] = llm_[nx];
      pwm[lk] = lwm_[nx];
      pxs[lk] = lxs_[nx];
      pcap[lk] = square_cap(nx);
      ++lk;
      L = nx;
    }
    // Refine the promising spans best first: stop once no remaining quick bound
    // can beat what the refined spans already reach.
    while (nspan > 0) {
      int bi = 0;
      for (int i = 1; i < nspan; ++i)
        if (spans[i].coarse > spans[bi].coarse) bi = i;
      const SpanRec S = spans[bi];
      spans[bi] = spans[--nspan];
      if (S.coarse <= best) break;
      const float floor = std::max(threshold, best);
      best = std::max(best, (float)S.fixed + refined_var(S.k, S.eff, S.capped, S.kt, S.through, a, floor - (float)S.fixed));
    }
    return best;
  }

  void run_best_shadow() {
    const Board& b = *B_;
    const bool wmp_on = use_wmp_ && !lex_->amap0.empty();
    if (wmp_on) {
      // Every span of every anchor with its bound, then all of them best first.
      // Anchors (empty squares next to a tile) of each row and column as bitmasks.
      u32 occ[2][N] = {};  // occ[0][row] bit col, occ[1][col] bit row
      for (int r = 0; r < N; ++r)
        for (int c = 0; c < N; ++c)
          if (b.sq[r * N + c]) {
            occ[0][r] |= 1u << c;
            occ[1][c] |= 1u << r;
          }
      spans_.clear();
      for (int d = 0; d < 2; ++d)
        for (int line = 0; line < N; ++line) {
          const u32 o = occ[d][line];
          u32 anchors = ~o & ((o << 1) | (o >> 1) | (line > 0 ? occ[d][line - 1] : 0) | (line < N - 1 ? occ[d][line + 1] : 0)) &
                        ((1u << N) - 1);
          if (!anchors) continue;
          load_line(d, line);
          save_line();
          last_anchor_ = -1;
          for (; anchors; anchors &= anchors - 1) {
            const int k = lowest_bit64(anchors);
            collect_spans(k);
            last_anchor_ = k;
          }
        }
      search_spans();
      return;
    }
    for (int s = 0; s < NSQ; ++s) is_anchor_[s] = !b.sq[s] && b.has_neighbor(s);
    int na = 0;
    for (int d = 0; d < 2; ++d)
      for (int line = 0; line < N; ++line) {
        bool any = false;
        for (int k = 0; k < N && !any; ++k) any = is_anchor_[d == 0 ? line * N + k : k * N + line];
        if (!any) continue;
        load_line(d, line);
        last_anchor_ = -1;
        for (int k = 0; k < N; ++k) {
          if (!is_anchor_[d == 0 ? line * N + k : k * N + line]) continue;
          const float bound = shadow_bound(k);
          if (bound > best_eq_) anchors_[na++] = {bound, (u8)d, (u8)line, (u8)k, (int8_t)last_anchor_, false};
          last_anchor_ = k;
        }
      }
    // Best bound first; an anchor's quick bound is refined (word-spelling check on its
    // promising spans) when it reaches the top of the queue.
    std::make_heap(anchors_, anchors_ + na);
    anchors_total += na;
    int cur_d = -1, cur_line = -1;
    while (na > 0) {
      AnchorInfo A = anchors_[0];
      if (A.bound <= best_eq_) break;
      std::pop_heap(anchors_, anchors_ + na);
      --na;
      if (A.dir != cur_d || A.line != cur_line) {
        load_line(A.dir, A.line);
        cur_d = A.dir;
        cur_line = A.line;
      }
      last_anchor_ = A.last;
      if (!A.refined && use_refine_) {
        A.bound = shadow_bound(A.col, true, best_eq_);
        A.refined = true;
        if (A.bound > best_eq_) {
          anchors_[na++] = A;
          std::push_heap(anchors_, anchors_ + na);
        }
        continue;
      }
      anchor_ = A.col;
      no_right_ = (A.col == N - 1) || lt_[A.col + 1] == 0;
      ++anchors_searched;
      if (use_refine_) prepare_span_tables(A.col);
      rec(A.col, lex_->gaddag_root, 0, 1, 0, A.col);
      span_prune_ = false;
    }
  }

  // --- Anagram ("word map") generation of the best play -------------------------------
  // Every span (contiguous squares containing an anchor, with its tiles played
  // through) of every anchor is bounded; spans are taken best first over the whole
  // board, and in each span the subsets of rack tiles that spell a word there are
  // tried.  The words spelled by a subset plus the tiles played through come straight
  // from the anagram maps; each is checked against the positions of the through tiles
  // and the cross-checks and scored exactly.  With two blanks, every letter is tried
  // for one of them.
  struct WSpan {
    float bound;
    u8 dir, line, lo, hi, k;
    bool through;
    u8 pos[RACK_SIZE];  // squares played: the anchor, then leftwards, then rightwards
    int fixed, capped, wmt, through_sum;
    u64 kt;
    int eff_sorted[RACK_SIZE];
  };
  std::vector<WSpan> spans_;
  // Spans are taken best first from buckets one point of bound wide (linked lists).
  static constexpr int NBUCKET = 512;
  static constexpr float BUCKET_LO = -128.f;  // bucket b holds bounds in [BUCKET_LO + b, BUCKET_LO + b + 1)
  std::vector<int> bucket_head_;              // allocated on first use; -1 = empty
  std::vector<int> span_next_;
  static int bucket_of(float bound) {
    const float x = bound - BUCKET_LO;
    return x < 0.f ? 0 : (x >= (float)(NBUCKET - 1) ? NBUCKET - 1 : (int)x);
  }
  // The loaded line of each (direction, line), saved when its spans are collected.
  struct LineData {
    u8 lt[N];
    u32 lx[N];
    i16 lxs[N];
    u8 llm[N], lwm[N];
  };
  LineData lsave_[2][N];

  void save_line() {
    LineData& D = lsave_[dir_][line_];
    std::memcpy(D.lt, lt_, sizeof lt_);
    std::memcpy(D.lx, lx_, sizeof lx_);
    std::memcpy(D.lxs, lxs_, sizeof lxs_);
    std::memcpy(D.llm, llm_, sizeof llm_);
    std::memcpy(D.lwm, lwm_, sizeof lwm_);
  }
  void restore_line(int d, int line) {
    const LineData& D = lsave_[d][line];
    dir_ = d;
    line_ = line;
    std::memcpy(lt_, D.lt, sizeof lt_);
    std::memcpy(lx_, D.lx, sizeof lx_);
    std::memcpy(lxs_, D.lxs, sizeof lxs_);
    std::memcpy(llm_, D.llm, sizeof llm_);
    std::memcpy(lwm_, D.lwm, sizeof lwm_);
  }

  // Appends the spans of anchor a of the loaded line (last_anchor_ set) whose quick
  // bound beats the best play so far.  The walk is shadow_bound's; the word multiplier,
  // cross-word sums, score cap and sorted effective multipliers are kept up to date as
  // a span grows (a square without a word multiplier inserts one value; one with a
  // word multiplier rescales them all).
  void collect_spans(int a) {
    if (!placeable(a)) return;
    // Placed squares in slot order: the anchor, then leftwards, then rightwards.
    int plm[N], pwm[N], pcc[N], pxw[N], pcap[N];  // pcc/pxw: letter-multiplier and fixed part of a cross word
    u8 ppos[N];
    auto set_slot = [&](int j, int sq) {
      plm[j] = llm_[sq];
      pwm[j] = lwm_[sq];
      const bool cross = lxs_[sq] >= 0;
      pcc[j] = cross ? llm_[sq] * lwm_[sq] : 0;
      pxw[j] = cross ? lxs_[sq] * lwm_[sq] : 0;
      pcap[j] = square_cap(sq);
      ppos[j] = (u8)sq;
    };
    auto insert_desc = [](int* E, int n, int v) {  // E[0, n) sorted descending
      int y = n - 1;
      while (y >= 0 && E[y] < v) {
        E[y + 1] = E[y];
        --y;
      }
      E[y + 1] = v;
    };
    auto rebuild = [&](int* E, int n, int w) {
      for (int j = 0; j < n; ++j) insert_desc(E, j, plm[j] * w + pcc[j]);
    };
    // Left part: slots [0, lk).
    int lk = 1;
    set_slot(0, a);
    int wL = pwm[0], crossL = pxw[0], capAL = pcap[0] * plm[0], capBL = pcap[0] * pcc[0];
    int EL[RACK_SIZE];
    EL[0] = plm[0] * wL + pcc[0];
    int l_through = 0, l_tc = 0, l_tl = 0;
    u64 l_key = 0;
    int L = a;
    while (true) {
      if (L == 0 || lt_[L - 1] == 0) {
        int k = lk;
        int w = wL, cross = crossL, capA = capAL, capB = capBL;
        int E[RACK_SIZE];
        std::memcpy(E, EL, sizeof E);
        int r_through = 0, r_tc = 0, r_tl = 0;
        u64 r_key = 0;
        int R = a;
        while (true) {
          if (R == N - 1 || lt_[R + 1] == 0) {
            const int tc = l_tc + r_tc;
            // A lone tile needs a main word here; in the down pass it must not also
            // form an across word (that play belongs to the across pass).
            bool feasible = (k >= 2 || tc > 0) && !(k == 1 && dir_ == 1 && lxs_[a] >= 0);
            if (feasible && k == RACK_SIZE) {
              // A 7-tile play spells exactly this span: rule out impossible bingos.
              if (tc == 0) feasible = bingo7_;
              else if (tc == 1) feasible = (bingo8_ >> (l_tc ? l_tl : r_tl)) & 1u;
            }
            if (feasible) {
              const int capped = w * capA + capB;  // sum of cap * effective multiplier
              const int fixed = (l_through + r_through) * w + cross + (k == RACK_SIZE ? BINGO_BONUS : 0);
              const float bound = (float)fixed + quick_var(k, E, capped, a);
              if (bound > best_eq_) {
                spans_.emplace_back();
                WSpan& S = spans_.back();
                S.bound = bound;
                S.dir = (u8)dir_;
                S.line = (u8)line_;
                S.lo = (u8)L;
                S.hi = (u8)R;
                S.k = (u8)k;
                S.through = tc > 0;
                std::memcpy(S.pos, ppos, sizeof S.pos);  // fixed size: entries past k are unused
                std::memcpy(S.eff_sorted, E, sizeof S.eff_sorted);
                S.fixed = fixed;
                S.capped = capped;
                S.wmt = w;
                S.through_sum = l_through + r_through;
                S.kt = l_key + r_key;
              }
            }
          }
          if (R == N - 1) break;
          const int nx = R + 1;
          if (lt_[nx]) {
            r_through += tile_face(lt_[nx]);
            r_key += lex_->lkey[lt_[nx] & 31];
            ++r_tc;
            r_tl = lt_[nx] & 31;
            R = nx;
            continue;
          }
          if (k + 1 > nr_ || !placeable(nx)) break;
          set_slot(k, nx);
          cross += pxw[k];
          capA += pcap[k] * plm[k];
          capB += pcap[k] * pcc[k];
          if (pwm[k] == 1) {
            insert_desc(E, k, plm[k] * w + pcc[k]);
          } else {
            w *= pwm[k];
            rebuild(E, k + 1, w);
          }
          ++k;
          R = nx;
        }
      }
      if (L == 0) break;
      const int nx = L - 1;
      if (lt_[nx]) {
        l_through += tile_face(lt_[nx]);
        l_key += lex_->lkey[lt_[nx] & 31];
        ++l_tc;
        l_tl = lt_[nx] & 31;
        L = nx;
        continue;
      }
      if (nx == last_anchor_ || lk + 1 > nr_ || !placeable(nx)) break;
      set_slot(lk, nx);  // right-part slots start after the left part
      crossL += pxw[lk];
      capAL += pcap[lk] * plm[lk];
      capBL += pcap[lk] * pcc[lk];
      if (pwm[lk] == 1) {
        insert_desc(EL, lk, plm[lk] * wL + pcc[lk]);
      } else {
        wL *= pwm[lk];
        rebuild(EL, lk + 1, wL);
      }
      ++lk;
      L = nx;
    }
  }

  // Searches the collected spans best bound first (to within a point), until no bound
  // beats the best play.
  void search_spans() {
    const int n = (int)spans_.size();
    if (bucket_head_.empty()) bucket_head_.assign(NBUCKET, -1);
    span_next_.resize(n);
    int top = -1, bottom = NBUCKET;
    for (int i = 0; i < n; ++i) {
      const int b = bucket_of(spans_[i].bound);
      span_next_[i] = bucket_head_[b];
      bucket_head_[b] = i;
      top = std::max(top, b);
      bottom = std::min(bottom, b);
    }
    anchors_total += n;
    int cur_d = -1, cur_line = -1;
    for (int b = top; b >= bottom; --b) {
      // every bound left is below this bucket's upper edge (the top bucket has none)
      if (b < NBUCKET - 1 && BUCKET_LO + (float)(b + 1) <= best_eq_) break;
      for (int i = bucket_head_[b]; i >= 0; i = span_next_[i]) {
        const WSpan& S = spans_[i];
        if (S.bound <= best_eq_) continue;
        if (S.dir != cur_d || S.line != cur_line) {
          restore_line(S.dir, S.line);
          cur_d = S.dir;
          cur_line = S.line;
        }
        ++anchors_searched;
        wmp_search_span(S);
      }
    }
    for (int b = bottom; b <= top; ++b) bucket_head_[b] = -1;
  }

  u64 word_mask(int k, u64 kt, bool through) {
    auto compute = [&]() {
      u64 m = 0;
      const u32* list;
      for (int f = 0; f < nallsub_[k]; ++f) {
        const ShadowSubset& F = allsub_[k][f];
        if (!through && !F.ok0) continue;  // must spell a word on its own
        // two blanks: not resolved here (the search tries every letter for one of them)
        if (F.blanks >= 2 || (F.blanks ? lex_->amap1.find(F.key + kt, list) : lex_->amap0.find(F.key + kt, list)) > 0)
          m |= 1ull << f;
      }
      return m;
    };
    const u32 hmask = (u32)wmask_.size() - 1;
    u32 h = (u32)((kt ^ (u64)(2 * k + through) * 0x9E3779B97F4A7C15ULL) >> 40) & hmask;
    for (int probe = 0; probe < 16; ++probe, h = (h + 1) & hmask) {
      WordMask& e = wmask_[h];
      if (e.stamp != wstamp_) {  // free, or another rack's: take it
        const u64 m = compute();
        e.kt = kt;
        e.mask = m;
        e.stamp = wstamp_;
        e.k = (u8)k;
        e.through = through;
        return m;
      }
      if (e.kt == kt && e.k == k && e.through == through) return e.mask;
    }
    return compute();
  }

  void wmp_search_span(const WSpan& S) {
    const int k = S.k;
    // Only subsets that spell a word here, each as soon as its bound beats the best play.
    for (u64 m = word_mask(k, S.kt, S.through); m; m &= m - 1) {
      const ShadowSubset& F = allsub_[k][lowest_bit64(m)];
      int dot = 0;
      for (int j = 0; j < k; ++j) dot += F.sc[j] * S.eff_sorted[j];
      if ((float)S.fixed + (float)std::min(dot, S.capped) + F.rest <= best_eq_) continue;
      const u32* list;
      if (F.blanks < 2) {
        const int cnt = F.blanks ? lex_->amap1.find(F.key + S.kt, list) : lex_->amap0.find(F.key + S.kt, list);
        for (int e = 0; e < cnt; ++e) wmp_try_word(S, F, list[e], 0);
      } else {
        // Two blanks: try every letter x for one of them, the one-blank map gives the
        // other's letter y; each pair of letters is tried once (y >= x).
        for (int x = 1; x < NLET; ++x) {
          const int cnt = lex_->amap1.find(F.key + S.kt + lex_->lkey[x], list);
          for (int e = 0; e < cnt; ++e)
            if ((int)(list[e] >> 24) >= x) wmp_try_word(S, F, list[e], x);
        }
      }
    }
  }

  void wmp_try_word(const WSpan& S, const ShadowSubset& F, u32 entry, int y2) {
    const u32 id = entry & 0xFFFFFFu;
    const int y = (int)(entry >> 24);  // letter played by a blank (0: no blank); y2: the second blank
    const u8* w = lex_->wl_letters.data() + lex_->wl_off[id];
    const int len = S.hi - S.lo + 1;
    if ((int)(lex_->wl_off[id + 1] - lex_->wl_off[id]) != len) return;
    // Through tiles must sit where the word has them.
    for (int c = S.lo; c <= S.hi; ++c)
      if (lt_[c] && (lt_[c] & 31) != w[c - S.lo]) return;
    // Cross-checks, and where the blanks go: each on a placed square holding its letter,
    // the one with the smallest multiplier.
    for (int i = 0; i < S.k; ++i) {
      const int p = S.pos[i];
      if (!((lx_[p] >> w[p - S.lo]) & 1u)) return;
    }
    int blank_at = -1, blank2_at = -1;
    auto place_blank = [&](int letter, int taken) {
      int at = -1, best_e = 1 << 30;
      for (int i = 0; i < S.k; ++i) {
        const int p = S.pos[i];
        if (p == taken || w[p - S.lo] != letter) continue;
        const int e = llm_[p] * S.wmt + (lxs_[p] >= 0 ? llm_[p] * lwm_[p] : 0);
        if (e < best_e) {
          best_e = e;
          at = p;
        }
      }
      return at;
    };
    if (y) blank_at = place_blank(y, -1);
    if (y2) blank2_at = place_blank(y2, blank_at);
    int lsum = S.through_sum, xsum = 0;
    for (int i = 0; i < S.k; ++i) {
      const int p = S.pos[i];
      const int ls = (p == blank_at || p == blank2_at) ? 0 : TILE_SCORE[w[p - S.lo]] * llm_[p];
      lsum += ls;
      if (lxs_[p] >= 0) xsum += (lxs_[p] + ls) * lwm_[p];
    }
    const int score = lsum * S.wmt + xsum + (S.k == RACK_SIZE ? BINGO_BONUS : 0);
    const float eq = (float)score + F.rest;
    if (eq <= best_eq_) return;
    best_eq_ = eq;
    for (int c = S.lo; c <= S.hi; ++c)
      strip_[c] = lt_[c] ? 0 : (u8)(w[c - S.lo] | ((c == blank_at || c == blank2_at) ? BLANK_BIT : 0));
    tiles_played_ = S.k;
    fill(best_, S.lo, S.hi, score, eq);
    tiles_played_ = 0;
  }

  // Bounds of every span of anchor a, turned into the two tables rec() consults:
  // span_lmax_[c]  (still growing leftwards, leftmost square c): best span with left end <= c
  // span_rmax_[l][c] (growing rightwards from left end l, next square c): best span l..r, r >= c
  void prepare_span_tables(int a) {
    float vtab[NSQ];
    for (int i = 0; i < NSQ; ++i) vtab[i] = -1e30f;
    shadow_bound(a, false, 0.f, vtab);
    float run = -1e30f;
    for (int l = 0; l <= a; ++l) {
      float row = -1e30f;
      float suffix = -1e30f;
      for (int c = N - 1; c >= a; --c) {
        suffix = std::max(suffix, vtab[l * N + c]);
        span_rmax_[l][c] = suffix;
        row = std::max(row, vtab[l * N + c]);
      }
      run = std::max(run, row);
      span_lmax_[l] = run;
    }
    span_prune_ = true;
  }

  float pass_equity() {
    if (ctx_.bag > 0) return (nr_ <= 6 ? leave_val(full_mask_) : 0.f) - STATIC_PARAMS.pass_penalty;
    leave_val(full_mask_);
    return -(STATIC_PARAMS.not_out_mult * (float)lf_[full_mask_] + STATIC_PARAMS.not_out_const);
  }

  void rec(int col, u32 list, int lsum, int wmul, int xsum, int leftmost) {
    // Inside an anchor: stop when no span this partial play can still grow into
    // has a bound above the best play found so far.
    if (span_prune_ && (col <= anchor_ ? span_lmax_[col] : span_rmax_[leftmost][col]) <= best_eq_) return;
    const u8 cur = lt_[col];
    if (cur) {
      const u32 a = lex_->find(list, cur & 31);
      if (a) {
        strip_[col] = 0;
        go_on(col, lex_->nodes[a], lsum + tile_face(cur), wmul, xsum, leftmost);
      }
      return;
    }
    if (!rack_left_) return;
    // Letters we could put here: allowed by the cross-check and available on the rack.
    const u32 avail = lx_[col] & (rk_[BLANK] ? ALL_LETTERS : rack_mask_);
    if (!avail) return;
    const int max_label = highest_bit(avail);
    const u32* nodes = lex_->nodes.data();
    const int lm = llm_[col], wm = lwm_[col], xs = lxs_[col];
    for (u32 i = list;; ++i) {
      const u32 v = nodes[i];
      const int L = Lexicon::label(v);
      if (L > max_label) break;  // arcs are sorted by label
      if ((avail >> L) & 1u) {
        if (rk_[L]) {
          take(L);
          strip_[col] = (u8)L;
          const int ls = TILE_SCORE[L] * lm;
          go_on(col, v, lsum + ls, wmul * wm, xs >= 0 ? xsum + (xs + ls) * wm : xsum, leftmost);
          untake(L);
        }
        if (rk_[BLANK]) {
          take(BLANK);
          strip_[col] = (u8)(L | BLANK_BIT);
          go_on(col, v, lsum, wmul * wm, xs >= 0 ? xsum + xs * wm : xsum, leftmost);
          untake(BLANK);
        }
      }
      if (Lexicon::is_end(v)) break;
    }
  }

  void go_on(int col, u32 v, int lsum, int wmul, int xsum, int leftmost) {
    const u32 next = Lexicon::child(v);
    const bool acc = Lexicon::accepts(v);
    if (col <= anchor_) {
      const bool no_left = (col == 0) || lt_[col - 1] == 0;
      if (acc && no_left && no_right_) record(col, anchor_, lsum, wmul, xsum);
      if (!next) return;
      if (col > 0 && col - 1 != last_anchor_) rec(col - 1, next, lsum, wmul, xsum, col - 1);
      if (no_left && anchor_ < N - 1) {
        const u32 sv = lex_->nodes[next];
        if (Lexicon::label(sv) == SEP) {
          const u32 nl = Lexicon::child(sv);
          if (nl) rec(anchor_ + 1, nl, lsum, wmul, xsum, col);
        }
      }
    } else {
      const bool no_right = (col == N - 1) || lt_[col + 1] == 0;
      if (acc && no_right) record(leftmost, col, lsum, wmul, xsum);
      if (next && col < N - 1) rec(col + 1, next, lsum, wmul, xsum, leftmost);
    }
  }

  void record(int start, int end, int lsum, int wmul, int xsum) {
    const int tp = tiles_played_;
    if (dir_ == 1 && tp == 1) {
      // A lone tile that also forms an across word was already generated by the across pass.
      for (int k = start; k <= end; ++k)
        if (strip_[k]) {
          if (lxs_[k] >= 0) return;
          break;
        }
    }
    const int score = lsum * wmul + xsum + (tp == RACK_SIZE ? BINGO_BONUS : 0);
    const float eq = equity(score, mask_);
    if (mode_ == GEN_BEST) {
      if (eq <= best_eq_) return;
      best_eq_ = eq;
      fill(best_, start, end, score, eq);
    } else {
      out_->emplace_back();
      fill(out_->back(), start, end, score, eq);
    }
  }

  void fill(Move& m, int start, int end, int score, float eq) {
    m.type = MT_PLACE;
    m.dir = (u8)dir_;
    if (dir_ == 0) {
      m.row = (u8)line_;
      m.col = (u8)start;
    } else {
      m.row = (u8)start;
      m.col = (u8)line_;
    }
    m.len = (u8)(end - start + 1);
    std::memcpy(m.tiles, strip_ + start, m.len);
    if (m.len < N) std::memset(m.tiles + m.len, 0, N - m.len);
    m.ntiles = (u8)tiles_played_;
    m.score = (i16)score;
    m.equity = eq;
  }

  void gen_exchanges() {
    int dl[NLET], dc[NLET], nd = 0;
    for (int L = 0; L < NLET; ++L)
      if (rk_[L]) {
        dl[nd] = L;
        dc[nd] = rk_[L];
        ++nd;
      }
    int k[NLET] = {0};
    while (true) {
      int i = 0;
      while (i < nd) {
        if (k[i] < dc[i]) {
          ++k[i];
          break;
        }
        k[i] = 0;
        ++i;
      }
      if (i == nd) break;
      u32 kept = 0;
      int total = 0;
      for (int j = 0; j < nd; ++j) {
        const int keep = dc[j] - k[j];
        kept |= ((1u << keep) - 1u) << rstart_[dl[j]];
        total += k[j];
      }
      const float eq = leave_val(kept);
      if (mode_ == GEN_BEST) {
        if (eq <= best_eq_) continue;
        best_eq_ = eq;
        best_ = Move();
        fill_exchange(best_, dl, k, nd, total, eq);
      } else {
        out_->emplace_back();
        fill_exchange(out_->back(), dl, k, nd, total, eq);
      }
    }
  }

  static void fill_exchange(Move& m, const int* dl, const int* k, int nd, int total, float eq) {
    m.type = MT_EXCHANGE;
    m.len = (u8)total;
    m.ntiles = (u8)total;
    int p = 0;
    for (int j = 0; j < nd; ++j)
      for (int t = 0; t < k[j]; ++t) m.tiles[p++] = (u8)dl[j];
    for (; p < N; ++p) m.tiles[p] = 0;
    m.score = 0;
    m.equity = eq;
  }
};

inline void sort_by_equity(std::vector<Move>& v) {
  std::stable_sort(v.begin(), v.end(), [](const Move& a, const Move& b) { return a.equity > b.equity; });
}

}  // namespace tf
namespace tf {

// =====================================================================================
// §10  Game state and rules (WESPA / NASPA tournament rules)
// =====================================================================================
//
//  * 100 tiles, racks of 7, 50-point bingo bonus.
//  * Exchanges need at least 7 tiles in the bag.
//  * The game ends when a player plays out with the bag empty (they gain twice the
//    value of the opponent's rack in spread: + opp rack for them, - for the opponent),
//    or after six consecutive scoreless turns (each player loses their own rack value).

inline int draw_tile(Rack& bag, Rng& rng) {
  u32 x = rng.below((u32)bag.n);
  for (int L = 0; L < NLET; ++L) {
    if (x < (u32)bag.c[L]) {
      bag.sub(L);
      return L;
    }
    x -= (u32)bag.c[L];
  }
  return -1;
}

inline void fill_rack(Rack& rack, Rack& bag, Rng& rng) {
  while (rack.n < RACK_SIZE && bag.n > 0) rack.add(draw_tile(bag, rng));
}

// Tiles not visible to the holder of `rack`: full distribution - board - rack.
inline Rack unseen_from(const Board& b, const Rack& rack) {
  Rack u = Rack::full_distribution();
  for (int s = 0; s < NSQ; ++s)
    if (b.sq[s]) u.sub(tile_rack_code(b.sq[s]));
  u.sub_all(rack);
  return u;
}

struct GameEvent {
  int player;
  Move move;
  Rack rack_before;  // the mover's rack before the move (known in self-play/analysis)
  int score_after;
  std::string note;
};

struct Game {
  Board board;
  Rack rack[2];
  int score[2] = {0, 0};
  Rack bag;
  int turn = 0;   // player to move
  int zeros = 0;  // consecutive scoreless turns
  bool over = false;
  int end_bonus[2] = {0, 0};
  std::vector<GameEvent> events;
  // What the player to move knows about the opponent's last move (for inference).
  bool has_last = false;
  Board board_before_last;
  Move last_move;

  void reset(Rng& rng) {
    board.clear();
    bag = Rack::full_distribution();
    for (int p = 0; p < 2; ++p) {
      rack[p].clear();
      score[p] = 0;
      end_bonus[p] = 0;
    }
    fill_rack(rack[0], bag, rng);
    fill_rack(rack[1], bag, rng);
    turn = 0;
    zeros = 0;
    over = false;
    events.clear();
    has_last = false;
  }

  // Applies a legal move for the player to move and draws replacement tiles.
  void apply(const Lexicon& lex, const Move& m, Rng& rng) {
    const int p = turn;
    GameEvent ev;
    ev.player = p;
    ev.move = m;
    ev.rack_before = rack[p];
    board_before_last = board;
    last_move = m;
    has_last = true;
    if (m.type == MT_PLACE) {
      board.place(lex, m);
      rack[p].sub_all(m.used());
      score[p] += m.score;
      fill_rack(rack[p], bag, rng);
    } else if (m.type == MT_EXCHANGE) {
      const Rack out = m.used();
      rack[p].sub_all(out);
      fill_rack(rack[p], bag, rng);
      bag.add_all(out);
    }
    zeros = (m.type == MT_PLACE && m.score != 0) ? 0 : zeros + 1;
    if (m.type == MT_PLACE && rack[p].n == 0 && bag.n == 0) {
      const int v = rack[1 - p].face();
      score[p] += v;
      score[1 - p] -= v;
      end_bonus[p] = v;
      end_bonus[1 - p] = -v;
      over = true;
    } else if (zeros >= 6) {
      for (int q = 0; q < 2; ++q) {
        const int v = rack[q].face();
        score[q] -= v;
        end_bonus[q] = -v;
      }
      over = true;
    }
    ev.score_after = score[p];
    events.push_back(ev);
    turn = 1 - p;
  }
};

// Everything the player to move is entitled to know, plus derived quantities.
struct Position {
  Board board;
  Rack rack;             // rack of the player to move
  int my_score = 0, opp_score = 0;
  int zeros = 0;         // consecutive scoreless turns so far
  Rack unseen;           // bag + opponent's rack (from the mover's point of view)
  int bag_n = 0;         // tiles in the bag
  int opp_n = 0;         // tiles on the opponent's rack
  // Optional: the opponent's last move and the board before it (enables inference).
  bool has_opp_last = false;
  Board board_before_opp;
  Move opp_last;

  void derive() {
    unseen = unseen_from(board, rack);
    opp_n = std::min(RACK_SIZE, unseen.n);
    bag_n = unseen.n - opp_n;
  }
  int spread() const { return my_score - opp_score; }

  static Position from_game(const Game& g) {
    Position P;
    const int p = g.turn;
    P.board = g.board;
    P.rack = g.rack[p];
    P.my_score = g.score[p];
    P.opp_score = g.score[1 - p];
    P.zeros = g.zeros;
    P.derive();
    if (g.has_last && !g.events.empty() && g.events.back().player != p) {
      P.has_opp_last = true;
      P.board_before_opp = g.board_before_last;
      P.opp_last = g.last_move;
    }
    return P;
  }
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §11  Monte-Carlo simulation ("rollouts")
// =====================================================================================
//
//  For each candidate move and each iteration:
//    1. deal the opponent a rack from the unseen tiles (weighted by inference when
//       available) and shuffle the rest into a bag;
//    2. play the candidate, then let both sides play `plies` more turns with the static
//       evaluator;
//    3. record the spread gained (+ residual leave values) and the win probability of
//       the resulting position.
//  Every candidate sees exactly the same opponent rack and bag order in a given
//  iteration (common random numbers), so comparisons between candidates are paired
//  and luck largely cancels.  Candidates that are significantly worse than the leader
//  (paired z-test) are dropped early, focusing the remaining time on close decisions;
//  the closest challenger is never dropped, so the whole time budget goes into the
//  decision (a move's unused time is not saved for later).

struct SimParams {
  int plies = 2;
  int playout_bag = 7;        // with this many tiles or fewer in the bag, play out to the end
  int max_candidates = 20;
  int late_candidates = 0;    // with playout_bag tiles or fewer in the bag (0: max_candidates)
  int deep_k = 0;             // deeper second stage on this many finalists (0: off)
  int deep_plies = 4;
  double deep_frac = 0.5;     // share of simulation time spent on the first stage
  int max_iterations = 1000000;  // per candidate (in practice the clock decides)
  double time_limit = 5.0;    // seconds
  int threads = 1;
  bool win_objective = true;      // rank by win probability (else by spread/equity)
  double equity_tiebreak = 0.0008;  // win-objective: + this much per point of equity
  double prune_z = 2.4;           // successive-elimination threshold
  int min_iterations = 96;        // before any pruning
  int batch = 48;                 // iterations between pruning checks
  double shrink_tau = 4.0;        // prior: static equity is right to within ~this many points
  // The opponent's rack, when inference has nothing: a draw from the unseen tiles in which
  // each tile kept counts with odds exp(keep * its one-tile leave value), so good tiles are
  // likelier on the rack than in the bag (0: a uniform draw).  Applied with 1..keep_bag
  // tiles in the bag.
  double keep = 0.0;
  int keep_bag = 7;
  // Play-outs to the end: once the bag is empty, each side picks among its eg_k best static
  // moves the one that does best when both sides then play greedily to the end, instead
  // of the static best (0: static play to the end).
  int eg_k = 0;
  u64 seed = 0;                   // 0 = random
  bool verbose = false;
};

// Fisher's noncentral hypergeometric distribution over racks: the weight of a rack is the
// product, over its tiles, of their letters' odds (times the number of ways to pick them
// from the unseen tiles).  build() tabulates the partial sums once per search; sample()
// then draws a rack exactly, letter by letter.
struct RackPrior {
  int n = 0;
  int cnt[NLET] = {0};
  double w[NLET] = {0};
  double g[NLET + 1][RACK_SIZE + 1] = {{0}};  // g[i][k]: weight of all k-tile picks from letters i..
  static double choose(int a, int b) {
    if (b < 0 || b > a) return 0;
    double r = 1;
    for (int i = 1; i <= b; ++i) r = r * (a - b + i) / i;
    return r;
  }
  void build(const Rack& unseen, int rack_n, const double* odds) {
    n = std::min(rack_n, unseen.n);
    for (int L = 0; L < NLET; ++L) {
      cnt[L] = unseen.c[L];
      w[L] = odds[L];
    }
    for (int k = 0; k <= RACK_SIZE; ++k) g[NLET][k] = k == 0 ? 1.0 : 0.0;
    for (int L = NLET - 1; L >= 0; --L)
      for (int k = 0; k <= RACK_SIZE; ++k) {
        double s = 0, wj = 1;
        for (int j = 0; j <= std::min(cnt[L], k); ++j, wj *= w[L]) s += choose(cnt[L], j) * wj * g[L + 1][k - j];
        g[L][k] = s;
      }
  }
  void sample(Rng& rng, Rack& out) const {
    out.clear();
    int k = n;
    for (int L = 0; L < NLET && k > 0; ++L) {
      const double u = rng.uniform() * g[L][k];
      double acc = 0, wj = 1;
      int pick = -1, last = 0;  // last: the largest feasible count, if rounding leaves u unmatched
      for (int j = 0; j <= std::min(cnt[L], k); ++j, wj *= w[L]) {
        const double t = choose(cnt[L], j) * wj * g[L + 1][k - j];
        if (t <= 0) continue;
        last = j;
        acc += t;
        if (u < acc) {
          pick = j;
          break;
        }
      }
      if (pick < 0) pick = last;
      out.add(L, pick);
      k -= pick;
    }
  }
};

struct SimCandidate {
  Move move;
  float static_eq = 0;
  std::vector<float> eq, win;
  int n = 0;
  bool active = true;
  double post = 0;  // posterior objective relative to the most-simulated candidate (ranking key)
  double prior_w = NAN;  // share of the posterior's precision that comes from the static prior (0..1)
  double sim_diff = NAN;  // simulated difference from the reference candidate (objective units)
  double mean_eq() const {
    double s = 0;
    for (int i = 0; i < n; ++i) s += eq[i];
    return n ? s / n : 0;
  }
  double mean_win() const {
    double s = 0;
    for (int i = 0; i < n; ++i) s += win[i];
    return n ? s / n : 0;
  }
  double se_eq() const { return se(eq); }
  double se_win() const { return se(win); }
  double objective(bool win_obj, double tb) const { return win_obj ? mean_win() + tb * mean_eq() : mean_eq(); }

 private:
  double se(const std::vector<float>& v) const {
    if (n < 2) return 0;
    double s = 0, s2 = 0;
    for (int i = 0; i < n; ++i) {
      s += v[i];
      s2 += (double)v[i] * v[i];
    }
    const double m = s / n;
    const double var = std::max(0.0, s2 / n - m * m) * n / (n - 1);
    return std::sqrt(var / n);
  }
};

struct SimResult {
  std::vector<SimCandidate> cands;  // best first
  int iterations = 0;
  double seconds = 0;
  long positions = 0;
};

// Weighted list of possible opponent leaves (from inference).
struct OppModel {
  std::vector<Rack> leaves;
  std::vector<double> cum;  // cumulative weights
  int drawn = 0;            // tiles the opponent drew after the inferred play
  bool empty() const { return leaves.empty(); }
  const Rack& sample(Rng& rng) const {
    const double x = rng.uniform() * cum.back();
    const size_t i = std::upper_bound(cum.begin(), cum.end(), x) - cum.begin();
    return leaves[std::min(i, leaves.size() - 1)];
  }
};

class Simulator {
 public:
  Simulator(const Lexicon* lex, const LeaveTable* lt, const WinModel* wm) : lex_(lex), lt_(lt), wm_(wm) {}

  // Top candidates by static equity for position P (`generated`: how many legal moves there were).
  std::vector<Move> candidates(const Position& P, int max_n, int* generated = nullptr, const Move* keep = nullptr) const {
    MoveGen gen(lex_, lt_);
    std::vector<Move> all;
    EvalCtx ctx = ctx_for(P);
    gen.generate_all(P.board, P.rack, ctx, all);
    if (generated) *generated = (int)all.size();
    sort_by_equity(all);
    if (keep) {  // appended after the normal candidates if the cut would drop it
      for (int i = max_n; i < (int)all.size(); ++i)
        if (all[i].same_as(*keep)) {
          all[max_n] = all[i];
          ++max_n;
          break;
        }
    }
    if ((int)all.size() > max_n) all.resize(max_n);
    return all;
  }

  static EvalCtx ctx_for(const Position& P) {
    EvalCtx ctx;
    ctx.bag = P.bag_n;
    ctx.opp_face = P.bag_n == 0 ? P.unseen.face() : 0;
    ctx.allow_exchange = P.bag_n >= RACK_SIZE;
    return ctx;
  }

  SimResult run(const Position& P, const std::vector<Move>& cand_moves, const SimParams& sp, const OppModel* opp = nullptr,
                const std::function<void(const SimResult&)>& progress = nullptr) const {
    SimResult R;
    const double t0 = now_s();
    const u64 seed = sp.seed ? sp.seed : time_seed();
    R.cands.resize(cand_moves.size());
    for (size_t i = 0; i < cand_moves.size(); ++i) {
      R.cands[i].move = cand_moves[i];
      R.cands[i].static_eq = cand_moves[i].equity;
    }
    if (cand_moves.size() <= 1) {
      R.seconds = now_s() - t0;
      return R;
    }
    const int nthreads = std::max(1, sp.threads);
    // Near the end the game is decided by who goes out and who gets stuck: play it out.
    const int plies = P.bag_n <= sp.playout_bag ? std::max(sp.plies, 40) : sp.plies;
    const int egk = P.bag_n <= sp.playout_bag ? sp.eg_k : 0;
    RackPrior prior;
    const RackPrior* kp = nullptr;
    if (keep_active(P, sp, opp)) {
      prior.build(P.unseen, P.opp_n, keep_odds(sp.keep).data());
      kp = &prior;
    }
    std::vector<std::unique_ptr<MoveGen>> gens;
    for (int t = 0; t < nthreads; ++t) gens.emplace_back(new MoveGen(lex_, lt_));
    // Each candidate's board after it is played, set up once for all iterations.
    std::vector<Board> starts(R.cands.size(), P.board);
    for (size_t i = 0; i < R.cands.size(); ++i)
      if (R.cands[i].move.type == MT_PLACE) starts[i].place(*lex_, R.cands[i].move);
    std::atomic<long> positions{0};
    int iters = 0;
    int last_prune_check = 0;
    double per_iter = 0;  // seconds per iteration in the last batch
    while (true) {
      int n_active = 0;
      for (auto& c : R.cands) n_active += c.active;
      if (n_active <= 1) break;
      if (iters >= sp.max_iterations) break;
      if (now_s() - t0 >= sp.time_limit && iters > 0) break;
      // Batches (and pruning checks) grow with the iterations done, so their overhead
      // stays small; the batch also shrinks to the time left, so the clock is respected.
      int batch;
      if (iters > 0) {
        const double left = sp.time_limit - (now_s() - t0);
        batch = std::max(nthreads, std::min(std::max(sp.batch, iters / 16), (int)(left / std::max(1e-7, per_iter)) + 1));
      } else {
        batch = std::max(nthreads, std::min(sp.batch, 2 * nthreads));
      }
      const double t_batch = now_s();
      const int it0 = iters, it1 = std::min(sp.max_iterations, iters + batch);
      for (auto& c : R.cands)
        if (c.active) {
          c.eq.resize(it1);
          c.win.resize(it1);
        }
      std::atomic<int> next{it0};
      auto worker = [&](int tid) {
        MoveGen& gen = *gens[tid];
        while (true) {
          const int k = next.fetch_add(1);
          if (k >= it1) break;
          Deal deal;
          make_deal(P, seed, k, opp, kp, deal);
          for (auto& c : R.cands) {
            if (!c.active) continue;
            float e, w;
            const int done = simulate(P, starts[&c - R.cands.data()], deal, c.move, gen, plies,
                                      seed ^ (u64)k * 0x9E3779B97F4A7C15ULL, e, w, egk);
            c.eq[k] = e;
            c.win[k] = w;
            positions.fetch_add(done, std::memory_order_relaxed);
          }
        }
      };
      if (nthreads == 1) worker(0);
      else {
        std::vector<std::thread> th;
        for (int t = 0; t < nthreads; ++t) th.emplace_back(worker, t);
        for (auto& x : th) x.join();
      }
      per_iter = (now_s() - t_batch) / std::max(1, it1 - it0);
      iters = it1;
      for (auto& c : R.cands)
        if (c.active) c.n = iters;
      if (iters >= sp.min_iterations && iters - last_prune_check >= std::max(sp.batch, iters / 16)) {
        last_prune_check = iters;
        prune(R, sp);
      }
      if (progress) progress(R);
    }
    R.iterations = iters;
    SimParams rp = sp;
    if (P.bag_n <= sp.playout_bag) rp.shrink_tau *= 2.5;  // static equity is a weaker guide near the end
    rank(R, rp);
    R.seconds = now_s() - t0;
    R.positions = positions.load();
    return R;
  }

  // The ranking `run` ends with, applied to a snapshot taken during the search (telemetry:
  // how the choice changes with more work).
  static void rank_snapshot(SimResult& R, const Position& P, const SimParams& sp) {
    SimParams rp = sp;
    if (P.bag_n <= sp.playout_bag) rp.shrink_tau *= 2.5;
    rank(R, rp);
  }

  // Whether the opponent's rack is drawn from the keep prior in this position.
  static bool keep_active(const Position& P, const SimParams& sp, const OppModel* opp) {
    return sp.keep > 0 && P.bag_n > 0 && P.bag_n <= sp.keep_bag && P.opp_n > 0 && (!opp || opp->empty());
  }

  // Odds of each letter in the keep prior: exp(keep * one-tile leave value).
  std::array<double, NLET> keep_odds(double keep) const {
    std::array<double, NLET> w;
    for (int L = 0; L < NLET; ++L) {
      Rack one;
      one.add(L);
      const double v = lt_ ? (double)lt_->value(one) : 0.0;
      w[L] = std::exp(std::max(-8.0, std::min(8.0, keep * v)));
    }
    return w;
  }

 private:
  const Lexicon* lex_;
  const LeaveTable* lt_;
  const WinModel* wm_;

  struct Deal {
    u8 opp[RACK_SIZE];
    int opp_n = 0;
    u8 bag[TOTAL_TILES];
    int bag_n = 0;
  };

  static void make_deal(const Position& P, u64 seed, int k, const OppModel* opp, const RackPrior* rp, Deal& D) {
    Rng rng(mix64(seed + (u64)k * 0xD1B54A32D192ED03ULL));
    Rack pool = P.unseen;
    D.opp_n = 0;
    if (rp) {
      Rack r;
      rp->sample(rng, r);
      for (int L = 0; L < NLET; ++L)
        for (int j = 0; j < r.c[L]; ++j) D.opp[D.opp_n++] = (u8)L;
      pool.sub_all(r);
    } else if (opp && !opp->empty() && P.bag_n > 0) {
      const Rack& leave = opp->sample(rng);
      if (pool.contains(leave) && leave.n <= P.opp_n) {
        for (int L = 0; L < NLET; ++L)
          for (int j = 0; j < leave.c[L]; ++j) D.opp[D.opp_n++] = (u8)L;
        pool.sub_all(leave);
      }
    }
    u8 rest[TOTAL_TILES];
    int rn = 0;
    for (int L = 0; L < NLET; ++L)
      for (int j = 0; j < pool.c[L]; ++j) rest[rn++] = (u8)L;
    for (int i = rn - 1; i > 0; --i) std::swap(rest[i], rest[rng.below((u32)i + 1)]);
    int p = 0;
    while (D.opp_n < P.opp_n && p < rn) D.opp[D.opp_n++] = rest[p++];
    D.bag_n = 0;
    while (p < rn) D.bag[D.bag_n++] = rest[p++];
  }

  // Returns the number of plies played (including the candidate).
  // `start` is P.board with the candidate already placed on it.
  int simulate(const Position& P, const Board& start, const Deal& D, const Move& cand, MoveGen& gen, int plies, u64 salt,
               float& out_eq, float& out_win, int egk = 0) const {
    Board b = start;
    Rack rk[2];
    rk[0] = P.rack;
    for (int i = 0; i < D.opp_n; ++i) rk[1].add(D.opp[i]);
    u8 bag[TOTAL_TILES];
    int bn = D.bag_n, bp = 0;
    std::memcpy(bag, D.bag, (size_t)bn);
    int sc[2] = {P.my_score, P.opp_score};
    float last_leave[2] = {0.f, 0.f};
    int zeros = P.zeros;
    bool over = false;
    Rng rng(salt ^ cand.hash());

    // place_on_board: false when the board already has the play, or is never read again
    auto play = [&](int side, const Move& m, bool place_on_board) {
      Rack& r = rk[side];
      const int bag_before = bn - bp;
      if (m.type == MT_PLACE) {
        if (place_on_board) b.place(*lex_, m);
        r.sub_all(m.used());
        last_leave[side] = bag_before > 0 ? lt_->value(r) : 0.f;
        sc[side] += m.score;
        for (int k = 0; k < m.ntiles && bp < bn; ++k) r.add(bag[bp++]);
        if (r.n == 0 && bp >= bn) {
          const int v = rk[1 - side].face();
          sc[side] += v;
          sc[1 - side] -= v;
          over = true;
        }
      } else if (m.type == MT_EXCHANGE) {
        const Rack out = m.used();
        r.sub_all(out);
        last_leave[side] = lt_->value(r);
        for (int k = 0; k < m.len && bp < bn; ++k) r.add(bag[bp++]);
        // return the exchanged tiles to random places in the remaining bag
        for (int L = 0; L < NLET; ++L)
          for (int j = 0; j < out.c[L]; ++j) {
            // bp > 0 here because at least len tiles were drawn
            bag[--bp] = (u8)L;
            const int span = bn - bp;
            std::swap(bag[bp], bag[bp + (int)rng.below((u32)span)]);
          }
      } else {
        last_leave[side] = 0.f;
      }
      zeros = (m.type == MT_PLACE && m.score != 0) ? 0 : zeros + 1;
      if (!over && zeros >= 6) {
        sc[0] -= rk[0].face();
        sc[1] -= rk[1].face();
        over = true;
      }
    };

    play(0, cand, false);
    int side = 1;
    int played = 1;
    for (int ply = 0; ply < plies && !over; ++ply, ++played) {
      EvalCtx ctx;
      ctx.bag = bn - bp;
      ctx.opp_face = rk[1 - side].face();
      ctx.allow_exchange = ctx.bag >= RACK_SIZE;
      const Move m = egk > 0 && ctx.bag == 0 ? endgame_pick(b, rk[side], rk[1 - side], zeros, gen, egk)
                                             : gen.generate_best(b, rk[side], ctx);
      play(side, m, ply + 1 < plies);
      side = 1 - side;
    }
    const int spread = sc[0] - sc[1];
    if (over) {
      out_eq = (float)(spread - P.spread());
      out_win = spread > 0 ? 1.f : (spread < 0 ? 0.f : 0.5f);
      return played;
    }
    out_eq = (float)(spread - P.spread()) + last_leave[0] - last_leave[1];
    // `side` is now the player to move.
    const int unseen_for_side = (bn - bp) + rk[1 - side].n;
    float s_side = (float)(side == 0 ? spread : -spread) + last_leave[side] - last_leave[1 - side];
    const float w = wm_->win(s_side, unseen_for_side);
    out_win = side == 0 ? w : 1.f - w;
    return played;
  }

  // The endgame in a play-out (bag empty, both racks known), spread for the side to move
  // when both sides play their static best (a play that goes out first) to the end.
  int greedy_finish(Board b, const Rack& me, const Rack& opp, int zeros, MoveGen& gen) const {
    Rack r[2] = {me, opp};
    int side = 0, spread = 0;
    for (int turn = 0; turn < 30 && zeros < 6; ++turn) {  // six scoreless turns end the game at once
      EvalCtx ctx;
      ctx.bag = 0;
      ctx.opp_face = r[1 - side].face();
      ctx.allow_exchange = false;
      const Move m = gen.generate_best(b, r[side], ctx);
      const int sign = side == 0 ? 1 : -1;
      if (m.type == MT_PLACE) {
        b.place(*lex_, m);
        r[side].sub_all(m.used());
        spread += sign * m.score;
        if (r[side].n == 0) return spread + sign * 2 * r[1 - side].face();
      }
      zeros = (m.type == MT_PLACE && m.score != 0) ? 0 : zeros + 1;
      if (zeros >= 6) break;
      side = 1 - side;
    }
    return spread - r[0].face() + r[1].face();
  }

  // With the bag empty: of the side's k best moves by static equity, the one that leaves it
  // best off when both sides then play greedily to the end.  This sees one move ahead of
  // the static player: setting up an out in two, or blocking the opponent's out.
  Move endgame_pick(const Board& b, const Rack& me, const Rack& opp, int zeros, MoveGen& gen, int k) const {
    thread_local std::vector<Move> all;
    all.clear();
    EvalCtx ctx;
    ctx.bag = 0;
    ctx.opp_face = opp.face();
    ctx.allow_exchange = false;
    gen.generate_all(b, me, ctx, all);
    if (all.empty()) return Move();
    const int n = std::min<int>(k, (int)all.size());
    std::partial_sort(all.begin(), all.begin() + n, all.end(),
                      [](const Move& x, const Move& y) { return x.equity > y.equity; });
    int best = 0, best_v = -1000000;
    for (int i = 0; i < n; ++i) {
      const Move& m = all[i];
      int v;
      if (m.type == MT_PLACE) {
        Board nb = b;
        nb.place(*lex_, m);
        Rack left = me;
        left.sub_all(m.used());
        v = left.n == 0 ? m.score + 2 * opp.face()
                        : m.score - greedy_finish(nb, opp, left, m.score != 0 ? 0 : zeros + 1, gen);
      } else {
        v = zeros + 1 >= 6 ? opp.face() - me.face() : -greedy_finish(b, opp, me, zeros + 1, gen);
      }
      if (v > best_v) {
        best_v = v;
        best = i;
      }
    }
    return all[best];
  }

  // Final ranking.  The simulation measures each candidate's advantage over the most
  // simulated candidate (paired, so luck cancels); that measurement is combined with
  // the static evaluator's opinion as a Bayesian prior:
  //     posterior = (sim_diff / se^2 + static_diff / prior_var) / (1/se^2 + 1/prior_var)
  // With few iterations the static ranking dominates, with many the simulation does.
  static void rank(SimResult& R, const SimParams& sp) {
    auto obj = [&](const SimCandidate& c, int k) {
      return sp.win_objective ? (double)c.win[k] + sp.equity_tiebreak * c.eq[k] : (double)c.eq[k];
    };
    int ref = 0;
    for (size_t i = 1; i < R.cands.size(); ++i) {
      const auto& a = R.cands[i];
      const auto& b = R.cands[ref];
      if (a.n > b.n || (a.n == b.n && a.objective(sp.win_objective, sp.equity_tiebreak) >
                                          b.objective(sp.win_objective, sp.equity_tiebreak)))
        ref = (int)i;
    }
    const SimCandidate& Rc = R.cands[ref];
    // Exchange rate between equity and the objective (win probability per point).
    double scale = 1.0;
    if (sp.win_objective) {
      double sxy = 0, sxx = 0;
      for (const auto& c : R.cands) {
        const int n = std::min(c.n, Rc.n);
        if (n < 8 || &c == &Rc) continue;
        const double x = c.mean_eq() - Rc.mean_eq(), y = c.mean_win() - Rc.mean_win();
        sxy += n * x * y;
        sxx += n * x * x;
      }
      const double slope = sxx > 0 ? std::max(0.0, std::min(0.03, sxy / sxx)) : 0.005;
      scale = slope + sp.equity_tiebreak;
    }
    for (auto& c : R.cands) {
      const double prior_mean = scale * ((double)c.static_eq - (double)Rc.static_eq);
      const double prior_var = 2.0 * std::pow(scale * sp.shrink_tau, 2);
      if (&c == &Rc) {
        c.post = 0;
        c.prior_w = NAN;
        c.sim_diff = 0;
        continue;
      }
      const int n = std::min(c.n, Rc.n);
      c.prior_w = n < 2 ? 1.0 : (sp.shrink_tau <= 0 ? 0.0 : NAN);
      c.sim_diff = NAN;
      if (n < 2 || sp.shrink_tau <= 0) {
        c.post = n < 2 ? prior_mean : c.objective(sp.win_objective, sp.equity_tiebreak) -
                                          Rc.objective(sp.win_objective, sp.equity_tiebreak);
        continue;
      }
      double sum = 0, sum2 = 0;
      for (int k = 0; k < n; ++k) {
        const double d = obj(c, k) - obj(Rc, k);
        sum += d;
        sum2 += d * d;
      }
      const double m = sum / n;
      const double se2 = std::max(1e-12, (sum2 / n - m * m) / (n - 1));
      c.post = (m / se2 + prior_mean / prior_var) / (1.0 / se2 + 1.0 / prior_var);
      c.prior_w = (1.0 / prior_var) / (1.0 / se2 + 1.0 / prior_var);
      c.sim_diff = m;
    }
    std::stable_sort(R.cands.begin(), R.cands.end(), [](const SimCandidate& a, const SimCandidate& b) { return a.post > b.post; });
  }

  static void prune(SimResult& R, const SimParams& sp) {
    int best = -1;
    double best_obj = -1e300;
    for (size_t i = 0; i < R.cands.size(); ++i) {
      const auto& c = R.cands[i];
      if (!c.active) continue;
      const double o = c.objective(sp.win_objective, sp.equity_tiebreak);
      if (o > best_obj) {
        best_obj = o;
        best = (int)i;
      }
    }
    if (best < 0) return;
    const auto& B = R.cands[best];
    int survivors = 0, closest = -1;
    double closest_z = 1e300;
    for (size_t i = 0; i < R.cands.size(); ++i) {
      auto& c = R.cands[i];
      if (!c.active || (int)i == best) continue;
      const int n = std::min(c.n, B.n);
      if (n < 2) {
        ++survivors;
        continue;
      }
      double s = 0, s2 = 0;
      for (int k = 0; k < n; ++k) {
        const double ob = sp.win_objective ? B.win[k] + sp.equity_tiebreak * B.eq[k] : B.eq[k];
        const double oc = sp.win_objective ? c.win[k] + sp.equity_tiebreak * c.eq[k] : c.eq[k];
        const double d = ob - oc;
        s += d;
        s2 += d * d;
      }
      const double m = s / n;
      const double var = std::max(1e-12, (s2 / n - m * m) * n / (n - 1));
      const double se = std::sqrt(var / n);
      if (m - sp.prune_z * se > 0) {
        c.active = false;
        if (m / se < closest_z) {
          closest_z = m / se;
          closest = (int)i;
        }
      } else {
        ++survivors;
      }
    }
    // Keep the closest challenger rather than stop early.
    if (survivors == 0 && closest >= 0) R.cands[closest].active = true;
  }
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §12  Endgame solver
// =====================================================================================
//
//  With the bag empty both racks are known (the opponent holds exactly the unseen
//  tiles), so the endgame is a finite two-player zero-sum game with perfect
//  information.  Negamax with alpha-beta, iterative deepening, principal-variation
//  search and a transposition table solves it.  Values are "spread gained from here to
//  the end of the game" for the side to move.  A result is marked exact when every
//  line in the searched tree reached the end of the game (no depth cut-off was used).
//
//  Rules modelled: playing out ends the game (+2x opponent rack); two consecutive
//  passes, or six scoreless turns in a row, end it (each side loses its own rack).

struct EndgameParams {
  double time_limit = 10.0;
  int max_depth = 40;
  int tt_bits = 20;  // 2^20 entries x 40 bytes = 40 MB
  int threads = 1;   // Lazy SMP: helpers share the transposition table
  bool verbose = false;
};

struct EndgameResult {
  Move best;
  int value = 0;         // spread gained from here to the end, for the side to move
  bool solved = false;   // proven exact
  int depth = -1;        // deepest completed iteration (0: greedy play-out estimate only)
  long nodes = 0;
  double seconds = 0;
  std::vector<Move> pv;
  std::vector<std::pair<Move, int>> root;  // root moves with their (last completed) values
};

class EndgameSolver {
 public:
  EndgameSolver(const Lexicon* lex, int tt_bits = 20) : lex_(lex) {
    Rng r(0xE9D6A5B4C3F2E1D0ULL);
    for (int s = 0; s < NSQ; ++s)
      for (int c = 0; c < 64; ++c) zsq_[s][c] = r.next();
    for (int p = 0; p < 2; ++p)
      for (int L = 0; L < NLET; ++L)
        for (int c = 0; c < 16; ++c) zrack_[p][L][c] = r.next();
    zside_ = r.next();
    for (int i = 0; i < 8; ++i) zpass_[i] = r.next();
    for (int i = 0; i < 8; ++i) zzero_[i] = r.next();
    default_bits_ = tt_bits;  // the table is allocated on first use
  }

  void resize_tt(int bits) {
    if (bits == tt_bits_ && !tt_.empty()) return;
    tt_bits_ = bits;
    tt_.assign((size_t)1 << bits, TTE());
    tt_mask_ = ((u64)1 << bits) - 1;
  }

  // Test hook: walks random lines of play from (b, racks) and checks that the move
  // source used inside the search (root plays still valid + plays around new tiles)
  // yields exactly the legal plays found by full generation.  Returns mismatches.
  int check_move_source(const Board& b0, const Rack& me, const Rack& opp, Rng& rng, int lines) {
    const Rack r0[2] = {me, opp};
    build_root_moves(b0, r0);
    Worker w(lex_);
    MoveGen full(lex_, nullptr);
    int bad = 0;
    auto key = [](const Move& m) {
      std::string k = std::to_string(m.type) + ":" + std::to_string(m.row) + "," + std::to_string(m.col) + "," +
                      std::to_string(m.dir) + ":" + std::to_string(m.score) + ":";
      for (int i = 0; i < m.len; ++i) k += std::to_string(m.tiles[i]) + ".";
      return k;
    };
    for (int line = 0; line < lines; ++line) {
      Board b = b0;
      Rack r[2] = {me, opp};
      SqSet fresh;
      int side = 0;
      for (int ply = 0; ply < 8; ++ply) {
        std::vector<Move> a, c;
        gen_moves(w, b, r[side], r[side ^ 1], side, fresh, a);
        EvalCtx ctx;
        ctx.bag = 0;
        ctx.opp_face = r[side ^ 1].face();
        ctx.allow_exchange = false;
        ctx.use_leaves = false;
        full.generate_all(b, r[side], ctx, c);
        std::multiset<std::string> ka, kc;
        for (const auto& m : a) ka.insert(key(m));
        for (const auto& m : c) kc.insert(key(m));
        if (ka != kc) ++bad;
        // follow a random play
        std::vector<Move> plays;
        for (const auto& m : c)
          if (m.type == MT_PLACE) plays.push_back(m);
        if (plays.empty()) break;
        const Move m = plays[rng.below((u32)plays.size())];
        b.place(*lex_, m);
        for (int i = 0; i < m.len; ++i)
          if (m.tiles[i]) fresh.add(m.square(i));
        r[side].sub_all(m.used());
        if (!r[side].n) break;
        side ^= 1;
      }
    }
    root_moves_[0].clear();
    root_moves_[1].clear();
    return bad;
  }

  // `me` is the side to move.
  EndgameResult solve(const Board& b, const Rack& me, const Rack& opp, int zeros, const EndgameParams& p) {
    EndgameResult R;
    const double t0 = now_s();
    deadline_ = t0 + p.time_limit;
    stop_.store(false);
    resize_tt(p.tt_bits > 0 ? p.tt_bits : default_bits_);
    salt_ = mix64(++solves_ * 0x9E3779B97F4A7C15ULL);  // entries from earlier solves no longer match
    gen_ = (u8)solves_;
    {
      const Rack rr[2] = {me, opp};
      build_root_moves(b, rr);
    }
    const int nthreads = std::max(1, p.threads);
    std::vector<std::unique_ptr<Worker>> workers;
    for (int i = 0; i < nthreads; ++i) {
      workers.emplace_back(new Worker(lex_));
      workers.back()->id = i;
    }
    std::vector<std::thread> helpers;
    for (int i = 1; i < nthreads; ++i)
      helpers.emplace_back([&, i]() {
        EndgameResult dummy;
        root_search(*workers[i], b, me, opp, zeros, p, dummy, t0);
      });
    root_search(*workers[0], b, me, opp, zeros, p, R, t0);
    stop_.store(true);
    for (auto& h : helpers) h.join();
    R.nodes = 0;
    for (auto& w : workers) R.nodes += w->nodes;
    R.seconds = now_s() - t0;
    Rack r[2] = {me, opp};
    R.pv = principal_variation(b, r, zeros, R.best);
    return R;
  }

 private:
  struct TTE {
    u64 check = 0;  // key ^ digest(data): torn concurrent writes fail verification
    i16 value = 0;
    u8 depth = 0;
    u8 flag = 0;  // 0 empty, 1 exact, 2 lower bound, 3 upper bound
    u8 mtype = 255, mrow = 0, mcol = 0, mdir = 0, mlen = 0, mntiles = 0;
    i16 mscore = 0;
    u8 mtiles[N] = {0};
    u8 gen = 0;  // solve that wrote it (replacement policy only)
  };
  enum { F_EXACT = 1, F_LOWER = 2, F_UPPER = 3 };
  static constexpr u8 DEPTH_EXACT = 255;

  struct Worker {
    MoveGen gen;
    std::vector<Move> stack[64];
    std::vector<Move> tmp;
    u32 killer[64][2];
    long nodes = 0;
    u32 ticks = 0;  // clock checks are spaced by these (nodes and play-out steps)
    int id = 0;
    explicit Worker(const Lexicon* l) : gen(l, nullptr) { std::memset(killer, 0, sizeof killer); }
  };

  // Squares that received a tile since the root (a 225-bit set).
  struct SqSet {
    u64 w[4] = {0, 0, 0, 0};
    void add(int s) { w[s >> 6] |= 1ull << (s & 63); }
    bool has(int s) const { return (w[s >> 6] >> (s & 63)) & 1u; }
    bool any() const { return (w[0] | w[1] | w[2] | w[3]) != 0; }
    bool meets(const SqSet& o) const { return ((w[0] & o.w[0]) | (w[1] & o.w[1]) | (w[2] & o.w[2]) | (w[3] & o.w[3])) != 0; }
  };
  // Every play of a side on the root board, generated once.  A root play stays legal
  // with the same score as long as none of its `sens` squares (its own squares and
  // the first empty square beyond each word it forms) has received a tile; plays that
  // touch new tiles are generated afresh around them.
  struct RootMove {
    Move m;
    SqSet sens;
    u8 need[RACK_SIZE];  // rack codes of the tiles it uses
    u8 nneed;
  };
  std::vector<RootMove> root_moves_[2];

  static void add_sensitive(const Board& b, const Move& m, SqSet& out) {
    const int step = m.dir == 0 ? 1 : N, pstep = m.dir == 0 ? N : 1;
    auto beyond = [&](int s, int st, bool forward) {
      // first empty square past the run of tiles starting next to s
      int r = s / N, c = s % N;
      const int dr = st == N ? 1 : 0, dc = st == 1 ? 1 : 0;
      while (true) {
        r += forward ? dr : -dr;
        c += forward ? dc : -dc;
        if (r < 0 || c < 0 || r >= N || c >= N) return;
        if (!b.sq[r * N + c]) {
          out.add(r * N + c);
          return;
        }
      }
    };
    beyond(m.square(0), step, false);
    beyond(m.square(m.len - 1), step, true);
    for (int i = 0; i < m.len; ++i) {
      if (!m.tiles[i]) continue;
      const int sq = m.square(i);
      out.add(sq);
      beyond(sq, pstep, false);
      beyond(sq, pstep, true);
    }
  }

  // Does play m (on board b) form a word with a tile placed since the root?
  static bool touches_new(const Board& b, const Move& m, const SqSet& fresh) {
    const int pstep = m.dir == 0 ? N : 1;
    for (int i = 0; i < m.len; ++i) {
      const int sq = m.square(i);
      if (!m.tiles[i]) {
        if (fresh.has(sq)) return true;
        continue;
      }
      const int r = sq / N, c = sq % N;
      for (int dir = -1; dir <= 1; dir += 2) {
        int rr = r, cc = c;
        while (true) {
          if (pstep == N) rr += dir;
          else cc += dir;
          if (rr < 0 || cc < 0 || rr >= N || cc >= N) break;
          const int t = rr * N + cc;
          if (!b.sq[t]) break;
          if (fresh.has(t)) return true;
        }
      }
    }
    return false;
  }

  const Lexicon* lex_;
  std::vector<TTE> tt_;
  u64 tt_mask_ = 0;
  int tt_bits_ = 0;
  u64 zsq_[NSQ][64];
  u64 zrack_[2][NLET][16];
  u64 zside_;
  u64 zpass_[8];
  u64 zzero_[8];
  double deadline_ = 0;
  std::atomic<bool> stop_{false};
  u64 salt_ = 0, solves_ = 0;
  u8 gen_ = 0;
  int default_bits_ = 20;

  static u64 digest(const TTE& e) {
    u64 h = mix64(((u64)(u16)e.value << 48) ^ ((u64)e.depth << 40) ^ ((u64)e.flag << 32) ^ ((u64)e.mtype << 24) ^
                  ((u64)e.mrow << 16) ^ ((u64)e.mcol << 8) ^ e.mdir);
    h = mix64(h ^ ((u64)e.mlen << 40) ^ ((u64)e.mntiles << 32) ^ (u64)(u16)e.mscore);
    u64 t1 = 0, t2 = 0;
    std::memcpy(&t1, e.mtiles, 8);
    std::memcpy(&t2, e.mtiles + 8, 7);
    return mix64(h ^ t1) ^ mix64(t2 + 0x51ED270B27);
  }
  bool probe(u64 key, TTE& out) const {
    out = tt_[key & tt_mask_];
    return out.flag != 0 && (out.check ^ digest(out)) == key;
  }
  void store(u64 key, int value, u8 depth, u8 flag, const Move* m) {
    TTE& slot = tt_[key & tt_mask_];
    TTE old = slot;
    const bool old_ok = old.flag != 0 && (old.check ^ digest(old)) == key;
    if (old_ok && old.depth == DEPTH_EXACT && depth != DEPTH_EXACT) return;  // keep proven results
    // Play-out estimates at the leaves are plentiful: they never evict a searched
    // entry (a bound or a best move) written during this solve.
    if (depth == 0 && old.flag != 0 && old.gen == gen_ && old.depth > 0) return;
    TTE e;
    e.value = (i16)std::max(-32000, std::min(32000, value));
    e.depth = depth;
    e.flag = flag;
    if (m) {
      e.mtype = m->type;
      e.mrow = m->row;
      e.mcol = m->col;
      e.mdir = m->dir;
      e.mlen = m->len;
      e.mntiles = m->ntiles;
      e.mscore = m->score;
      std::memcpy(e.mtiles, m->tiles, N);
    }
    e.gen = gen_;
    e.check = key ^ digest(e);
    slot = e;
  }
  static bool unpack(const TTE& e, Move& m) {
    if (e.mtype == 255) return false;
    m = Move();
    m.type = e.mtype;
    m.row = e.mrow;
    m.col = e.mcol;
    m.dir = e.mdir;
    m.len = e.mlen;
    m.ntiles = e.mntiles;
    m.score = e.mscore;
    std::memcpy(m.tiles, e.mtiles, N);
    return true;
  }
  static bool quick_legal(const Board& b, const Rack& mine, const Move& m) {
    if (m.type == MT_PASS) return true;
    if (m.type != MT_PLACE || m.len == 0 || m.len > N) return false;
    if (!mine.contains(m.used())) return false;
    for (int i = 0; i < m.len; ++i) {
      const int s = m.square(i);
      if (s < 0 || s >= NSQ) return false;
      if (m.tiles[i] ? b.sq[s] != 0 : b.sq[s] == 0) return false;
    }
    return true;
  }

  static int tile_code(u8 t) { return (t & 31) + ((t & BLANK_BIT) ? 26 : 0); }
  u64 board_hash(const Board& b) const {
    u64 h = 0;
    for (int s = 0; s < NSQ; ++s)
      if (b.sq[s]) h ^= zsq_[s][tile_code(b.sq[s])];
    return h;
  }
  u64 full_key(u64 bh, const Rack* r, int side, int passes, int zeros) const {
    u64 k = salt_ ^ bh ^ (side ? zside_ : 0) ^ zpass_[passes & 7] ^ zzero_[std::min(zeros, 7)];
    for (int p = 0; p < 2; ++p) {
      const int q = side ^ p;  // racks relative to the side to move
      for (int L = 0; L < NLET; ++L)
        if (r[q].c[L]) k ^= zrack_[p][L][r[q].c[L] & 15];
    }
    return k;
  }

  // Search order: going out first, then by score plus the value of tiles unloaded.
  static void order(Worker& w, std::vector<Move>& mv, int rack_n, int opp_face, int ply) {
    for (auto& m : mv) {
      float key;
      if (m.type == MT_PLACE && m.ntiles == rack_n) key = 1e8f + (float)m.score + 2.f * (float)opp_face;
      else if (m.type == MT_PASS) key = -1e8f;
      else {
        key = (float)m.score + 2.0f * (float)m.played_face() + 1.5f * (float)m.ntiles;
        if (ply >= 0 && ply < 64) {
          const u32 h = m.hash();
          if (h == w.killer[ply][0] || h == w.killer[ply][1]) key += 1e7f;
        }
      }
      m.equity = key;
    }
    std::stable_sort(mv.begin(), mv.end(), [](const Move& a, const Move& b) { return a.equity > b.equity; });
  }

  // Leaf estimate at the depth limit: both sides in turn play their highest-scoring
  // play (going out first) until the game ends; returns the spread gained by `side`.
  // Once the search is stopped the value is meaningless (callers check stop_).
  int greedy_playout(Worker& w, const Board& b0, const Rack* r0, int side0, int passes, int zeros, const SqSet& fresh0,
                     int ply) {
    Board b = b0;
    Rack r[2] = {r0[0], r0[1]};
    SqSet fresh = fresh0;
    int side = side0, spread = 0;
    for (int step = 0; step < 12 && ply + step < 63; ++step) {
      if (out_of_time(w)) return 0;
      const Rack& mine = r[side];
      const Rack& theirs = r[side ^ 1];
      const int sign = side == side0 ? 1 : -1;
      std::vector<Move>& mv = w.stack[ply + step];
      gen_moves(w, b, mine, theirs, side, fresh, mv);
      const Move* best = nullptr;
      int best_key = -1000000;
      for (const Move& m : mv) {
        if (m.type != MT_PLACE) continue;
        const int k = m.ntiles == mine.n ? 100000 + m.score : m.score;
        if (k > best_key) {
          best_key = k;
          best = &m;
        }
      }
      if (!best) {  // must pass
        ++passes;
        ++zeros;
        if (passes >= 2 || zeros >= 6) return spread + sign * (theirs.face() - mine.face());
        side ^= 1;
        continue;
      }
      passes = 0;
      const Move m = *best;
      if (m.ntiles == mine.n) return spread + sign * (m.score + 2 * theirs.face());
      spread += sign * m.score;
      zeros = m.score == 0 ? zeros + 1 : 0;
      b.place(*lex_, m);
      for (int i = 0; i < m.len; ++i)
        if (m.tiles[i]) fresh.add(m.square(i));
      r[side].sub_all(m.used());
      side ^= 1;
      if (zeros >= 6) break;  // six scoreless turns: the game ends as below
    }
    // Game over or not finished: each side is charged its remaining tiles.
    return spread + (side == side0 ? 1 : -1) * (r[side ^ 1].face() - r[side].face());
  }

  static int leaf_eval(const Rack& mine, const Rack& theirs) {
    // Cheap guess: whoever keeps more face value is worse off.
    return theirs.face() - mine.face();
  }

  bool out_of_time(Worker& w) {
    if (stop_.load(std::memory_order_relaxed)) return true;
    if ((++w.ticks & 31) == 0 && now_s() > deadline_) {
      stop_.store(true);
      return true;
    }
    return false;
  }

  void gen_moves(Worker& w, const Board& b, const Rack& mine, const Rack& theirs, int side, const SqSet& fresh,
                 std::vector<Move>& mv) {
    mv.clear();
    EvalCtx ctx;
    ctx.bag = 0;
    ctx.opp_face = theirs.face();
    ctx.allow_exchange = false;
    ctx.use_leaves = false;
    const std::vector<RootMove>& rl = root_moves_[side];
    if (rl.empty() && !fresh.any()) {
      w.gen.generate_all(b, mine, ctx, mv);
      return;
    }
    // 1. Root plays still available: squares untouched and tiles still on the rack.
    for (const RootMove& rm : rl) {
      if (rm.sens.meets(fresh)) continue;
      int8_t left[NLET];
      std::memcpy(left, mine.c, NLET);
      bool ok = true;
      for (int i = 0; i < rm.nneed && ok; ++i) ok = --left[rm.need[i]] >= 0;
      if (ok) mv.push_back(rm.m);
    }
    // 2. Plays that form words with tiles placed since the root.
    if (fresh.any()) {
      bool anchors[NSQ];
      std::memset(anchors, 0, sizeof anchors);
      for (int sq = 0; sq < NSQ; ++sq) {
        if (!fresh.has(sq)) continue;
        // ends of the horizontal and vertical runs of tiles through sq
        const int r = sq / N, c = sq % N;
        int c0 = c, c1 = c, r0 = r, r1 = r;
        while (c0 > 0 && b.sq[r * N + c0 - 1]) --c0;
        while (c1 < N - 1 && b.sq[r * N + c1 + 1]) ++c1;
        while (r0 > 0 && b.sq[(r0 - 1) * N + c]) --r0;
        while (r1 < N - 1 && b.sq[(r1 + 1) * N + c]) ++r1;
        if (c0 > 0) anchors[r * N + c0 - 1] = true;
        if (c1 < N - 1) anchors[r * N + c1 + 1] = true;
        if (r0 > 0) anchors[(r0 - 1) * N + c] = true;
        if (r1 < N - 1) anchors[(r1 + 1) * N + c] = true;
      }
      w.tmp.clear();
      w.gen.generate_near(b, mine, ctx, anchors, w.tmp);
      for (const Move& m : w.tmp)
        if (touches_new(b, m, fresh)) mv.push_back(m);
    }
    Move pass;
    pass.type = MT_PASS;
    mv.push_back(pass);
  }

  void build_root_moves(const Board& b, const Rack* r) {
    MoveGen gen(lex_, nullptr);
    for (int side = 0; side < 2; ++side) {
      std::vector<Move> all;
      EvalCtx ctx;
      ctx.bag = 0;
      ctx.opp_face = r[side ^ 1].face();
      ctx.allow_exchange = false;
      ctx.use_leaves = false;
      ctx.add_pass = false;
      gen.generate_all(b, r[side], ctx, all);
      root_moves_[side].clear();
      root_moves_[side].reserve(all.size());
      for (const Move& m : all) {
        if (m.type != MT_PLACE) continue;
        RootMove rm;
        rm.m = m;
        add_sensitive(b, m, rm.sens);
        rm.nneed = 0;
        for (int i = 0; i < m.len; ++i)
          if (m.tiles[i]) rm.need[rm.nneed++] = (u8)tile_rack_code(m.tiles[i]);
        root_moves_[side].push_back(rm);
      }
    }
  }

  // Value (for the side to move at `b`) of playing m, i.e. m.score - value(child).
  int child_value(Worker& w, const Board& b, const Rack* r, int side, int passes, int zeros, u64 bh, const SqSet& fresh,
                  const Move& m, int depth, int alpha, int beta, int ply, bool& exact) {
    const Rack& mine = r[side];
    const Rack& theirs = r[side ^ 1];
    if (m.type == MT_PLACE && m.ntiles == mine.n) {
      exact = true;
      return m.score + 2 * theirs.face();
    }
    if (m.type == MT_PASS) {
      bool ex = true;
      const int v = -negamax(w, b, r, side ^ 1, passes + 1, zeros + 1, bh, fresh, depth - 1, -beta, -alpha, ply + 1, ex);
      exact = ex;
      return v;
    }
    Board nb = b;
    nb.place(*lex_, m);
    u64 nbh = bh;
    SqSet nf = fresh;
    for (int i = 0; i < m.len; ++i)
      if (m.tiles[i]) {
        nbh ^= zsq_[m.square(i)][tile_code(m.tiles[i])];
        nf.add(m.square(i));
      }
    Rack nr[2] = {r[0], r[1]};
    nr[side].sub_all(m.used());
    bool ex = true;
    const int v = m.score - negamax(w, nb, nr, side ^ 1, 0, m.score == 0 ? zeros + 1 : 0, nbh, nf, depth - 1, -beta + m.score,
                                    -alpha + m.score, ply + 1, ex);
    exact = ex;
    return v;
  }

  int negamax(Worker& w, const Board& b, const Rack* r, int side, int passes, int zeros, u64 bh, const SqSet& fresh, int depth,
              int alpha, int beta, int ply, bool& exact) {
    ++w.nodes;
    if (out_of_time(w)) {
      exact = false;
      return 0;
    }
    const Rack& mine = r[side];
    const Rack& theirs = r[side ^ 1];
    if (passes >= 2 || zeros >= 6) {
      exact = true;
      return theirs.face() - mine.face();
    }
    const u64 key = full_key(bh, r, side, passes, zeros);
    TTE e;
    Move ttm;
    bool have_tt = false;
    if (probe(key, e)) {
      if (e.depth >= depth) {
        const bool ex = e.depth == DEPTH_EXACT;
        if (e.flag == F_EXACT || (e.flag == F_LOWER && e.value >= beta) || (e.flag == F_UPPER && e.value <= alpha)) {
          exact = ex;
          return e.value;
        }
      }
      have_tt = unpack(e, ttm) && quick_legal(b, mine, ttm);
    }
    if (depth <= 0 || ply >= 60) {
      exact = false;
      if (ply >= 60) return leaf_eval(mine, theirs);
      const int v = greedy_playout(w, b, r, side, passes, zeros, fresh, ply);
      if (!stop_.load(std::memory_order_relaxed)) store(key, v, 0, F_EXACT, nullptr);
      return v;
    }
    const int alpha0 = alpha;
    int best = -100000;
    Move best_move;
    bool have_best = false;
    bool all_exact = true;
    int searched = 0;
    bool cut = false;
    auto consider = [&](const Move& m) {
      bool ex = true;
      int v;
      if (searched == 0) {
        v = child_value(w, b, r, side, passes, zeros, bh, fresh, m, depth, alpha, beta, ply, ex);
      } else {
        v = child_value(w, b, r, side, passes, zeros, bh, fresh, m, depth, alpha, alpha + 1, ply, ex);
        if (!stop_.load(std::memory_order_relaxed) && v > alpha && v < beta) {
          ex = true;
          v = child_value(w, b, r, side, passes, zeros, bh, fresh, m, depth, alpha, beta, ply, ex);
        }
      }
      if (stop_.load(std::memory_order_relaxed)) return;
      ++searched;
      all_exact &= ex;
      if (v > best) {
        best = v;
        best_move = m;
        have_best = true;
      }
      if (v > alpha) alpha = v;
      if (alpha >= beta) {
        cut = true;
        if (m.type == MT_PLACE && ply < 64) {
          const u32 h = m.hash();
          if (w.killer[ply][0] != h) {
            w.killer[ply][1] = w.killer[ply][0];
            w.killer[ply][0] = h;
          }
        }
      }
    };
    if (have_tt) consider(ttm);
    if (!cut && !stop_.load(std::memory_order_relaxed)) {
      std::vector<Move>& mv = w.stack[ply];
      gen_moves(w, b, mine, theirs, side, fresh, mv);
      order(w, mv, mine.n, theirs.face(), ply);
      const u32 th = have_tt ? ttm.hash() : 0;
      for (size_t i = 0; i < mv.size() && !cut; ++i) {
        const Move m = mv[i];  // copy: children reuse deeper stack slots only, but be safe
        if (have_tt && m.hash() == th && m.same_as(ttm)) continue;
        consider(m);
        if (stop_.load(std::memory_order_relaxed)) break;
      }
    }
    if (stop_.load(std::memory_order_relaxed)) {
      exact = false;
      return best > -100000 ? best : 0;
    }
    exact = all_exact;
    const u8 flag = best <= alpha0 ? F_UPPER : (best >= beta ? F_LOWER : F_EXACT);
    store(key, best, all_exact ? DEPTH_EXACT : (u8)std::min(depth, 254), flag, have_best ? &best_move : nullptr);
    return best;
  }

  // Iterative deepening at the root.  Worker 0 reports results; helpers only feed the
  // shared table (odd helpers search one ply deeper to diversify).
  void root_search(Worker& w, const Board& b, const Rack& me, const Rack& opp, int zeros, const EndgameParams& p,
                   EndgameResult& R, double t0) {
    Rack r[2] = {me, opp};
    const u64 bh = board_hash(b);
    const SqSet none;
    std::vector<Move> root;
    gen_moves(w, b, me, opp, 0, none, root);
    order(w, root, me.n, opp.face(), -1);
    if (root.empty()) {
      R.best = Move();
      return;
    }
    std::vector<int> vals(root.size(), 0);
    Move best = root[0];
    int best_val = 0;
    // Depth 0: both sides play greedily to the end.  A first estimate of the value,
    // for when not even one ply of search fits in the time limit.
    if (w.id == 0) {
      const int v = greedy_playout(w, b, r, 0, 0, zeros, none, 0);
      if (!stop_.load()) {
        best_val = v;
        R.depth = 0;
      }
    }
    const int start_depth = 1 + (w.id & 1);
    for (int depth = start_depth; depth <= p.max_depth; ++depth) {
      int alpha = -100000;
      const int beta = 100000;
      bool all_exact = true;
      int it_best = -1, it_val = -100000;
      std::vector<int> nv(root.size(), -100000);
      bool finished = true;
      for (size_t i = 0; i < root.size(); ++i) {
        const Move& m = root[i];
        bool ex = true;
        int v;
        if (i == 0) {
          v = child_value(w, b, r, 0, 0, zeros, bh, none, m, depth, alpha, beta, 0, ex);
        } else {
          v = child_value(w, b, r, 0, 0, zeros, bh, none, m, depth, alpha, alpha + 1, 0, ex);
          if (!stop_.load() && v > alpha) {
            ex = true;
            v = child_value(w, b, r, 0, 0, zeros, bh, none, m, depth, alpha, beta, 0, ex);
          }
        }
        if (stop_.load()) {
          finished = false;
          break;
        }
        all_exact &= ex;
        nv[i] = v;
        if (v > it_val) {
          it_val = v;
          it_best = (int)i;
        }
        if (v > alpha) alpha = v;
      }
      if (!finished) {
        // A move that beat the previous best in the unfinished iteration is trusted.
        if (it_best > 0) {
          best = root[it_best];
          best_val = it_val;
        }
        break;
      }
      best = root[it_best];
      best_val = it_val;
      if (w.id == 0) R.depth = depth;
      std::vector<size_t> idx(root.size());
      std::iota(idx.begin(), idx.end(), 0);
      std::stable_sort(idx.begin(), idx.end(), [&](size_t x, size_t y) { return nv[x] > nv[y]; });
      std::vector<Move> nr;
      std::vector<int> nvals;
      for (size_t k : idx) {
        nr.push_back(root[k]);
        nvals.push_back(nv[k]);
      }
      root.swap(nr);
      vals.swap(nvals);
      if (p.verbose && w.id == 0)
        std::cerr << fmt("  endgame depth %2d: best %-22s value %+4d  %.2fs%s\n", depth, move_str(b, best).c_str(), best_val,
                         now_s() - t0, all_exact ? "  (exact)" : "");
      if (all_exact) {
        if (w.id == 0) R.solved = true;
        break;
      }
      if (now_s() > deadline_ || stop_.load()) break;
    }
    if (w.id == 0) {
      R.best = best;
      R.value = best_val;
      R.root.clear();
      for (size_t i = 0; i < root.size() && i < vals.size(); ++i) R.root.push_back({root[i], vals[i]});
    }
  }

  std::vector<Move> principal_variation(const Board& b0, const Rack* r0, int zeros0, const Move& first) {
    std::vector<Move> pv;
    Board b = b0;
    Rack r[2] = {r0[0], r0[1]};
    int side = 0, passes = 0, zeros = zeros0;
    Move m = first;
    for (int ply = 0; ply < 30; ++ply) {
      pv.push_back(m);
      if (m.type == MT_PLACE) {
        const bool out = m.ntiles == r[side].n;
        b.place(*lex_, m);
        r[side].sub_all(m.used());
        passes = 0;
        zeros = m.score == 0 ? zeros + 1 : 0;
        if (out) break;
      } else {
        ++passes;
        ++zeros;
      }
      side ^= 1;
      if (passes >= 2 || zeros >= 6) break;
      TTE e;
      Move nm;
      if (!probe(full_key(board_hash(b), r, side, passes, zeros), e) || !unpack(e, nm) || !quick_legal(b, r[side], nm)) break;
      m = nm;
    }
    return pv;
  }
};

// =====================================================================================
// §13  Pre-endgame: exactly one tile in the bag
// =====================================================================================
//
//  Any play that uses at least one tile draws the last tile and empties the bag.  The
//  unseen pool is the opponent's 7 tiles plus the bag tile, so for each candidate we
//  enumerate which of the unseen tiles is the one we draw, solve the resulting endgame
//  (opponent to move) and average.  Candidates are ranked by win probability, then by
//  expected spread: first with both sides playing greedily to the end (cheap, so
//  every candidate gets a value), then the leaders again with 1, 2, ... plies of
//  search before the greedy play-out, all of them to the same depth.

struct PegResult {
  struct Row {
    Move move;
    double win = 0;
    double spread = 0;  // expected spread change for us
    bool exact = true;
  };
  std::vector<Row> rows;  // best first
  double seconds = 0;
};

class PreEndgameSolver {
 public:
  PreEndgameSolver(const Lexicon* lex, const LeaveTable* lt) : lex_(lex), lt_(lt) {}

  PegResult solve(const Position& P, int max_candidates, double time_limit, int threads, bool verbose = false,
                  const Move* must_include = nullptr) {
    PegResult R;
    const double t0 = now_s();
    MoveGen gen(lex_, lt_);
    std::vector<Move> all;
    EvalCtx ctx;
    ctx.bag = P.bag_n;
    ctx.allow_exchange = false;
    gen.generate_all(P.board, P.rack, ctx, all);
    sort_by_equity(all);
    std::vector<Move> cands;
    for (const auto& m : all) {
      if (m.type == MT_PLACE) cands.push_back(m);
      if ((int)cands.size() >= max_candidates) break;
    }
    int forced = -1;  // candidate that must be evaluated (review: the move actually played)
    if (must_include && must_include->type == MT_PLACE) {
      for (size_t c = 0; c < cands.size() && forced < 0; ++c)
        if (cands[c].same_as(*must_include)) forced = (int)c;
      if (forced < 0)
        for (const auto& m : all)
          if (m.same_as(*must_include)) {
            forced = (int)cands.size();
            cands.push_back(m);
            break;
          }
    }
    if (cands.empty()) return R;
    // Distinct possible bag tiles.
    std::vector<std::pair<int, int>> tiles;  // (letter, count)
    for (int L = 0; L < NLET; ++L)
      if (P.unseen.c[L]) tiles.push_back({L, P.unseen.c[L]});
    const int total = P.unseen.n;
    const int nt = (int)tiles.size();
    struct Job {
      int value = 0;
      int depth = -1;  // search depth its value comes from (-1: none yet)
      bool exact = false;
    };
    std::vector<Job> jobs(cands.size() * nt);  // candidate c, tile t: jobs[c * nt + t]
    // Everything, setup included, must fit in the time limit: no endgame starts
    // after the deadline.
    const double deadline = t0 + time_limit * 0.95;
    // Solves the draws of candidates `cs` (in that order) to `depth` plies, 0 meaning
    // both sides just play greedily to the end.  A solve cut off by the deadline
    // leaves the job as it was.
    auto run = [&](const std::vector<int>& cs, int depth) {
      std::vector<int> which;
      for (int c : cs)
        for (int t = 0; t < nt; ++t)
          if (!jobs[c * nt + t].exact) which.push_back(c * nt + t);
      std::atomic<size_t> next{0};
      auto worker = [&]() {
        EndgameSolver eg(lex_, 18);
        while (true) {
          const size_t k = next.fetch_add(1);
          if (k >= which.size()) break;
          const double left = deadline - now_s();
          if (left <= 0.001) break;
          Job& J = jobs[which[k]];
          const Move& m = cands[which[k] / nt];
          const int L = tiles[which[k] % nt].first;
          Board nb = P.board;
          nb.place(*lex_, m);
          Rack mine = P.rack;
          mine.sub_all(m.used());
          mine.add(L);
          Rack opp = P.unseen;
          opp.sub(L);
          EndgameParams ep;
          ep.time_limit = left;
          ep.max_depth = depth;
          ep.tt_bits = 18;
          const EndgameResult er = eg.solve(nb, opp, mine, m.score == 0 ? P.zeros + 1 : 0, ep);
          if (!er.solved && er.depth < depth) continue;
          J.value = m.score - er.value;
          J.exact = er.solved;
          J.depth = depth;
        }
      };
      if (threads <= 1) worker();
      else {
        std::vector<std::thread> th;
        for (int i = 0; i < threads; ++i) th.emplace_back(worker);
        for (auto& x : th) x.join();
      }
    };
    auto complete = [&](int c, int depth) {
      for (int t = 0; t < nt; ++t)
        if (!jobs[c * nt + t].exact && jobs[c * nt + t].depth < depth) return false;
      return true;
    };
    auto row_of = [&](int c) {
      PegResult::Row row;
      row.move = cands[c];
      for (int t = 0; t < nt; ++t) {
        const Job& J = jobs[c * nt + t];
        const double w = (double)tiles[t].second / total;
        const int final_spread = P.spread() + J.value;
        row.win += w * (final_spread > 0 ? 1.0 : (final_spread == 0 ? 0.5 : 0.0));
        row.spread += w * J.value;
        row.exact = row.exact && J.exact;
      }
      return row;
    };
    auto better = [](const PegResult::Row& a, const PegResult::Row& b) {
      if (std::fabs(a.win - b.win) > 1e-9) return a.win > b.win;
      return a.spread > b.spread;
    };
    // Candidates ranked so far, best first, with their rows.
    std::vector<int> rank;
    std::vector<PegResult::Row> rows;
    // Re-ranks the candidates in `cs` among themselves and moves them to the front,
    // above everything that was not searched as deep.
    auto promote = [&](const std::vector<int>& cs) {
      std::vector<std::pair<PegResult::Row, int>> v;
      for (int c : cs) v.push_back({row_of(c), c});
      std::stable_sort(v.begin(), v.end(), [&](const std::pair<PegResult::Row, int>& a, const std::pair<PegResult::Row, int>& b) {
        return better(a.first, b.first);
      });
      std::vector<int> nrank;
      std::vector<PegResult::Row> nrows;
      for (const auto& x : v) {
        nrank.push_back(x.second);
        nrows.push_back(x.first);
      }
      for (size_t i = 0; i < rank.size(); ++i)
        if (std::find(cs.begin(), cs.end(), rank[i]) == cs.end()) {
          nrank.push_back(rank[i]);
          nrows.push_back(rows[i]);
        }
      rank.swap(nrank);
      rows.swap(nrows);
    };
    // Pass 1, every candidate and draw: both sides play greedily to the end.
    {
      std::vector<int> order;
      if (forced >= 0) order.push_back(forced);
      for (int c = 0; c < (int)cands.size(); ++c)
        if (c != forced) order.push_back(c);
      run(order, 0);
      std::vector<int> ok;
      for (int c : order)
        if (complete(c, 0)) ok.push_back(c);
      promote(ok);
    }
    // Deeper rounds over the leaders, one depth at a time so that the candidates
    // compared were searched equally deep.  When time runs out during a round, only
    // the leading candidates whose draws were all searched are re-ranked.
    const int keep = 6;
    for (int d = 1; d <= 40 && now_s() < deadline; ++d) {
      std::vector<int> top(rank.begin(), rank.begin() + std::min<size_t>(keep, rank.size()));
      const bool forced_extra = forced >= 0 && complete(forced, 0) && std::find(top.begin(), top.end(), forced) == top.end();
      bool all_exact = true;
      for (int c : top)
        for (int t = 0; t < nt; ++t) all_exact = all_exact && jobs[c * nt + t].exact;
      if (top.size() < 2 || all_exact) break;
      std::vector<int> order;
      if (forced_extra) order.push_back(forced);
      order.insert(order.end(), top.begin(), top.end());
      run(order, d);
      size_t p = 0;
      while (p < top.size() && complete(top[p], d)) ++p;
      if (p < 2) break;
      std::vector<int> done(top.begin(), top.begin() + p);
      if (forced_extra && complete(forced, d)) done.push_back(forced);
      promote(done);
      if (p < top.size()) break;
    }
    R.rows = rows;
    R.seconds = now_s() - t0;
    if (verbose)
      for (size_t i = 0; i < std::min<size_t>(8, R.rows.size()); ++i)
        std::cerr << fmt("  peg %-22s win %5.1f%%  spread %+6.1f%s\n", move_str(P.board, R.rows[i].move).c_str(),
                         100 * R.rows[i].win, R.rows[i].spread, R.rows[i].exact ? " (exact)" : "");
    return R;
  }

 private:
  const Lexicon* lex_;
  const LeaveTable* lt_;
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §14  Inference: what did the opponent keep?
// =====================================================================================
//
//  After the opponent plays tiles T they kept a leave L of 7-|T| tiles.  For each
//  possible L (enumerated exactly when there are few, otherwise sampled from the
//  unseen pool) we ask: with rack T+L, how close to the best static play was the play
//  they actually made?  Leaves under which their play looks like a mistake become
//  unlikely.  weight(L) = prior(L) * (floor + (1-floor) * exp(-regret / tau)).
//  The simulator then deals the opponent L plus random tiles.

struct InferenceParams {
  double time_limit = 1.0;
  int max_samples = 3000;
  double tau = 4.0;     // points of regret that divide the likelihood by e
  double floor = 0.10;  // keep some weight on every leave (the opponent is not us)
  int max_evals = 0;    // test hook: stop after this many leaves, as the clock would (0: no limit)
};

class Inference {
 public:
  Inference(const Lexicon* lex, const LeaveTable* lt) : lex_(lex), lt_(lt) {}

  // Returns an empty model when inference does not apply.
  OppModel infer(const Position& P, const InferenceParams& ip, std::string* note = nullptr) const {
    OppModel M;
    if (!P.has_opp_last || P.opp_last.type != MT_PLACE || P.bag_n <= 0) return M;
    const Move& om = P.opp_last;
    const Rack played = om.used();
    const int leave_n = RACK_SIZE - played.n;
    if (leave_n <= 0) return M;
    // Tiles unseen to us before their play (includes the tiles they played).
    Rack avail = unseen_from(P.board_before_opp, P.rack);
    if (!avail.contains(played)) return M;
    avail.sub_all(played);
    if (avail.n < leave_n) return M;
    MoveGen gen(lex_, lt_);
    EvalCtx ctx;
    ctx.bag = P.bag_n + played.n;  // bag size before they drew
    ctx.allow_exchange = ctx.bag >= RACK_SIZE;
    const double t0 = now_s();
    auto likelihood = [&](const Rack& leave) {
      Rack full = played;
      full.add_all(leave);
      const Move best = gen.generate_best(P.board_before_opp, full, ctx);
      const float theirs = gen.equity_of(om, full, ctx);
      const double regret = std::max(0.0, (double)best.equity - (double)theirs);
      return ip.floor + (1.0 - ip.floor) * std::exp(-regret / ip.tau);
    };
    // Count distinct leaves; enumerate exactly if few enough.
    std::vector<Rack> exact;
    bool too_many = false;
    {
      Rack cur;
      std::function<void(int, int)> rec = [&](int L, int left) {
        if (too_many) return;
        if (left == 0) {
          exact.push_back(cur);
          if ((int)exact.size() > ip.max_samples) too_many = true;
          return;
        }
        if (L >= NLET) return;
        for (int k = std::min<int>(left, avail.c[L]); k >= 0; --k) {
          cur.c[L] = (int8_t)k;
          cur.n += k;
          rec(L + 1, left - k);
          cur.n -= k;
          cur.c[L] = 0;
          if (too_many) return;
        }
      };
      rec(0, leave_n);
    }
    double total = 0;
    bool cut = false;  // the clock stopped the exact enumeration before its end
    if (!too_many) {
      // The leaves are weighed in a random order, so a clock that stops the loop early
      // leaves a uniform sample of them (each still weighted by its prior), not the first
      // ones in letter order, which hold mostly blanks and A's.  They are kept in letter
      // order, so a complete pass gives the same model as before.
      std::vector<int> order(exact.size());
      for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
      Rng shuffle(mix64(om.hash() ^ (u64)exact.size()));
      for (int i = (int)order.size() - 1; i > 0; --i) std::swap(order[i], order[shuffle.below((u32)i + 1)]);
      std::vector<double> weight(exact.size(), -1.0);  // -1: not weighed
      int weighed = 0;
      for (int i : order) {
        if (now_s() - t0 > ip.time_limit || (ip.max_evals > 0 && weighed >= ip.max_evals)) {
          cut = true;
          break;
        }
        const Rack& leave = exact[i];
        double prior = 1;
        for (int L = 0; L < NLET; ++L)
          for (int k = 0; k < leave.c[L]; ++k) prior *= (double)(avail.c[L] - k) / (double)(k + 1);
        weight[i] = prior * likelihood(leave);
        ++weighed;
      }
      for (size_t i = 0; i < exact.size(); ++i)
        if (weight[i] >= 0) {
          M.leaves.push_back(exact[i]);
          total += weight[i];
          M.cum.push_back(total);
        }
    } else {
      Rng rng(time_seed());
      for (int s = 0; s < ip.max_samples; ++s) {
        if (now_s() - t0 > ip.time_limit || (ip.max_evals > 0 && s >= ip.max_evals)) break;
        Rack pool = avail, leave;
        for (int k = 0; k < leave_n; ++k) leave.add(draw_tile(pool, rng));
        const double w = likelihood(leave);
        M.leaves.push_back(leave);
        total += w;
        M.cum.push_back(total);
      }
    }
    // Need a reasonable coverage before trusting it.
    if (M.leaves.size() < 30 && (too_many || cut)) M = OppModel();
    if (note && !M.empty()) {
      // Report the most likely kept tiles.
      std::map<int, double> tile_w;
      double prev = 0;
      for (size_t i = 0; i < M.leaves.size(); ++i) {
        const double w = M.cum[i] - prev;
        prev = M.cum[i];
        for (int L = 0; L < NLET; ++L)
          if (M.leaves[i].c[L]) tile_w[L] += w;
      }
      std::vector<std::pair<double, int>> v;
      for (auto& kv : tile_w) v.push_back({kv.second / total, kv.first});
      std::sort(v.rbegin(), v.rend());
      std::ostringstream o;
      o << "inferred opponent leave (" << M.leaves.size()
        << (too_many ? " sampled" : cut ? " of " + std::to_string(exact.size()) + ", out of time" : " exact") << "): P(holds)";
      for (size_t i = 0; i < std::min<size_t>(6, v.size()); ++i)
        o << ' ' << rack_char(v[i].second) << '=' << std::fixed << std::setprecision(0) << 100 * v[i].first << '%';
      *note = o.str();
    }
    return M;
  }

 private:
  const Lexicon* lex_;
  const LeaveTable* lt_;
};

// =====================================================================================
// §15  The engine: picks the right tool for the position
// =====================================================================================

struct EngineConfig {
  std::string name = "champion";
  bool simulate = true;       // false = static evaluation only
  SimParams sim;
  bool endgame = true;        // exact endgame solver when the bag is empty
  double endgame_time = 8.0;
  bool preendgame = true;     // exhaustive 1-tile-in-bag solver
  double peg_time = 8.0;
  int peg_candidates = 16;
  bool inference = true;
  InferenceParams inf;
  int threads = 1;
  std::string leaves_file;  // optional per-player data (matches between trained sets)
  std::string win_file;
  std::string include;      // analysis only: this move is simulated too, even if the cut drops it

  // "static", "sim", "champion", optionally followed by ":key=value,key=value".
  static bool parse(const std::string& spec, EngineConfig& c, std::string& err) {
    c = EngineConfig();
    std::string base = spec, opts;
    const size_t colon = spec.find(':');
    if (colon != std::string::npos) {
      base = spec.substr(0, colon);
      opts = spec.substr(colon + 1);
    }
    c.name = spec;
    if (base == "static") {
      c.simulate = false;
      c.endgame = false;
      c.preendgame = false;
      c.inference = false;
    } else if (base == "static+") {
      c.simulate = false;
      c.endgame_time = 1.0;
      c.peg_time = 1.0;
      c.inference = false;
    } else if (base == "sim") {
      c.sim.time_limit = 2.0;
      c.sim.max_iterations = 600;
      c.sim.max_candidates = 15;
      c.endgame_time = 2.0;
      c.peg_time = 2.0;
      c.inf.time_limit = 0.3;
    } else if (base == "champion") {
      c.sim.time_limit = 12.0;
      c.sim.max_iterations = 1000000;
      c.sim.max_candidates = 30;
      c.endgame_time = 12.0;
      c.peg_time = 12.0;
    } else {
      err = "unknown player '" + base + "' (use static, static+, sim or champion)";
      return false;
    }
    // Option values come from users and GUIs: the experimental options are kept in sensible
    // ranges (a NaN becomes the lower bound) so no setting can make a move unbounded.
    auto clampd = [](double x, double lo, double hi) { return x >= lo ? (x <= hi ? x : hi) : lo; };
    for (const auto& kv : parse_kv(opts)) {
      const std::string& k = kv.first;
      const double v = std::atof(kv.second.c_str());
      if (k.empty()) continue;
      if (k == "time") {
        c.sim.time_limit = v;
        c.endgame_time = v;
        c.peg_time = v;
      } else if (k == "iters") c.sim.max_iterations = (int)v;
      else if (k == "plies") c.sim.plies = (int)v;
      else if (k == "playout") c.sim.playout_bag = (int)v;
      else if (k == "cands") c.sim.max_candidates = (int)v;
      else if (k == "latecands") c.sim.late_candidates = (int)v;
      else if (k == "deep") c.sim.deep_k = (int)clampd(v, 0, 30);
      else if (k == "deepplies") c.sim.deep_plies = (int)clampd(v, 1, 40);
      else if (k == "deepfrac") c.sim.deep_frac = clampd(v, 0.05, 0.95);
      else if (k == "threads") c.threads = (int)v;
      else if (k == "z") c.sim.prune_z = v;
      else if (k == "tau") c.sim.shrink_tau = v;
      else if (k == "keep") c.sim.keep = clampd(v, 0, 1);
      else if (k == "keepbag") c.sim.keep_bag = (int)clampd(v, 0, 100);
      else if (k == "egk") c.sim.eg_k = (int)clampd(v, 0, 64);
      else if (k == "win") c.sim.win_objective = v != 0;
      else if (k == "eg") c.endgame = v != 0;
      else if (k == "egtime") c.endgame_time = v;
      else if (k == "peg") c.preendgame = v != 0;
      else if (k == "pegtime") c.peg_time = v;
      else if (k == "inf") c.inference = v != 0;
      else if (k == "sim") c.simulate = v != 0;
      else if (k == "leaves") c.leaves_file = kv.second;
      else if (k == "winmodel") c.win_file = kv.second;
      else {
        err = "unknown option '" + k + "'";
        return false;
      }
    }
    return true;
  }
};

// One analysed candidate.  Fields that a method does not produce are NaN / -1.
struct DecisionRow {
  Move move;
  double static_eq = NAN;  // static equity
  double value = NAN;      // simulated spread gain / exact endgame value / expected pre-endgame spread
  double win = NAN;        // win probability (0..1)
  int iterations = -1;     // simulation iterations spent on this move
  bool pruned = false;
};

struct Decision {
  Move move;
  std::string method;
  std::vector<std::string> report;  // human-readable analysis lines
  std::vector<DecisionRow> rows;    // structured analysis, best first
  double seconds = 0;
  bool exact = false;               // proven result (solved endgame / exhaustive pre-endgame)
  std::string info;                 // telemetry, one JSON object (protocol mode prints it as "info ...")
};

// Numbers for telemetry JSON (NaN and infinities become null).
inline std::string jnum(double v, int prec = 4) {
  if (!std::isfinite(v)) return "null";
  std::ostringstream o;
  o << std::setprecision(prec) << v;
  return o.str();
}

// Analysis of a given move in a solved or searched endgame: its value among the root moves.
inline std::string endgame_include(const Position& P, const EndgameResult& er, const std::string& include) {
  Move keep;
  std::string err;
  if (include.empty() || !parse_move(P.board, include, keep, err)) return "";
  for (size_t i = 0; i < er.root.size(); ++i)
    if (er.root[i].first.same_as(keep))
      return ",\"inc\":{\"m\":\"" + move_str(P.board, keep) + "\",\"value\":" + std::to_string(er.root[i].second) + "}";
  return ",\"inc\":{\"m\":\"" + move_str(P.board, keep) + "\",\"value\":null}";
}

class Engine {
 public:
  Engine(const Lexicon* lex, const LeaveTable* lt, const WinModel* wm)
      : lex_(lex), lt_(lt), wm_(wm), sim_(lex, lt, wm), eg_(lex, 20), peg_(lex, lt), inf_(lex, lt) {}

  Decision choose(const Position& P, const EngineConfig& cfg, bool verbose = false) {
    const double t_start = now_s();
    Decision D;
    MoveGen gen(lex_, lt_);
    const EvalCtx ctx = Simulator::ctx_for(P);
    // 1. Endgame: perfect information.
    if (P.bag_n == 0 && cfg.endgame && P.rack.n > 0) {
      EndgameParams ep;
      ep.time_limit = cfg.endgame_time;
      ep.threads = std::max(1, cfg.threads);
      ep.verbose = verbose;
      const EndgameResult er = eg_.solve(P.board, P.rack, P.unseen, P.zeros, ep);
      D.move = er.best;
      D.method = er.solved ? "endgame (solved)" : "endgame (depth " + std::to_string(er.depth) + ")";
      D.exact = er.solved;
      D.seconds = er.seconds;
      for (const auto& rm : er.root) {
        DecisionRow row;
        row.move = rm.first;
        row.value = rm.second;
        const int final_spread = P.spread() + rm.second;
        row.win = final_spread > 0 ? 1.0 : (final_spread == 0 ? 0.5 : 0.0);
        D.rows.push_back(row);
      }
      D.info = "{\"phase\":\"endgame\",\"bag\":0,\"unseen\":" + std::to_string(P.unseen.n) +
               ",\"spread\":" + std::to_string(P.spread()) + ",\"solved\":" + (er.solved ? "true" : "false") +
               ",\"depth\":" + std::to_string(er.depth) + ",\"nodes\":" + std::to_string(er.nodes) +
               ",\"value\":" + std::to_string(er.value) + ",\"roots\":" + std::to_string(er.root.size()) +
               endgame_include(P, er, cfg.include) +
               ",\"t\":{\"search\":" + jnum(er.seconds) + ",\"total\":" + jnum(now_s() - t_start) + "}}";
      std::ostringstream o;
      o << "endgame: " << move_str(P.board, er.best) << "  value " << std::showpos << er.value << std::noshowpos
        << (er.solved ? " (exact)" : " (best found)") << ", depth " << er.depth << ", " << er.nodes << " nodes, "
        << std::fixed << std::setprecision(2) << er.seconds << "s";
      D.report.push_back(o.str());
      if (!er.pv.empty()) {
        std::string line = "  principal variation:";
        Board b = P.board;
        for (const auto& m : er.pv) {
          line += "  " + move_str(b, m);
          if (m.type == MT_PLACE) b.place(*lex_, m);
        }
        D.report.push_back(line);
      }
      return D;
    }
    // 2. Pre-endgame with one tile in the bag.
    if (P.bag_n == 1 && cfg.preendgame) {
      const PegResult pr = peg_.solve(P, cfg.peg_candidates, cfg.peg_time, cfg.threads, verbose);
      if (pr.rows.empty()) {  // not even one candidate solved in time: static play
        D.move = gen.generate_best(P.board, P.rack, ctx);
        D.method = "static (pre-endgame out of time)";
        D.info = "{\"phase\":\"peg\",\"bag\":1,\"rows\":0,\"t\":{\"total\":" + jnum(now_s() - t_start) + "}}";
        DecisionRow row;
        row.move = D.move;
        row.static_eq = D.move.equity;
        D.rows.push_back(row);
        return D;
      }
      if (!pr.rows.empty()) {
        D.move = pr.rows[0].move;
        D.method = "pre-endgame";
        D.seconds = pr.seconds;
        D.exact = pr.rows[0].exact;
        {
          int exact_rows = 0;
          for (const auto& r : pr.rows) exact_rows += r.exact;
          std::string top;
          for (size_t i = 0; i < std::min<size_t>(4, pr.rows.size()); ++i)
            top += std::string(i ? "," : "") + "{\"m\":\"" + move_str(P.board, pr.rows[i].move) + "\",\"w\":" +
                   jnum(pr.rows[i].win) + ",\"e\":" + jnum(pr.rows[i].spread) + "}";
          D.info = "{\"phase\":\"peg\",\"bag\":1,\"unseen\":" + std::to_string(P.unseen.n) + ",\"spread\":" +
                   std::to_string(P.spread()) + ",\"rows\":" + std::to_string(pr.rows.size()) + ",\"exact_rows\":" +
                   std::to_string(exact_rows) + ",\"best\":{\"w\":" + jnum(pr.rows[0].win) + "},\"top\":[" + top +
                   "],\"t\":{\"search\":" + jnum(pr.seconds) + ",\"total\":" + jnum(now_s() - t_start) + "}}";
        }
        for (const auto& r : pr.rows) {
          DecisionRow row;
          row.move = r.move;
          row.value = r.spread;
          row.win = r.win;
          row.static_eq = r.move.equity;
          D.rows.push_back(row);
        }
        for (size_t i = 0; i < std::min<size_t>(5, pr.rows.size()); ++i)
          D.report.push_back(fmt("  %-24s win %5.1f%%  spread %+6.1f", move_str(P.board, pr.rows[i].move).c_str(),
                                 100 * pr.rows[i].win, pr.rows[i].spread));
        // The static fallback may still prefer a pass/other move; trust the solver.
        return D;
      }
    }
    // 3. Simulation.
    if (cfg.simulate) {
      int generated = 0;
      Move keep;
      std::string keep_err;
      const bool has_keep = !cfg.include.empty() && parse_move(P.board, cfg.include, keep, keep_err);
      // Near the end the static ranking is a weaker guide, so more candidates may be kept.
      const int ncand = P.bag_n <= cfg.sim.playout_bag && cfg.sim.late_candidates > 0 ? cfg.sim.late_candidates
                                                                                     : cfg.sim.max_candidates;
      std::vector<Move> cands = sim_.candidates(P, ncand, &generated, has_keep ? &keep : nullptr);
      const double t_gen = now_s() - t_start;
      if (cands.size() > 1) {
        OppModel opp;
        std::string note;
        // Inference and simulation share the time for the move.
        InferenceParams ip = cfg.inf;
        ip.time_limit = std::min(ip.time_limit, 0.25 * cfg.sim.time_limit);
        const double t_inf0 = now_s();
        if (cfg.inference) opp = inf_.infer(P, ip, &note);
        const double t_inf = now_s() - t_inf0;
        if (!note.empty()) D.report.push_back(note);
        SimParams sp = cfg.sim;
        sp.threads = std::max(sp.threads, cfg.threads);
        sp.time_limit = std::max(0.02, cfg.sim.time_limit - (now_s() - t_start));
        const bool deep = sp.deep_k >= 2 && P.bag_n > sp.playout_bag && (int)cands.size() > sp.deep_k;
        const double sim_time = sp.time_limit;
        if (deep) sp.time_limit *= sp.deep_frac;
        // Telemetry: the ranking at 1/64, 1/32, ... 1/2 of the search, to see how the
        // choice changes with more work (a copy is ranked; the search itself is untouched).
        struct Snap {
          double t;
          int iters;
          Move top;
        };
        std::vector<Snap> snaps;
        double next_frac = 1.0 / 64;
        const double t_sim0 = now_s();
        auto progress = [&](const SimResult& R) {
          const double el = now_s() - t_sim0;
          if (next_frac >= 1.0 || el < next_frac * sp.time_limit) return;
          while (next_frac < 1.0 && next_frac * sp.time_limit <= el) next_frac *= 2;
          SimResult copy = R;
          Simulator::rank_snapshot(copy, P, sp);
          int it = 0;
          for (const auto& c : R.cands) it = std::max(it, c.n);
          snaps.push_back({el, it, copy.cands[0].move});
        };
        const SimResult sr = sim_.run(P, cands, sp, opp.empty() ? nullptr : &opp, progress);
        SimResult dr;
        if (deep) {
          std::vector<Move> finalists;
          for (int i = 0; i < sp.deep_k; ++i) finalists.push_back(sr.cands[i].move);
          SimParams dp = sp;
          dp.plies = sp.deep_plies;
          dp.time_limit = std::max(0.02, sim_time - (now_s() - t_sim0));
          dr = sim_.run(P, finalists, dp, opp.empty() ? nullptr : &opp);
          // Keep the other candidates available to analysis and review, after the finalists.
          for (size_t i = sp.deep_k; i < sr.cands.size(); ++i) {
            dr.cands.push_back(sr.cands[i]);
            dr.cands.back().active = false;
          }
        }
        const SimResult& result = deep ? dr : sr;
        D.move = result.cands[0].move;
        D.method = "simulation";
        D.seconds = sr.seconds + (deep ? dr.seconds : 0);
        {
          // Static rank of each candidate (cands is in static order).
          auto srank = [&](const Move& m) {
            for (size_t i = 0; i < cands.size(); ++i)
              if (cands[i].same_as(m)) return (int)i;
            return -1;
          };
          std::vector<int> alloc(cands.size(), 0);
          int pruned = 0;
          for (const auto& c : sr.cands) {
            const int k = srank(c.move);
            if (k >= 0) alloc[k] = c.n;
            pruned += !c.active;
          }
          const bool playout = P.bag_n <= sp.playout_bag;
          const int plies = playout ? std::max(sp.plies, 40) : sp.plies;
          std::ostringstream o;
          o << "{\"phase\":\"" << (playout ? "playout" : "sim") << "\",\"bag\":" << P.bag_n
            << ",\"unseen\":" << P.unseen.n << ",\"spread\":" << P.spread() << ",\"gen\":" << generated
            << ",\"cands\":" << cands.size() << ",\"plies\":" << plies << ",\"iters\":" << sr.iterations
            << ",\"pos\":" << sr.positions << ",\"pruned\":" << pruned
            << ",\"inf\":" << (opp.empty() ? -1 : (int)opp.leaves.size())
            << ",\"tau\":" << jnum(sp.shrink_tau * (playout ? 2.5 : 1.0)) << ",\"z\":" << jnum(sp.prune_z)
            << (Simulator::keep_active(P, sp, opp.empty() ? nullptr : &opp) ? ",\"keep\":" + jnum(sp.keep) : std::string())
            << ",\"best\":{\"w\":" << jnum(result.cands[0].mean_win()) << ",\"e\":" << jnum(result.cands[0].mean_eq())
            << ",\"s\":" << srank(result.cands[0].move) << "},\"alloc\":[";
          for (size_t i = 0; i < alloc.size(); ++i) o << (i ? "," : "") << alloc[i];
          o << "],\"top\":[";
          for (size_t i = 0; i < std::min<size_t>(6, result.cands.size()); ++i) {
            const auto& c = result.cands[i];
            o << (i ? "," : "") << "{\"m\":\"" << move_str(P.board, c.move) << "\",\"s\":" << srank(c.move)
              << ",\"st\":" << jnum(c.static_eq) << ",\"n\":" << c.n << ",\"pr\":" << (c.active ? 0 : 1)
              << ",\"w\":" << jnum(c.mean_win()) << ",\"e\":" << jnum(c.mean_eq()) << ",\"post\":" << jnum(c.post, 5)
              << ",\"d\":" << jnum(c.sim_diff, 5) << ",\"pw\":" << jnum(c.prior_w) << "}";
          }
          o << "]";
          if (deep)
            o << ",\"deep\":{\"k\":" << sp.deep_k << ",\"plies\":" << sp.deep_plies
              << ",\"t1\":" << jnum(sr.seconds) << ",\"t2\":" << jnum(dr.seconds) << ",\"it2\":" << dr.iterations
              << ",\"s1\":\"" << move_str(P.board, sr.cands[0].move) << "\",\"same\":"
              << (sr.cands[0].move.same_as(D.move) ? 1 : 0) << "}";
          if (has_keep) {
            for (size_t i = 0; i < sr.cands.size(); ++i)
              if (sr.cands[i].move.same_as(keep)) {
                const auto& c = sr.cands[i];
                o << ",\"inc\":{\"m\":\"" << move_str(P.board, c.move) << "\",\"rank\":" << i << ",\"s\":" << srank(c.move)
                  << ",\"n\":" << c.n << ",\"pr\":" << (c.active ? 0 : 1) << ",\"w\":" << jnum(c.mean_win())
                  << ",\"e\":" << jnum(c.mean_eq()) << ",\"post\":" << jnum(c.post, 5) << ",\"pw\":" << jnum(c.prior_w) << "}";
                break;
              }
          }
          o << ",\"snap\":[";
          for (size_t i = 0; i < snaps.size(); ++i)
            o << (i ? "," : "") << "{\"t\":" << jnum(snaps[i].t) << ",\"it\":" << snaps[i].iters << ",\"m\":\""
              << move_str(P.board, snaps[i].top) << "\",\"same\":" << (snaps[i].top.same_as(sr.cands[0].move) ? 1 : 0)
              << "}";
          o << "],\"t\":{\"gen\":" << jnum(t_gen) << ",\"inf\":" << jnum(t_inf) << ",\"sim\":" << jnum(sr.seconds)
            << ",\"total\":" << jnum(now_s() - t_start) << "}}";
          D.info = o.str();
        }
        for (const auto& c : result.cands) {
          DecisionRow row;
          row.move = c.move;
          row.static_eq = c.static_eq;
          row.value = c.mean_eq();
          row.win = c.mean_win();
          row.iterations = c.n;
          row.pruned = !c.active;
          D.rows.push_back(row);
        }
        D.report.push_back(fmt("simulated %d iterations, %d plies, %.1fs:", result.iterations,
                               deep ? sp.deep_plies : sp.plies, result.seconds));
        for (size_t i = 0; i < std::min<size_t>(8, result.cands.size()); ++i) {
          const auto& c = result.cands[i];
          D.report.push_back(fmt("  %-24s static %6.1f  sim %+6.1f  win %5.1f%%  (%d it)%s",
                                 move_str(P.board, c.move).c_str(), c.static_eq, c.mean_eq(), 100 * c.mean_win(), c.n,
                                 c.active ? "" : " pruned"));
        }
        return D;
      }
      if (cands.size() == 1) {
        D.move = cands[0];
        D.method = "only move";
        D.info = "{\"phase\":\"only\",\"bag\":" + std::to_string(P.bag_n) + ",\"gen\":1}";
        DecisionRow row;
        row.move = cands[0];
        row.static_eq = cands[0].equity;
        D.rows.push_back(row);
        return D;
      }
    }
    // 4. Static evaluation.
    D.move = gen.generate_best(P.board, P.rack, ctx);
    D.method = "static";
    {
      DecisionRow row;
      row.move = D.move;
      row.static_eq = D.move.equity;
      D.rows.push_back(row);
    }
    return D;
  }

  const WinModel* win_model() const { return wm_; }
  Simulator& simulator() { return sim_; }
  EndgameSolver& endgame() { return eg_; }
  PreEndgameSolver& preendgame() { return peg_; }
  Inference& inference() { return inf_; }

 private:
  const Lexicon* lex_;
  const LeaveTable* lt_;
  const WinModel* wm_;
  Simulator sim_;
  EndgameSolver eg_;
  PreEndgameSolver peg_;
  Inference inf_;
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §16  Autoplay: engine-vs-engine matches
// =====================================================================================

struct MatchResult {
  int games = 0;
  double wins = 0;  // for player A (ties count 1/2)
  double spread_sum = 0, spread_sq = 0;
  double score_a = 0, score_b = 0;
  long bingos_a = 0, bingos_b = 0;
  long turns = 0;
  double seconds = 0;
  void add(const MatchResult& o) {
    games += o.games;
    wins += o.wins;
    spread_sum += o.spread_sum;
    spread_sq += o.spread_sq;
    score_a += o.score_a;
    score_b += o.score_b;
    bingos_a += o.bingos_a;
    bingos_b += o.bingos_b;
    turns += o.turns;
  }
  std::string summary(const std::string& a, const std::string& b) const {
    if (!games) return "no games";
    const double wr = wins / games;
    const double ms = spread_sum / games;
    const double sd = std::sqrt(std::max(0.0, spread_sq / games - ms * ms));
    const double se_w = std::sqrt(std::max(1e-9, wr * (1 - wr) / games));
    double elo = 0;
    if (wr > 0 && wr < 1) elo = -400.0 * std::log10(1.0 / wr - 1.0);
    std::ostringstream o;
    o << std::fixed << std::setprecision(1);
    o << a << " vs " << b << ": " << games << " games\n";
    o << "  " << a << " wins " << wins << " (" << 100 * wr << "% +/- " << 196 * se_w << ")";
    o << "   mean spread " << std::showpos << ms << std::noshowpos << " +/- " << 1.96 * sd / std::sqrt((double)games);
    o << "   ~" << std::showpos << (int)std::lround(elo) << std::noshowpos << " Elo\n";
    o << "  avg score " << score_a / games << " - " << score_b / games << "   bingos/game " << (double)bingos_a / games
      << " - " << (double)bingos_b / games << "   " << seconds << "s";
    return o.str();
  }
};

// Plays one game; `first` is the index (0 = A, 1 = B) of the player who starts.
inline void play_one_game(const Lexicon& lex, Engine& ea, Engine& eb, const EngineConfig& ca, const EngineConfig& cb,
                          int first, u64 seed, MatchResult& out, Game* keep = nullptr) {
  Rng rng(seed);
  Game G;
  G.reset(rng);
  int turns = 0;
  while (!G.over && turns < 200) {
    const bool a_to_move = (G.turn == 0) == (first == 0);
    Position P = Position::from_game(G);
    Decision D = a_to_move ? ea.choose(P, ca) : eb.choose(P, cb);
    // Safety: never apply an illegal move (fall back to pass).
    if (D.move.type == MT_EXCHANGE && (G.bag.n < RACK_SIZE || !G.rack[G.turn].contains(D.move.used()))) D.move = Move();
    if (D.move.type == MT_PLACE && !G.rack[G.turn].contains(D.move.used())) D.move = Move();
    if (D.move.type == MT_PLACE && D.move.ntiles == RACK_SIZE) (a_to_move ? out.bingos_a : out.bingos_b)++;
    G.apply(lex, D.move, rng);
    ++turns;
  }
  const int sa = first == 0 ? G.score[0] : G.score[1];
  const int sb = first == 0 ? G.score[1] : G.score[0];
  out.games++;
  out.wins += sa > sb ? 1.0 : (sa == sb ? 0.5 : 0.0);
  out.spread_sum += sa - sb;
  out.spread_sq += (double)(sa - sb) * (sa - sb);
  out.score_a += sa;
  out.score_b += sb;
  out.turns += turns;
  if (keep) *keep = G;
}

inline MatchResult run_match(const Lexicon& lex, const LeaveTable& lt, const WinModel& wm, const EngineConfig& ca,
                             const EngineConfig& cb, int games, int threads, u64 seed, bool progress = true) {
  MatchResult total;
  const double t0 = now_s();
  // Per-player leave / win-model files, if given.
  std::unique_ptr<LeaveTable> la, lb;
  std::unique_ptr<WinModel> wa, wb;
  auto load_data = [&](const EngineConfig& c, std::unique_ptr<LeaveTable>& L, std::unique_ptr<WinModel>& W) {
    std::string err;
    if (c.leaves_file == "default") {
      L.reset(new LeaveTable());  // the built-in model
    } else if (c.leaves_file == "zero") {
      L.reset(new LeaveTable());  // no leave knowledge at all: pure score maximiser
      std::vector<u32> keys;
      L->for_each([&](u32 k, float) { keys.push_back(k); });
      for (u32 k : keys) L->set(k, 0.f);
    } else if (!c.leaves_file.empty()) {
      L.reset(new LeaveTable());
      if (!L->load(c.leaves_file, err)) {
        std::cerr << "warning: " << err << " (using the current leaves)\n";
        L.reset();
      }
    }
    if (!c.win_file.empty()) {
      W.reset(new WinModel());
      if (!W->load(c.win_file, err)) {
        std::cerr << "warning: " << err << " (using the current win model)\n";
        W.reset();
      }
    }
  };
  load_data(ca, la, wa);
  load_data(cb, lb, wb);
  const LeaveTable* lta = la ? la.get() : &lt;
  const LeaveTable* ltb = lb ? lb.get() : &lt;
  const WinModel* wma = wa ? wa.get() : &wm;
  const WinModel* wmb = wb ? wb.get() : &wm;
  std::mutex mu;
  std::atomic<int> next{0};
  auto worker = [&]() {
    Engine ea(&lex, lta, wma), eb(&lex, ltb, wmb);
    while (true) {
      const int g = next.fetch_add(1);
      if (g >= games) break;
      MatchResult r;
      // Pairs of games share a seed with the first move swapped.
      play_one_game(lex, ea, eb, ca, cb, g & 1, mix64(seed + (u64)(g / 2)), r);
      std::lock_guard<std::mutex> lk(mu);
      total.add(r);
      if (progress && (total.games % std::max(1, games / 20) == 0 || total.games == games)) {
        const double wr = total.wins / total.games;
        std::cerr << fmt("\r  %d/%d games  %s win %.1f%%  mean spread %+.1f   ", total.games, games, ca.name.c_str(), 100 * wr,
                         total.spread_sum / total.games)
                  << std::flush;
      }
    }
  };
  std::vector<std::thread> th;
  for (int t = 0; t < std::max(1, threads); ++t) th.emplace_back(worker);
  for (auto& x : th) x.join();
  if (progress) std::cerr << "\n";
  total.seconds = now_s() - t0;
  return total;
}


// Writes leave values as KLV2 (see LeaveTable::load_klv): a minimal DAWG of the sorted
// leaves (blank = 0 first) followed by one f32 per leave in DAWG order.
inline bool save_klv2(const LeaveTable& lt, const std::string& path) {
  std::vector<std::string> words;
  lt.for_each([&](u32 key, float) {
    int8_t c[NLET];
    LeaveTable::counts_of(key, c);
    std::string w;
    for (int L = 0; L < NLET; ++L)
      for (int k = 0; k < c[L]; ++k) w += (char)L;
    words.push_back(w);
  });
  std::sort(words.begin(), words.end());
  std::unique_ptr<LexBuilder> B(new LexBuilder());
  const int root = B->new_state();
  for (const auto& w : words) B->add(root, (const u8*)w.data(), (int)w.size());
  B->finish(root);
  std::vector<u32> nodes(2, 0u);
  std::vector<u32> memo(B->st.size(), UINT32_MAX);
  std::function<u32(int)> emit = [&](int s) -> u32 {
    if (B->st[s].arcs.empty()) return 0;
    if (memo[s] != UINT32_MAX) return memo[s];
    const size_t k = B->st[s].arcs.size();
    std::vector<u32> packed(k);
    for (size_t i = 0; i < k; ++i) {
      const int t = B->st[s].arcs[i].second;
      packed[i] = ((u32)B->st[s].arcs[i].first << 24) | (B->st[t].fin ? Lexicon::ACCEPT_BIT : 0u) | emit(t);
    }
    packed.back() |= Lexicon::END_BIT;
    const u32 idx = (u32)nodes.size();
    nodes.insert(nodes.end(), packed.begin(), packed.end());
    memo[s] = idx;
    return idx;
  };
  const u32 r = emit(root);
  nodes[0] = Lexicon::END_BIT | r;  // DAWG root pointer
  nodes[1] = Lexicon::END_BIT;      // no GADDAG
  // Values in DAWG (pre-order) order.
  std::vector<float> vals;
  std::string cur;
  std::function<void(u32)> walk = [&](u32 list) {
    if (!list) return;
    for (u32 i = list;; ++i) {
      const u32 v = nodes[i];
      cur.push_back((char)Lexicon::label(v));
      if (Lexicon::accepts(v)) {
        int8_t c[NLET] = {0};
        for (char ch : cur) c[(u8)ch]++;
        vals.push_back(lt.get(LeaveTable::key_of(c)));
      }
      walk(Lexicon::child(v));
      cur.pop_back();
      if (Lexicon::is_end(v)) break;
    }
  };
  walk(r);
  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  auto w32 = [&](u32 x) {
    const unsigned char b[4] = {(unsigned char)x, (unsigned char)(x >> 8), (unsigned char)(x >> 16), (unsigned char)(x >> 24)};
    out.write((const char*)b, 4);
  };
  w32((u32)nodes.size());
  for (u32 x : nodes) w32(x);
  w32((u32)vals.size());
  for (float f : vals) {
    u32 x;
    std::memcpy(&x, &f, 4);
    w32(x);
  }
  return (bool)out;
}

// =====================================================================================
// §17  Self-play training of leave values and the win model
// =====================================================================================
//
//  Leave values.  In every self-play turn with a full rack R, the equity E of the best
//  play (score + value of what is kept) measures how good R was.  E is credited to
//  every sub-multiset L of R (1..6 tiles); then
//        value(L) = mean(E | L is part of the rack) - mean(E).
//  Holding L before a draw means your next rack will contain L, so this is exactly the
//  "worth of keeping L".  Rarely seen leaves are shrunk towards a structural prior
//  built from their sub-leaves (single-tile values plus pairwise synergies).
//  Generations repeat the process with the improved values (policy iteration).
//
//  Win model.  Every turn records (spread, unseen tiles) for the player to move and
//  whether they went on to win; a logistic curve is fitted per unseen-tile count.

class KeyIndex {
 public:
  void build() {
    keys_.clear();
    LeaveTable::enumerate(6, [&](const int8_t* cnt, int) { keys_.push_back(LeaveTable::key_of(cnt)); });
    size_t cap = 1;
    while (cap < keys_.size() * 2) cap <<= 1;
    slots_.assign(cap, -1);
    mask_ = cap - 1;
    for (size_t i = 0; i < keys_.size(); ++i) {
      size_t h = hash(keys_[i]);
      while (slots_[h] >= 0) h = (h + 1) & mask_;
      slots_[h] = (int)i;
    }
  }
  int find(u32 key) const {
    size_t h = hash(key);
    while (true) {
      const int i = slots_[h];
      if (i < 0) return -1;
      if (keys_[i] == key) return i;
      h = (h + 1) & mask_;
    }
  }
  size_t size() const { return keys_.size(); }
  u32 key(size_t i) const { return keys_[i]; }

 private:
  std::vector<u32> keys_;
  std::vector<int> slots_;
  size_t mask_ = 0;
  size_t hash(u32 k) const { return (size_t)((k * 2654435761u) ^ (k >> 13)) & mask_; }
};

struct TrainParams {
  int games = 20000;       // per generation
  int generations = 4;
  int threads = 1;
  double shrink = 25.0;    // prior weight, in observations
  bool learn_leaves = true;  // false: keep the leave values, refit only the win model
  std::string out = "trained";
  u64 seed = 0;
};

struct WinSample {
  float spread;
  u8 unseen;
  u8 won2;  // 0 loss, 1 tie, 2 win
};

inline void fit_win_model(const std::vector<WinSample>& data, WinModel& wm) {
  // Bucket by unseen count; fit logistic regression pooled over a small window.
  std::vector<std::vector<const WinSample*>> by(MAX_UNSEEN + 1);
  for (const auto& d : data)
    if (d.unseen <= MAX_UNSEEN) by[d.unseen].push_back(&d);
  WinModel def;
  for (int t = 1; t <= MAX_UNSEEN; ++t) {
    std::vector<const WinSample*> pts(by[t].begin(), by[t].end());
    for (int w = 1; w <= 6 && pts.size() < 20000; ++w) {
      if (t - w >= 1) pts.insert(pts.end(), by[t - w].begin(), by[t - w].end());
      if (t + w <= MAX_UNSEEN) pts.insert(pts.end(), by[t + w].begin(), by[t + w].end());
    }
    if (pts.size() < 200) {
      wm.a[t] = def.a[t];
      wm.b[t] = def.b[t];
      continue;
    }
    double a = 0, b = 0.02;
    for (int iter = 0; iter < 25; ++iter) {
      double ga = 0, gb = 0, haa = 0, hab = 0, hbb = 0;
      for (const WinSample* p : pts) {
        const double y = p->won2 * 0.5;
        const double z = a + b * p->spread;
        const double pr = 1.0 / (1.0 + std::exp(-z));
        const double r = y - pr;
        const double w = std::max(1e-6, pr * (1 - pr));
        ga += r;
        gb += r * p->spread;
        haa += w;
        hab += w * p->spread;
        hbb += w * p->spread * p->spread;
      }
      // small ridge for stability
      haa += 1e-3;
      hbb += 1e-3;
      const double det = haa * hbb - hab * hab;
      if (std::fabs(det) < 1e-12) break;
      const double da = (hbb * ga - hab * gb) / det;
      const double db = (-hab * ga + haa * gb) / det;
      a += da;
      b += db;
      if (std::fabs(da) < 1e-7 && std::fabs(db) < 1e-9) break;
    }
    if (!(b > 0) || !std::isfinite(a) || !std::isfinite(b)) {
      wm.a[t] = def.a[t];
      wm.b[t] = def.b[t];
    } else {
      wm.a[t] = (float)a;
      wm.b[t] = (float)b;
    }
  }
  wm.a[0] = 0;
  wm.b[0] = 1;
}

inline void train(const Lexicon& lex, LeaveTable& leaves, WinModel& wm, const TrainParams& tp, std::ostream& log) {
  KeyIndex index;
  index.build();
  const size_t NL = index.size();
  log << "training on lexicon " << lex.name << ": " << tp.generations << " generation(s) x " << tp.games << " games, "
      << tp.threads << " thread(s); " << NL << " leaves\n";
  const u64 seed = tp.seed ? tp.seed : time_seed();
  for (int gen_i = 0; gen_i < tp.generations; ++gen_i) {
    const double t0 = now_s();
    std::vector<double> sum(NL, 0.0);
    std::vector<u32> cnt(NL, 0);
    double all_sum = 0;
    double all_cnt = 0;
    std::vector<WinSample> wins;
    std::mutex mu;
    std::atomic<int> next{0};
    std::atomic<long> turns_total{0};
    auto worker = [&](int tid) {
      MoveGen gen(&lex, &leaves);
      std::vector<double> lsum(NL, 0.0);
      std::vector<u32> lcnt(NL, 0);
      double lall = 0, lallc = 0;
      std::vector<WinSample> lwins;
      long lturns = 0;
      while (true) {
        const int g = next.fetch_add(1);
        if (g >= tp.games) break;
        Rng rng(mix64(seed ^ ((u64)gen_i << 40) ^ (u64)g));
        Game G;
        G.reset(rng);
        std::vector<std::pair<int, WinSample>> recs;  // (player, sample)
        int turns = 0;
        while (!G.over && turns < 200) {
          const int p = G.turn;
          const Rack rack = G.rack[p];
          EvalCtx ctx;
          ctx.bag = G.bag.n;
          ctx.opp_face = G.bag.n == 0 ? G.rack[1 - p].face() : 0;
          ctx.allow_exchange = G.bag.n >= RACK_SIZE;
          const Move m = gen.generate_best(G.board, rack, ctx);
          WinSample ws;
          ws.spread = (float)(G.score[p] - G.score[1 - p]);
          ws.unseen = (u8)std::min(MAX_UNSEEN, G.bag.n + G.rack[1 - p].n);
          ws.won2 = 0;
          recs.push_back({p, ws});
          if (rack.n == RACK_SIZE && G.bag.n > 0) {
            // credit E to every distinct sub-multiset of the rack (1..6 tiles)
            const double E = m.equity;
            lall += E;
            lallc += 1;
            int dl[NLET], dc[NLET], nd = 0;
            for (int L = 0; L < NLET; ++L)
              if (rack.c[L]) {
                dl[nd] = L;
                dc[nd] = rack.c[L];
                ++nd;
              }
            int k[NLET] = {0};
            while (true) {
              int i = 0;
              while (i < nd) {
                if (k[i] < dc[i]) {
                  ++k[i];
                  break;
                }
                k[i] = 0;
                ++i;
              }
              if (i == nd) break;
              int8_t c[NLET] = {0};
              int size = 0;
              for (int j = 0; j < nd; ++j) {
                c[dl[j]] = (int8_t)k[j];
                size += k[j];
              }
              if (size >= RACK_SIZE) continue;
              const int idx = index.find(LeaveTable::key_of(c));
              if (idx >= 0) {
                lsum[idx] += E;
                lcnt[idx]++;
              }
            }
          }
          G.apply(lex, m, rng);
          ++turns;
        }
        lturns += turns;
        for (auto& pr : recs) {
          const int p = pr.first;
          const int d = G.score[p] - G.score[1 - p];
          pr.second.won2 = d > 0 ? 2 : (d == 0 ? 1 : 0);
          lwins.push_back(pr.second);
        }
        if (tid == 0 && (g % 500) == 0)
          log << fmt("\r  generation %d: %d/%d games", gen_i + 1, g, tp.games) << std::flush;
      }
      std::lock_guard<std::mutex> lk(mu);
      for (size_t i = 0; i < NL; ++i) {
        sum[i] += lsum[i];
        cnt[i] += lcnt[i];
      }
      all_sum += lall;
      all_cnt += lallc;
      wins.insert(wins.end(), lwins.begin(), lwins.end());
      turns_total += lturns;
    };
    std::vector<std::thread> th;
    for (int t = 0; t < std::max(1, tp.threads); ++t) th.emplace_back(worker, t);
    for (auto& x : th) x.join();

    const double mean_all = all_cnt > 0 ? all_sum / all_cnt : 0;
    if (!tp.learn_leaves) {
      fit_win_model(wins, wm);
      log << fmt("\r  generation %d: %d games, %ld turns, %.0fs; win model refitted (leaves unchanged)\n", gen_i + 1, tp.games,
                 turns_total.load(), now_s() - t0);
      log << fmt("    win model: P(win | +20, 60 unseen) = %.1f%%   P(win | 0, 93 unseen, on turn) = %.1f%%\n",
                 100 * wm.win(20, 60), 100 * wm.win(0, 93));
      if (!tp.out.empty()) {
        wm.save(tp.out + ".win");
        log << "    saved " << tp.out << ".win\n";
      }
      continue;
    }
    // New leave values, smallest leaves first so larger ones can use them as priors.
    LeaveTable next_leaves = leaves;
    std::vector<std::vector<size_t>> by_size(7);
    for (size_t i = 0; i < NL; ++i) {
      int8_t c[NLET];
      LeaveTable::counts_of(index.key(i), c);
      int n = 0;
      for (int L = 0; L < NLET; ++L) n += c[L];
      by_size[n].push_back(i);
    }
    float single[NLET] = {0};
    size_t observed = 0;
    for (int size = 1; size <= 6; ++size) {
      for (size_t i : by_size[size]) {
        const u32 key = index.key(i);
        int8_t c[NLET];
        LeaveTable::counts_of(key, c);
        double prior;
        if (size == 1) {
          prior = leaves.get(key);
        } else {
          // mean over distinct x in L of V(L-x) + marginal(x | L-x) with pairwise synergies
          double acc = 0;
          int terms = 0;
          for (int x = 0; x < NLET; ++x) {
            if (!c[x]) continue;
            int8_t rest[NLET];
            std::memcpy(rest, c, NLET);
            rest[x]--;
            double marg = single[x];
            for (int y = 0; y < NLET; ++y) {
              if (!rest[y]) continue;
              int8_t pair[NLET] = {0};
              pair[x]++;
              pair[y]++;
              const double syn = next_leaves.get(LeaveTable::key_of(pair)) - single[x] - single[y];
              marg += syn * rest[y];
            }
            acc += next_leaves.get(LeaveTable::key_of(rest)) + marg;
            ++terms;
          }
          prior = acc / std::max(1, terms);
        }
        double v = prior;
        if (cnt[i] > 0) {
          const double obs = sum[i] / cnt[i] - mean_all;
          v = (cnt[i] * obs + tp.shrink * prior) / (cnt[i] + tp.shrink);
          ++observed;
        }
        next_leaves.set(key, (float)v);
        if (size == 1) {
          for (int L = 0; L < NLET; ++L)
            if (c[L]) single[L] = (float)v;
        }
      }
    }
    leaves = next_leaves;
    fit_win_model(wins, wm);
    const double secs = now_s() - t0;
    log << fmt("\r  generation %d: %d games, %ld turns, %.0fs (%.0f games/s); %zu/%zu leaves observed; mean rack equity %.2f\n",
               gen_i + 1, tp.games, turns_total.load(), secs, tp.games / std::max(1e-9, secs), observed, NL, mean_all);
    auto show = [&](const char* s) {
      Rack r;
      Rack::parse(s, r);
      return fmt("%s=%+.1f", s, leaves.value(r));
    };
    log << "    " << show("?") << ' ' << show("S") << ' ' << show("Z") << ' ' << show("X") << ' ' << show("E") << ' '
        << show("Q") << ' ' << show("U") << ' ' << show("V") << ' ' << show("QU") << ' ' << show("ERS") << ' ' << show("EEE")
        << ' ' << show("AEINST") << "\n";
    log << fmt("    win model: P(win | +20, 60 unseen) = %.1f%%   P(win | 0, 93 unseen, on turn) = %.1f%%\n",
               100 * wm.win(20, 60), 100 * wm.win(0, 93));
    if (!tp.out.empty()) {
      leaves.save(tp.out + ".leaves");
      wm.save(tp.out + ".win");
      log << "    saved " << tp.out << ".leaves and " << tp.out << ".win\n";
    }
  }
}

}  // namespace tf
namespace tf {

// =====================================================================================
// §18  CGP positions (Crossword Game Position, the "FEN" of Scrabble)
// =====================================================================================
//
//  <15 rows separated by '/'> <rack to move>/<other rack> <score to move>/<other score>
//  <consecutive zero-score turns> [lex NAME;]
//  Rows use digits for runs of empty squares; upper case = tile, lower case = blank.
//  Example:  15/15/15/15/15/15/15/7CAT5/15/15/15/15/15/15/15 AEINRST/ 0/7 0

inline std::string to_cgp(const Game& g, const std::string& lexname, bool hide_opponent_rack = false) {
  std::ostringstream o;
  for (int r = 0; r < N; ++r) {
    int run = 0;
    for (int c = 0; c < N; ++c) {
      const u8 t = g.board.sq[r * N + c];
      if (!t) {
        ++run;
        continue;
      }
      if (run) o << run;
      run = 0;
      o << tile_char(t);
    }
    if (run) o << run;
    if (r < N - 1) o << '/';
  }
  const int p = g.turn;
  o << ' ' << g.rack[p].str() << '/' << (hide_opponent_rack ? std::string() : g.rack[1 - p].str()) << ' ' << g.score[p] << '/'
    << g.score[1 - p] << ' ' << g.zeros;
  if (!lexname.empty()) o << " lex " << lexname << ';';
  return o.str();
}

inline bool from_cgp(const std::string& text, const Lexicon& lex, Game& g, Rng& rng, std::string& err) {
  std::vector<std::string> tok = split_ws(text);
  if (tok.size() < 3) {
    err = "CGP needs at least: board racks scores";
    return false;
  }
  Game ng;
  ng.board.clear();
  // board
  int r = 0, c = 0;
  const std::string& bs = tok[0];
  for (size_t i = 0; i < bs.size(); ++i) {
    const char ch = bs[i];
    if (ch == '/') {
      if (c != N) {
        err = "row " + std::to_string(r + 1) + " does not have 15 squares";
        return false;
      }
      ++r;
      c = 0;
      continue;
    }
    if (r >= N) {
      err = "too many rows";
      return false;
    }
    if (std::isdigit((unsigned char)ch)) {
      int k = ch - '0';
      while (i + 1 < bs.size() && std::isdigit((unsigned char)bs[i + 1])) k = k * 10 + (bs[++i] - '0');
      c += k;
    } else {
      const int L = char_to_rack(ch);
      if (L <= 0) {
        err = std::string("bad board character '") + ch + "'";
        return false;
      }
      if (c >= N) {
        err = "row too long";
        return false;
      }
      ng.board.sq[r * N + c] = (u8)(std::islower((unsigned char)ch) ? (L | BLANK_BIT) : L);
      ++c;
    }
    if (c > N) {
      err = "row too long";
      return false;
    }
  }
  if (r != N - 1 || c != N) {
    err = "board must have 15 rows of 15 squares";
    return false;
  }
  ng.board.recompute_all(lex);
  // racks
  const std::string& rs = tok[1];
  const size_t slash = rs.find('/');
  const std::string r1 = rs.substr(0, slash), r2 = slash == std::string::npos ? "" : rs.substr(slash + 1);
  Rack a, b;
  if (!Rack::parse(r1, a) || !Rack::parse(r2, b) || a.n > RACK_SIZE || b.n > RACK_SIZE) {
    err = "bad racks";
    return false;
  }
  // scores
  const std::string& ss = tok[2];
  const size_t s2 = ss.find('/');
  if (s2 == std::string::npos) {
    err = "scores must look like 123/456";
    return false;
  }
  ng.score[0] = std::atoi(ss.substr(0, s2).c_str());
  ng.score[1] = std::atoi(ss.substr(s2 + 1).c_str());
  ng.zeros = tok.size() > 3 && std::isdigit((unsigned char)tok[3][0]) ? std::atoi(tok[3].c_str()) : 0;
  // bag = everything else
  Rack bag = Rack::full_distribution();
  for (int s = 0; s < NSQ; ++s)
    if (ng.board.sq[s]) bag.sub(tile_rack_code(ng.board.sq[s]));
  for (int L = 0; L < NLET; ++L)
    if (bag.c[L] < 0) {
      err = std::string("too many ") + rack_char(L) + " on the board";
      return false;
    }
  if (!bag.contains(a)) {
    err = "rack " + r1 + " is not available (tiles already on the board)";
    return false;
  }
  bag.sub_all(a);
  if (!bag.contains(b)) {
    err = "second rack " + r2 + " is not available";
    return false;
  }
  bag.sub_all(b);
  ng.rack[0] = a;
  ng.rack[1] = b;
  // Unknown opponent rack: deal it from the bag.
  if (b.n == 0) fill_rack(ng.rack[1], bag, rng);
  if (a.n == 0) fill_rack(ng.rack[0], bag, rng);
  ng.bag = bag;
  ng.turn = 0;
  g = ng;
  return true;
}


// =====================================================================================
//  GCG game records (the standard format of Woogles, Quackle, cross-tables)
// =====================================================================================
//
//  >nick: RACK 8D WORD +SCORE TOTAL      play ('.' = tile played through)
//  >nick: RACK -ABC +0 TOTAL             exchange (or -7 when only the count is known)
//  >nick: RACK - +0 TOTAL                pass
//  >nick: RACK -- -SCORE TOTAL           phony withdrawn (undoes the previous play)
//  >nick: RACK (challenge) +5 TOTAL      bonuses / penalties / end-of-game rack points

struct GcgEvent {
  int player = 0;
  Rack rack;
  bool rack_known = false;
  std::string text;  // original move text
  Move move;         // PLACE / EXCHANGE / PASS; type 255 = score-only line
  bool withdrawn = false;
  int total = 0;
};

inline bool load_gcg_stream(std::istream& in, const std::string& path, const Lexicon& lex, int stop_at, Rng& rng,
                            Game& out, std::vector<GcgEvent>& events, std::string& err) {
  std::map<std::string, int> who;
  std::string line;
  events.clear();
  while (std::getline(in, line)) {
    line = trim(line);
    if (line.empty()) continue;
    if (starts_with(line, "#player1") || starts_with(line, "#player2")) {
      auto t = split_ws(line);
      if (t.size() >= 2) who[t[1]] = line[7] == '1' ? 0 : 1;
      continue;
    }
    if (line[0] != '>') continue;
    const size_t colon = line.find(':');
    if (colon == std::string::npos) continue;
    const std::string nick = line.substr(1, colon - 1);
    if (!who.count(nick)) who[nick] = (int)who.size() > 1 ? 1 : (int)who.size();
    std::vector<std::string> t = split_ws(line.substr(colon + 1));
    if (t.empty()) continue;
    GcgEvent e;
    e.player = who[nick];
    size_t i = 0;
    if (!t[0].empty() && t[0][0] != '(' && t[0][0] != '-' && !std::isdigit((unsigned char)t[0][0]) && t.size() >= 3) {
      e.rack_known = Rack::parse(t[0], e.rack) && e.rack.n <= RACK_SIZE;
      i = 1;
    }
    if (i >= t.size()) continue;
    e.total = std::atoi(t.back().c_str());
    const std::string a = t[i];
    e.move.type = 255;
    if (a == "--") {
      e.withdrawn = true;
    } else if (a == "-") {
      e.move = Move();
      e.move.type = MT_PASS;
      e.text = "pass";
    } else if (a.size() > 1 && a[0] == '-') {
      e.move = Move();
      e.move.type = MT_EXCHANGE;
      if (std::isdigit((unsigned char)a[1])) {
        e.move.len = (u8)std::atoi(a.c_str() + 1);  // count only
        e.move.ntiles = e.move.len;
        e.text = "exch " + a.substr(1);
      } else {
        for (size_t k = 1; k < a.size() && e.move.len < RACK_SIZE; ++k) {
          const int L = char_to_rack(a[k]);
          if (L >= 0) e.move.tiles[e.move.len++] = (u8)L;
        }
        e.move.ntiles = e.move.len;
        e.text = "exch " + a.substr(1);
      }
    } else if (a[0] != '(' && i + 1 < t.size()) {
      e.text = a + " " + t[i + 1];
      e.move.type = MT_PLACE;  // parsed against the board below
    } else {
      e.text = a;
    }
    events.push_back(e);
  }
  if (events.empty()) {
    err = "no moves found in " + path;
    return false;
  }
  // Replay.
  Game g;
  g.board.clear();
  g.bag = Rack::full_distribution();
  std::vector<Board> boards;
  const int n = (stop_at < 0 || stop_at > (int)events.size()) ? (int)events.size() : stop_at;
  for (int k = 0; k < n; ++k) {
    GcgEvent& e = events[k];
    if (e.withdrawn) {
      if (!boards.empty()) {
        g.board = boards.back();
        boards.pop_back();
      }
    } else if (e.move.type == MT_PLACE) {
      Move m;
      std::string perr;
      if (!parse_move(g.board, e.text, m, perr)) {
        err = "move " + std::to_string(k + 1) + " (" + e.text + "): " + perr;
        return false;
      }
      m.score = (i16)score_move(g.board, m);
      e.move = m;
      boards.push_back(g.board);
      g.board.place(lex, m);
    }
    if (e.move.type != 255 || e.withdrawn) {
      const bool scoring = e.move.type == MT_PLACE && e.move.score != 0 && !e.withdrawn;
      g.zeros = scoring ? 0 : g.zeros + 1;
    }
    g.score[e.player] = e.total;
  }
  // Position before event n: the mover and their rack (if known).
  int mover = n < (int)events.size() ? events[n].player : 1 - (events.back().player);
  g.turn = mover;
  Rack unseen = Rack::full_distribution();
  for (int s = 0; s < NSQ; ++s)
    if (g.board.sq[s]) unseen.sub(tile_rack_code(g.board.sq[s]));
  g.rack[0].clear();
  g.rack[1].clear();
  if (n < (int)events.size() && events[n].rack_known && unseen.contains(events[n].rack)) {
    g.rack[mover] = events[n].rack;
    unseen.sub_all(events[n].rack);
  } else {
    fill_rack(g.rack[mover], unseen, rng);
  }
  fill_rack(g.rack[1 - mover], unseen, rng);
  g.bag = unseen;
  g.board.recompute_all(lex);
  g.events.clear();
  g.has_last = false;
  out = g;
  return true;
}

inline bool load_gcg(const std::string& path, const Lexicon& lex, int stop_at, Rng& rng, Game& out,
                     std::vector<GcgEvent>& events, std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "cannot open " + path;
    return false;
  }
  return load_gcg_stream(in, path, lex, stop_at, rng, out, events, err);
}

inline std::string gcg_text(const Game& g, const std::string& lexname, const std::string& p1 = "player1",
                            const std::string& p2 = "player2") {
  std::ostringstream o;
  const std::string nick[2] = {p1, p2};
  o << "#character-encoding UTF-8\n#player1 " << p1 << ' ' << p1 << "\n#player2 " << p2 << ' ' << p2 << "\n";
  if (!lexname.empty()) o << "#lexicon " << lexname << "\n";
  int total[2] = {0, 0};
  for (const auto& e : g.events) {
    const Move& m = e.move;
    total[e.player] += m.type == MT_PLACE ? m.score : 0;
    o << '>' << nick[e.player] << ": " << e.rack_before.str() << ' ';
    if (m.type == MT_PLACE) {
      o << move_coord(m) << ' ' << move_word_dots(m) << " +" << m.score;
    } else if (m.type == MT_EXCHANGE) {
      o << '-' << m.used().str() << " +0";
    } else {
      o << "- +0";
    }
    o << ' ' << total[e.player] << "\n";
  }
  if (g.over && !g.events.empty()) {
    for (int p = 0; p < 2; ++p) {
      if (!g.end_bonus[p]) continue;
      total[p] += g.end_bonus[p];
      const int other_rack = g.end_bonus[p] > 0 ? 1 - p : p;
      o << '>' << nick[p] << ": (" << g.rack[other_rack].str() << ") " << (g.end_bonus[p] > 0 ? "+" : "") << g.end_bonus[p]
        << ' ' << total[p] << "\n";
    }
  }
  return o.str();
}

inline bool save_gcg(const Game& g, const std::string& path, const std::string& lexname, const std::string& p1 = "player1",
                     const std::string& p2 = "player2") {
  std::ofstream o(path);
  if (!o) return false;
  o << gcg_text(g, lexname, p1, p2);
  return (bool)o;
}

// =====================================================================================
// §19  Command-line interface
// =====================================================================================

struct App {
  Lexicon lex;
  LeaveTable leaves;
  WinModel wm;
  std::string leaves_src = "built-in model";
  std::string win_src = "built-in default";
  EngineConfig cfg;
  int threads = std::max(1u, std::thread::hardware_concurrency());
  Game game;
  Rng rng{time_seed()};
  bool color = false;
  bool quiet = false;  // protocol mode: no board echo after commands
  std::string pending_history;  // engine protocol: "history <GCG>" for the next position
  int exit_code = 0;   // non-zero after a failed selftest (for scripts and CI)
  std::unique_ptr<Engine> engine;

  App() {
    std::string e;
    EngineConfig::parse("champion", cfg, e);
    cfg.threads = threads;
  }

  Engine& eng() {
    if (!engine) engine.reset(new Engine(&lex, &leaves, &wm));
    return *engine;
  }
  bool need_lex() {
    if (lex.loaded()) return true;
    std::cout << "No lexicon loaded. Use: lexicon FILE   (a word list, one word per line)\n";
    return false;
  }

  bool load_lexicon(const std::string& path, bool auto_data) {
    std::string err;
    const double t0 = now_s();
    Lexicon L;
    const std::string low = to_lower(path);
    const bool kwg = low.size() > 4 && low.compare(low.size() - 4, 4, ".kwg") == 0;
    if (!(kwg ? L.load_kwg(path, err) : L.load_word_list(path, err))) {
      std::cout << "error: " << err << "\n";
      return false;
    }
    lex = std::move(L);
    engine.reset();
    std::cout << fmt("lexicon %s: %zu words, %zu graph nodes, %s in %.1fs\n", lex.name.c_str(), lex.nwords, lex.nodes.size(),
                     kwg ? "loaded" : "built", now_s() - t0);
    if (auto_data) {
      std::string stem = path;
      const size_t dot = stem.find_last_of('.');
      const size_t slash = stem.find_last_of("/\\");
      if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) stem = stem.substr(0, dot);
      // Leave values: our own trained text file first, then KLV2/KLV (wolges, MAGPIE).
      for (const char* ext : {".leaves", ".klv2", ".klv"}) {
        std::ifstream f(stem + ext);
        if (f) {
          load_leaves(stem + ext);
          break;
        }
      }
      std::ifstream f2(stem + ".win");
      if (f2) load_win(stem + ".win");
    }
    game.reset(rng);
    return true;
  }
  bool load_leaves(const std::string& path) {
    std::string err;
    size_t n = 0;
    LeaveTable t;
    if (!t.load(path, err, &n)) {
      std::cout << "error: " << err << "\n";
      return false;
    }
    leaves = std::move(t);
    engine.reset();
    leaves_src = path;
    std::cout << "leave values: " << n << " entries from " << path << "\n";
    return true;
  }
  bool load_win(const std::string& path) {
    std::string err;
    WinModel w;
    if (!w.load(path, err)) {
      std::cout << "error: " << err << "\n";
      return false;
    }
    wm = w;
    engine.reset();
    win_src = path;
    std::cout << "win model from " << path << "\n";
    return true;
  }

  void show_position() {
    if (quiet) return;
    std::cout << game.board.ascii(color);
    const int p = game.turn;
    std::cout << fmt("Player %d to move.  Score: P1 %d - P2 %d   Bag: %d   Zero-turns: %d\n", p + 1, game.score[0],
                     game.score[1], game.bag.n, game.zeros);
    std::cout << "Rack (P" << (p + 1) << "): " << game.rack[p].str() << "\n";
    if (game.over) std::cout << "Game over.\n";
  }

  void print_help() {
    std::cout << R"(Tilefish commands
  Setup
    lexicon FILE          load a word list or a .kwg lexicon (also loads FILE.leaves or
                          FILE.klv2, and FILE.win, if present)
    savewords FILE        write the word list as text, one word a line (e.g. from a .kwg)
    savekwg FILE          write the compiled lexicon as a .kwg (much faster to load)
    leaves FILE          load leave values        saveleaves FILE   save them
                          (text "LEAVE value" lines, or binary .klv/.klv2 as used by wolges/Macondo)
    win FILE              load win model           savewin FILE      save it
    threads N             worker threads (now )" << threads << R"()
    player SPEC           engine settings, e.g. champion, sim, static, champion:time=20
    color on|off          ANSI colours in the board display
  Play
    play [SPEC] [first|second]    play a game against the engine in this terminal
  Analysis (the current position)
    new                   new random game             board      show the position
    cgp CGP               set a position (see README) showcgp    print it as CGP
    gcg FILE [N]          load a game record (Woogles/Quackle .gcg); N = before move N
    savegcg FILE          save the current game (e.g. after `play`) as .gcg
    review FILE [SECS]    game review: every move vs the engine's choice (win% and spread lost)
    rack LETTERS          set the rack of the player to move ('?' = blank)
    move MOVE             play a move: 8D WORD (across), D8 WORD (down), exch ABC, pass
    gen [N]               top N moves by static equity
    sim [SECS]            Monte-Carlo simulation of the top candidates
    endgame [SECS]        solve the endgame (bag must be empty)
    peg [SECS]            pre-endgame solver (1 tile in the bag)
    go [SECS] [json]      what the engine would play here, with its analysis
                          (json: one machine-readable line, for GUIs and broadcasts)
    position cgp CGP      engine protocol: set a position ...
    go movetime MS        ... and answer "bestmove <move>" (see tools/referee.py)
    history GCG           (optional, before position) the game so far, GCG lines joined by " | ",
                          so the engine can infer the opponent's rack from their last play
    ui new|move|bot|hint|state|undo|review ...   a game for a graphical front-end, in JSON (web/)
    auto [N]              let the engine play the next N moves (either side)
    unseen                tiles you cannot see (bag + opponent rack)
    history               moves so far
  Engine development
    autoplay N A B [threads=T] [seed=S]   match between engine configs A and B
    train [games=N] [gens=G] [threads=T] [out=NAME]   self-play training of leaves + win model
    selftest [quick]      correctness checks (move generator vs brute force etc.)
    verifybest [N]        fast best-move search vs full generation on N positions
    verifyendgame [N]     endgame move source and values vs full generation/minimax
    bench                 speed benchmarks
    benchgen [G] [R]      best-move generation speed on fixed positions
    benchsim [SECS [THREADS [ITERS]]]  simulation throughput (fixed ITERS: deterministic)
    benchendgame [N] [S]  N self-play endgames, S seconds each
    quit
Player SPECs: static (no search), static+ (static + endgame solvers), sim (fast search),
champion (full strength).  Options: time=S iters=N plies=N cands=N threads=N win=0|1
eg=0|1 peg=0|1 inf=0|1 z=Z tau=T playout=N latecands=N   e.g.  champion:time=30,plies=3
)";
  }

  // ------------------------------------------------------------------------------------
  void cmd_gen(int n) {
    if (!need_lex()) return;
    Position P = Position::from_game(game);
    MoveGen gen(&lex, &leaves);
    std::vector<Move> mv;
    gen.generate_all(P.board, P.rack, Simulator::ctx_for(P), mv);
    sort_by_equity(mv);
    std::cout << fmt("%zu moves.  Rack %s, bag %d\n", mv.size(), P.rack.str().c_str(), P.bag_n);
    for (int i = 0; i < n && i < (int)mv.size(); ++i) {
      Rack leave = P.rack;
      if (mv[i].type != MT_PASS) leave.sub_all(mv[i].used());
      std::cout << fmt("%3d. %-26s score %3d  leave %-7s equity %6.1f\n", i + 1, move_str(P.board, mv[i]).c_str(), mv[i].score,
                       leave.str().c_str(), mv[i].equity);
    }
  }

  static std::string json_str(const std::string& x) {
    std::string o = "\"";
    for (char c : x) {
      if (c == '\n') o += "\\n";
      else if (c == '\r') o += "\\r";
      else if (c == '\t') o += "\\t";
      else if ((unsigned char)c < 0x20) o += fmt("\\u%04x", (unsigned)c);
      else {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
      }
    }
    return o + "\"";
  }
  static std::string json_num(double v, int prec = 3) {
    if (std::isnan(v)) return "null";
    std::ostringstream o;
    o << std::fixed << std::setprecision(prec) << v;
    return o.str();
  }
  // One-line JSON description of an analysis (for GUIs, broadcast overlays, scripts).
  std::string decision_json(const Position& P, const Decision& D) {
    std::ostringstream o;
    o << "{\"position\":" << json_str(to_cgp(game, lex.name, true)) << ",\"method\":" << json_str(D.method)
      << ",\"exact\":" << (D.exact ? "true" : "false") << ",\"seconds\":" << json_num(D.seconds, 2)
      << ",\"best\":" << json_str(move_str(P.board, D.move)) << ",\"moves\":[";
    for (size_t i = 0; i < D.rows.size(); ++i) {
      const auto& r = D.rows[i];
      Rack leave = P.rack;
      if (r.move.type != MT_PASS) leave.sub_all(r.move.used());
      if (i) o << ',';
      o << "{\"move\":" << json_str(move_str(P.board, r.move)) << ",\"score\":" << (r.move.type == MT_PLACE ? r.move.score : 0)
        << ",\"leave\":" << json_str(leave.str()) << ",\"static\":" << json_num(r.static_eq, 2)
        << ",\"value\":" << json_num(r.value, 2) << ",\"win\":" << json_num(r.win, 4)
        << ",\"iterations\":" << r.iterations << ",\"pruned\":" << (r.pruned ? "true" : "false") << '}';
    }
    o << "]}";
    return o.str();
  }

  void cmd_go(double secs, bool verbose, bool json = false) {
    if (!need_lex()) return;
    if (game.over) {
      std::cout << "The game is over.\n";
      return;
    }
    Position P = Position::from_game(game);
    EngineConfig c = cfg;
    c.threads = threads;
    c.sim.threads = threads;
    if (secs > 0) {
      c.sim.time_limit = secs;
      c.endgame_time = secs;
      c.peg_time = secs;
    }
    Decision D = eng().choose(P, c, verbose && !json);
    if (json) {
      std::cout << decision_json(P, D) << std::endl;
      return;
    }
    for (const auto& line : D.report) std::cout << line << "\n";
    std::cout << "best: " << move_str(P.board, D.move) << (D.move.type == MT_PLACE ? fmt("  (%d)", D.move.score) : "")
              << "   [" << D.method << "]\n";
  }

  // Engine protocol: answers "bestmove <move>" (see tools/referee.py).
  // Engine protocol: after "position cgp", use the referee's history (if one came first) to
  // record the opponent's last play and the board before it, which is what inference needs.
  // The history is used only if replaying it reproduces the CGP's board exactly and its last
  // move is the opponent's play; otherwise the position stands as the CGP alone.
  void attach_history() {
    if (pending_history.empty() || game.over) return;
    Rng scratch(1);  // the replay fills unknown racks; keep the engine's own stream untouched
    std::vector<GcgEvent> ev;
    std::string err;
    Game all;
    std::istringstream in(pending_history);
    if (!load_gcg_stream(in, "history", lex, -1, scratch, all, ev, err) || ev.empty()) return;
    const GcgEvent last = ev.back();
    if (last.withdrawn || last.move.type != MT_PLACE) return;
    for (int sq = 0; sq < NSQ; ++sq)
      if (all.board.sq[sq] != game.board.sq[sq]) return;
    std::vector<GcgEvent> ev2;
    Game before;
    std::istringstream in2(pending_history);
    if (!load_gcg_stream(in2, "history", lex, (int)ev.size() - 1, scratch, before, ev2, err)) return;
    game.has_last = true;
    game.board_before_last = before.board;
    game.last_move = last.move;
    GameEvent e;
    e.player = 1 - game.turn;
    e.move = last.move;
    e.score_after = game.score[1 - game.turn];
    game.events.push_back(e);
  }

  void cmd_go_protocol(double secs, const std::string& include = "") {
    if (!lex.loaded() || game.over) {
      std::cout << "bestmove pass" << std::endl;
      return;
    }
    Position P = Position::from_game(game);
    EngineConfig c = cfg;
    c.threads = threads;
    c.sim.threads = threads;
    c.include = include;
    if (secs > 0) {
      c.sim.time_limit = secs;
      c.endgame_time = secs;
      c.peg_time = secs;
    }
    Decision D = eng().choose(P, c, false);
    if (!D.info.empty()) std::cout << "info " << D.info << "\n";
    std::cout << "bestmove " << move_str(P.board, D.move) << std::endl;
  }

  void cmd_sim(double secs) {
    if (!need_lex()) return;
    Position P = Position::from_game(game);
    if (P.bag_n == 0) {
      std::cout << "Bag is empty: use `endgame` (exact) instead.\n";
    }
    SimParams sp = cfg.sim;
    sp.threads = threads;
    if (secs > 0) sp.time_limit = secs;
    Simulator& sim = eng().simulator();
    std::vector<Move> cands = sim.candidates(P, sp.max_candidates);
    OppModel opp;
    std::string note;
    if (cfg.inference) opp = eng().inference().infer(P, cfg.inf, &note);
    if (!note.empty()) std::cout << note << "\n";
    const SimResult R = sim.run(P, cands, sp, opp.empty() ? nullptr : &opp);
    std::cout << fmt("%d iterations, %d plies, %.1fs, %.0f positions/s\n", R.iterations, sp.plies, R.seconds,
                     R.positions / std::max(1e-9, R.seconds));
    std::cout << "   move                        static   sim-eq (+/-)     win%  (+/-)   iters\n";
    for (size_t i = 0; i < R.cands.size(); ++i) {
      const auto& c = R.cands[i];
      std::cout << fmt("%2zu. %-26s %6.1f  %+7.1f (%4.1f)  %5.1f%% (%4.1f)  %5d%s\n", i + 1, move_str(P.board, c.move).c_str(),
                       c.static_eq, c.mean_eq(), 1.96 * c.se_eq(), 100 * c.mean_win(), 196 * c.se_win(), c.n,
                       c.active ? "" : "  x");
    }
  }

  void cmd_endgame(double secs) {
    if (!need_lex()) return;
    Position P = Position::from_game(game);
    if (P.bag_n != 0) {
      std::cout << "The bag is not empty (" << P.bag_n << " tiles).\n";
      return;
    }
    EndgameParams ep;
    ep.time_limit = secs > 0 ? secs : cfg.endgame_time;
    ep.threads = threads;
    ep.verbose = true;
    std::cout << "Solving: " << P.rack.str() << " vs " << P.unseen.str() << " (spread " << P.spread() << ")\n";
    const EndgameResult R = eng().endgame().solve(P.board, P.rack, P.unseen, P.zeros, ep);
    std::cout << "best: " << move_str(P.board, R.best) << "  value " << std::showpos << R.value << std::noshowpos
              << "  final spread " << std::showpos << P.spread() + R.value << std::noshowpos
              << (R.solved ? "  (exact)" : "  (not proven: time limit)") << "\n";
    std::string line = "principal variation:";
    Board b = P.board;
    int side = 0;
    for (const auto& m : R.pv) {
      line += fmt("  %s%s", side ? "[opp] " : "", move_str(b, m).c_str());
      if (m.type == MT_PLACE) b.place(lex, m);
      side ^= 1;
    }
    std::cout << line << "\n";
    std::cout << "root moves:\n";
    for (size_t i = 0; i < std::min<size_t>(8, R.root.size()); ++i)
      std::cout << fmt("  %-26s %+d\n", move_str(P.board, R.root[i].first).c_str(), R.root[i].second);
  }

  void cmd_peg(double secs) {
    if (!need_lex()) return;
    Position P = Position::from_game(game);
    if (P.bag_n != 1) {
      std::cout << "The pre-endgame solver needs exactly 1 tile in the bag (there are " << P.bag_n << ").\n";
      return;
    }
    const PegResult R = eng().preendgame().solve(P, cfg.peg_candidates, secs > 0 ? secs : cfg.peg_time, threads, false);
    for (size_t i = 0; i < std::min<size_t>(12, R.rows.size()); ++i)
      std::cout << fmt("%2zu. %-26s win %5.1f%%  spread %+6.1f%s\n", i + 1, move_str(P.board, R.rows[i].move).c_str(),
                       100 * R.rows[i].win, R.rows[i].spread, R.rows[i].exact ? "" : "  (depth-limited)");
    std::cout << fmt("%.1fs\n", R.seconds);
  }

  bool human_move_ok(const std::string& text, Move& m, std::string& err) {
    if (!parse_move(game.board, text, m, err)) return false;
    const Rack& r = game.rack[game.turn];
    if (m.type == MT_EXCHANGE) {
      if (game.bag.n < RACK_SIZE) {
        err = "exchanges need at least 7 tiles in the bag";
        return false;
      }
      if (!r.contains(m.used())) {
        err = "you do not hold " + m.used().str();
        return false;
      }
      return true;
    }
    if (m.type == MT_PASS) return true;
    if (!r.contains(m.used())) {
      err = "you do not hold " + m.used().str() + " (use lower case for blanks)";
      return false;
    }
    std::vector<std::string> words;
    if (!validate_move(lex, game.board, m, err, &words)) return false;
    m.score = (i16)score_move(game.board, m);
    return true;
  }

  // ---- ui: a game against the engine driven by a graphical front-end (web/) ----
  // Every answer is one line of JSON.  The state is the human's view of the game: the
  // engine's rack appears only once the game is over.
  int ui_human = 0;
  double ui_win = -1;  // the human's winning chance after the engine's last move
  std::vector<Game> ui_before;  // the game before each move: takeback and review
  std::vector<Rng> ui_rng;      // ... and the tile-drawing generator, so draws repeat exactly

  void ui_apply(const Move& m) {
    ui_before.push_back(game);
    ui_rng.push_back(rng);
    game.apply(lex, m, rng);
  }

  static std::string board_json(const Board& b) {
    std::ostringstream o;
    o << "[";
    for (int r = 0; r < N; ++r) {
      std::string row;
      for (int c = 0; c < N; ++c) row += b.at(r, c) ? tile_char(b.at(r, c)) : '.';
      o << (r ? "," : "") << json_str(row);
    }
    o << "]";
    return o.str();
  }

  std::string ui_state() {
    const int h = ui_human;
    std::ostringstream o;
    o << "{\"board\":[";
    for (int r = 0; r < N; ++r) {
      std::string row;
      for (int c = 0; c < N; ++c) row += game.board.at(r, c) ? tile_char(game.board.at(r, c)) : '.';
      o << (r ? "," : "") << json_str(row);
    }
    o << "],\"rack\":" << json_str(game.rack[h].str()) << ",\"you\":" << game.score[h] << ",\"bot\":" << game.score[1 - h]
      << ",\"bag\":" << game.bag.n << ",\"yourTurn\":" << (!game.over && game.turn == h ? "true" : "false")
      << ",\"over\":" << (game.over ? "true" : "false")
      << ",\"unseen\":" << json_str(unseen_from(game.board, game.rack[h]).str()) << ",\"lexicon\":" << json_str(lex.name);
    if (game.over)
      o << ",\"botRack\":" << json_str(game.rack[1 - h].str()) << ",\"endYou\":" << game.end_bonus[h]
        << ",\"endBot\":" << game.end_bonus[1 - h];
    if (ui_win >= 0) o << ",\"win\":" << json_num(ui_win, 4);
    o << ",\"turn\":" << game.turn << ",\"seat\":" << h << ",\"moves\":" << game.events.size();
    o << ",\"history\":[";
    Board b;
    for (size_t i = 0; i < game.events.size(); ++i) {
      const auto& e = game.events[i];
      const bool mine = e.player == h;
      // The opponent's exchanged tiles are hidden, as at the board.
      const std::string text = e.move.type == MT_EXCHANGE && !mine ? fmt("exch %d", e.move.used().n) : move_str(b, e.move);
      o << (i ? "," : "") << "{\"who\":" << (mine ? "\"you\"" : "\"bot\"") << ",\"move\":" << json_str(text)
        << ",\"score\":" << (e.move.type == MT_PLACE ? e.move.score : 0) << ",\"total\":" << e.score_after
        << ",\"tiles\":" << (e.move.type == MT_PLACE ? (int)e.move.ntiles : 0)
        << ",\"type\":" << json_str(e.move.type == MT_PLACE ? "play" : e.move.type == MT_EXCHANGE ? "exchange" : "pass") << "}";
      if (e.move.type == MT_PLACE) b.place(lex, e.move);
    }
    o << "]}";
    return o.str();
  }

  //   ui new [first|second] [SEED]   new game (the human moves first or second)
  //   ui state                       the position as the human sees it
  //   ui check MOVE                  is MOVE legal for the human, and what it scores
  //   ui move MOVE                   the human plays MOVE
  //   ui bot [SPEC]                  the engine plays its move (SPEC as for `player`)
  //   ui hint [SECS]                 the engine's analysis of the human's position
  //   ui force MOVE                  MOVE for whoever is to move (replaying a saved game)
  //   ui seat 0|1                    whose view the state shows (two players at one screen)
  //   ui undo                        take back the last move, draws included
  //   ui gcg                         the game as a GCG record
  //   ui at N                        the board after the first N moves
  //   ui review N [SECS]             move N judged with what its player could see then
  //   ui record                      every move exactly (for saving; not for display)
  //   ui rewind N                    back to the position before move N, same draws ahead
  //   ui import FILE.gcg             a game record, for review (racks as recorded)
  //   ui cgp CGP / ui cgpout         set / show a position (the opponent's rack left out)
  void cmd_ui(const std::vector<std::string>& args, const std::string& rest) {
    const std::string sub = args.empty() ? "state" : to_lower(args[0]);
    auto fail = [](const std::string& e) { std::cout << "{\"ok\":false,\"error\":" << json_str(e) << "}" << std::endl; };
    if (!lex.loaded()) return fail("no word list loaded");
    if (sub == "new") {
      bool first = (rng.next() & 1) != 0;
      for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "first") first = true;
        else if (args[i] == "second") first = false;
        else rng.seed_with(std::strtoull(args[i].c_str(), nullptr, 10));
      }
      game.reset(rng);
      ui_human = first ? 0 : 1;
      ui_win = -1;
      ui_before.clear();
      ui_rng.clear();
    } else if (sub == "seat") {
      if (args.size() < 2 || (args[1] != "0" && args[1] != "1")) return fail("usage: ui seat 0|1");
      ui_human = args[1][0] - '0';
    } else if (sub == "record") {
      std::cout << "{\"ok\":true,\"moves\":[";
      Board b;
      for (size_t i = 0; i < game.events.size(); ++i) {
        const Move& m = game.events[i].move;
        std::cout << (i ? "," : "") << json_str(m.type == MT_EXCHANGE ? "exch " + m.used().str() : move_str(b, m));
        if (m.type == MT_PLACE) b.place(lex, m);
      }
      std::cout << "]}" << std::endl;
      return;
    } else if (sub == "rewind") {
      const size_t n = args.size() > 1 ? (size_t)std::max(0, std::atoi(args[1].c_str())) : 0;
      if (n >= ui_before.size()) return fail("no such move");
      game = ui_before[n];
      rng = ui_rng[n];
      ui_before.resize(n);
      ui_rng.resize(n);
      ui_win = -1;
    } else if (sub == "import") {
      const std::string path = args.size() > 1 ? trim(rest.substr(args[0].size())) : "";
      std::vector<GcgEvent> ev, ev2;
      std::string err;
      Game whole;
      if (path.empty() || !load_gcg(path, lex, -1, rng, whole, ev, err)) return fail(err.empty() ? "usage: ui import FILE" : err);
      std::vector<Game> before;
      std::vector<Rng> rngs;
      std::vector<GameEvent> events;
      size_t real = 0;
      for (size_t k = 0; k < ev.size(); ++k) {
        if (ev[k].withdrawn || ev[k].move.type == 255) continue;
        ++real;
        Game g;
        if (!load_gcg(path, lex, (int)k, rng, g, ev2, err)) break;
        // The move as played on the board before it (the record's own text, re-parsed).
        GameEvent e;
        e.player = ev[k].player;
        e.rack_before = ev[k].rack;
        e.score_after = ev[k].total;
        if (ev[k].move.type == MT_PLACE) {
          std::string perr;
          if (!parse_move(g.board, ev[k].text, e.move, perr)) break;
          e.move.score = (i16)score_move(g.board, e.move);
        } else {
          e.move = ev[k].move;
        }
        events.push_back(e);
        before.push_back(g);
        rngs.push_back(rng);
      }
      if (real == 0) return fail("the record has no moves");
      if (events.size() < real) return fail(fmt("move %zu of the record could not be read", events.size() + 1));
      // Words are not checked (a record may hold an unchallenged phony), tile counts are.
      {
        int count[NLET] = {};
        const Rack full = Rack::full_distribution();
        for (int sq = 0; sq < NSQ; ++sq)
          if (whole.board.sq[sq] && ++count[tile_rack_code(whole.board.sq[sq])] > full.c[tile_rack_code(whole.board.sq[sq])])
            return fail(std::string("impossible record: more ") +
                        (tile_rack_code(whole.board.sq[sq]) == BLANK ? std::string("blanks")
                                                                     : std::string(1, tile_char(whole.board.sq[sq] & 31))) +
                        " on the board than the set holds");
      }
      game = whole;
      game.events = events;
      ui_human = 0;
      ui_win = -1;
      // Review needs the position before every move; without it, the record is shown only.
      const bool aligned = before.size() == game.events.size();
      ui_before = aligned ? before : std::vector<Game>();
      ui_rng = aligned ? rngs : std::vector<Rng>();
    } else if (sub == "cgp") {
      std::string err;
      Game g;
      if (!from_cgp(trim(rest.substr(args[0].size())), lex, g, rng, err)) return fail(err);
      game = g;
      ui_human = game.turn;
      ui_win = -1;
      ui_before.clear();
      ui_rng.clear();
    } else if (sub == "cgpout") {
      std::cout << "{\"ok\":true,\"cgp\":" << json_str(to_cgp(game, lex.name, true)) << "}" << std::endl;
      return;
    } else if (sub == "undo") {
      if (ui_before.empty()) return fail("nothing to take back");
      game = ui_before.back();
      rng = ui_rng.back();
      ui_before.pop_back();
      ui_rng.pop_back();
      ui_win = -1;
    } else if (sub == "gcg") {
      std::cout << "{\"ok\":true,\"gcg\":" << json_str(gcg_text(game, lex.name, ui_human == 0 ? "you" : "tilefish",
                                                                ui_human == 0 ? "tilefish" : "you"))
                << "}" << std::endl;
      return;
    } else if (sub == "at") {
      const size_t n = args.size() > 1 ? (size_t)std::max(0, std::atoi(args[1].c_str())) : game.events.size();
      const Board& b = n < ui_before.size() ? ui_before[n].board : game.board;
      std::cout << "{\"ok\":true,\"board\":" << board_json(b) << "}" << std::endl;
      return;
    } else if (sub == "review") {
      const size_t n = args.size() > 1 ? (size_t)std::max(0, std::atoi(args[1].c_str())) : 0;
      if (n >= ui_before.size() || n >= game.events.size()) return fail("no such move");
      const double secs = args.size() > 2 ? std::max(0.1, std::atof(args[2].c_str())) : 1.0;
      const Position P = Position::from_game(ui_before[n]);  // the mover's view at that moment
      Move actual = game.events[n].move;
      if (actual.type == MT_PLACE) actual.score = (i16)score_move(P.board, actual);
      ReviewVerdict v;
      if (!review_position(P, actual, secs, v)) return fail("no verdict for this move");
      const bool same = v.best.same_as(actual);
      std::cout << "{\"ok\":true,\"n\":" << n << ",\"rack\":" << json_str(P.rack.str())
                << ",\"played\":" << json_str(move_str(P.board, actual)) << ",\"best\":" << json_str(move_str(P.board, v.best))
                << ",\"same\":" << (same ? "true" : "false") << ",\"winLoss\":" << json_num(v.win_loss, 4)
                << ",\"valueLoss\":" << json_num(v.eq_loss, 2) << ",\"bestWin\":" << json_num(v.best_win, 4)
                << ",\"method\":" << json_str(v.method) << ",\"exact\":" << (v.exact ? "true" : "false") << ",\"alts\":[";
      for (size_t k = 0; k < v.alts.size(); ++k)
        std::cout << (k ? "," : "") << "{\"move\":" << json_str(v.alts[k].move) << ",\"score\":" << v.alts[k].score
                  << ",\"win\":" << json_num(v.alts[k].win, 4) << ",\"value\":" << json_num(v.alts[k].value, 2) << "}";
      std::cout << "]}" << std::endl;
      return;
    } else if (sub == "move" || sub == "check" || sub == "force") {
      if (game.over) return fail("the game is over");
      if (sub != "force" && game.turn != ui_human) return fail("it is not your turn");
      Move m;
      std::string err;
      if (!human_move_ok(trim(rest.substr(args[0].size())), m, err)) return fail(err);
      if (sub == "check") {
        std::cout << "{\"ok\":true,\"score\":" << (m.type == MT_PLACE ? m.score : 0) << "}" << std::endl;
        return;
      }
      ui_apply(m);
    } else if (sub == "bot") {
      if (game.over) return fail("the game is over");
      if (game.turn == ui_human) return fail("it is your turn");
      EngineConfig ec = cfg;
      std::string err;
      if (args.size() > 1 && !EngineConfig::parse(args[1], ec, err)) return fail(err);
      ec.threads = ec.sim.threads = threads;
      const Position P = Position::from_game(game);
      const Decision D = eng().choose(P, ec, false);
      for (const auto& r : D.rows)
        if (r.move.same_as(D.move) && std::isfinite(r.win)) ui_win = 1.0 - r.win;
      ui_apply(D.move);
    } else if (sub == "hint") {
      if (game.over || game.turn != ui_human) return fail("a hint needs your turn");
      const double secs = args.size() > 1 ? std::max(0.1, std::atof(args[1].c_str())) : 2.0;
      EngineConfig c = cfg;
      c.threads = c.sim.threads = threads;
      c.sim.time_limit = c.endgame_time = c.peg_time = secs;
      const Position P = Position::from_game(game);
      const Decision D = eng().choose(P, c, false);
      std::cout << "{\"ok\":true,\"hint\":" << decision_json(P, D) << "}" << std::endl;
      return;
    } else if (sub != "state") {
      return fail("unknown ui command: " + sub);
    }
    std::cout << "{\"ok\":true,\"state\":" << ui_state() << "}" << std::endl;
  }

  // Makes the tiles in `want` the rack of the player to move (analysis helper).
  bool set_rack(const Rack& want, std::string& err) {
    const int p = game.turn;
    Rack pool = game.bag;
    pool.add_all(game.rack[p]);
    pool.add_all(game.rack[1 - p]);
    if (!pool.contains(want)) {
      err = "those tiles are not all unseen";
      return false;
    }
    game.bag.add_all(game.rack[p]);
    game.rack[p].clear();
    // Take from the bag first, then from the opponent (who redraws).
    Rack need = want;
    for (int L = 0; L < NLET; ++L) {
      const int from_bag = std::min<int>(need.c[L], game.bag.c[L]);
      game.bag.sub(L, from_bag);
      need.sub(L, from_bag);
    }
    for (int L = 0; L < NLET; ++L)
      if (need.c[L]) {
        game.rack[1 - p].sub(L, need.c[L]);
        need.sub(L, need.c[L]);
      }
    game.rack[p] = want;
    fill_rack(game.rack[1 - p], game.bag, rng);
    return true;
  }

  void cmd_move(const std::string& text) {
    if (!need_lex()) return;
    if (game.over) {
      std::cout << "The game is over.\n";
      return;
    }
    Move m;
    std::string err;
    if (!parse_move(game.board, text, m, err)) {
      std::cout << "error: " << err << "\n";
      return;
    }
    if (m.type == MT_PLACE) {
      std::vector<std::string> words;
      if (!validate_move(lex, game.board, m, err, &words)) {
        std::cout << "error: " << err << "\n";
        return;
      }
      m.score = (i16)score_move(game.board, m);
    }
    const Rack need = m.used();
    if (!game.rack[game.turn].contains(need)) {
      // Tracking a real game: make the rack consistent with the move if possible.
      Rack want = game.rack[game.turn];
      Rack add = need;
      for (int L = 0; L < NLET; ++L) add.c[L] = (int8_t)std::max(0, need.c[L] - want.c[L]);
      add.n = 0;
      for (int L = 0; L < NLET; ++L) add.n += add.c[L];
      // drop random other tiles to make room
      Rack keep = want;
      while (keep.n + add.n > RACK_SIZE) {
        for (int L = NLET - 1; L >= 0; --L)
          if (keep.c[L] > need.c[L]) {
            keep.sub(L);
            break;
          }
      }
      keep.add_all(add);
      if (!set_rack(keep, err)) {
        std::cout << "error: the tiles for this move are not available: " << err << "\n";
        return;
      }
    }
    std::cout << "P" << game.turn + 1 << " plays " << move_str(game.board, m)
              << (m.type == MT_PLACE ? fmt(" for %d", m.score) : "") << "\n";
    game.apply(lex, m, rng);
  }

  void cmd_play(const std::vector<std::string>& args) {
    if (!need_lex()) return;
    EngineConfig ec = cfg;
    bool human_first = (rng.next() & 1) != 0;
    for (const auto& a : args) {
      if (a == "first") human_first = true;
      else if (a == "second") human_first = false;
      else {
        std::string err;
        if (!EngineConfig::parse(a, ec, err)) {
          std::cout << "error: " << err << "\n";
          return;
        }
      }
    }
    ec.threads = threads;
    ec.sim.threads = threads;
    game.reset(rng);
    const int human = human_first ? 0 : 1;
    std::cout << "New game against Tilefish (" << ec.name << "). You are player " << human + 1
              << (human_first ? " and move first.\n" : " and move second.\n");
    std::cout << "Enter moves like  8D WORD  (across),  D8 WORD  (down),  exch QVU,  pass.\n"
                 "Lower case = blank.  Other commands: hint, unseen, board, resign.\n";
    std::string last;
    while (!game.over) {
      if (game.turn == human) {
        std::cout << "\n" << game.board.ascii(color);
        std::cout << fmt("You %d - %d Tilefish   Bag %d   %s\n", game.score[human], game.score[1 - human], game.bag.n,
                         last.c_str());
        std::cout << "Your rack: " << game.rack[human].str() << "\nyour move> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) return;
        line = trim(line);
        if (line.empty()) continue;
        const std::string low = to_lower(line);
        if (low == "quit" || low == "resign") {
          std::cout << "You resigned.\n";
          return;
        }
        if (low == "board") continue;
        if (low == "unseen") {
          Rack u = unseen_from(game.board, game.rack[human]);
          std::cout << "Unseen (" << u.n << "): " << u.str() << "\n";
          continue;
        }
        if (low == "hint") {
          cmd_go(std::min(5.0, ec.sim.time_limit), false);
          continue;
        }
        Move m;
        std::string err;
        if (!human_move_ok(line, m, err)) {
          std::cout << "Not accepted: " << err << "\n";
          continue;
        }
        // Feedback: how does the static evaluator rank the move?
        {
          Position P = Position::from_game(game);
          MoveGen gen(&lex, &leaves);
          std::vector<Move> mv;
          gen.generate_all(P.board, P.rack, Simulator::ctx_for(P), mv);
          sort_by_equity(mv);
          int rank = 0;
          for (size_t i = 0; i < mv.size(); ++i)
            if (mv[i].same_as(m)) {
              rank = (int)i + 1;
              break;
            }
          if (!mv.empty() && rank != 1)
            std::cout << fmt("  (static rank %d of %zu; top static move was %s)\n", rank, mv.size(),
                             move_str(P.board, mv[0]).c_str());
        }
        last = "You played " + move_str(game.board, m) + (m.type == MT_PLACE ? fmt(" for %d", m.score) : "");
        game.apply(lex, m, rng);
      } else {
        std::cout << "\nTilefish is thinking..." << std::flush;
        Position P = Position::from_game(game);
        Decision D = eng().choose(P, ec, false);
        std::cout << "\r";
        const std::string ms = move_str(game.board, D.move);
        last = "Tilefish played " + ms + (D.move.type == MT_PLACE ? fmt(" for %d", D.move.score) : "") + " [" + D.method + "]";
        std::cout << last << "\n";
        game.apply(lex, D.move, rng);
      }
    }
    std::cout << "\n" << game.board.ascii(color);
    std::cout << fmt("Final score: You %d - %d Tilefish.  %s\n", game.score[human], game.score[1 - human],
                     game.score[human] > game.score[1 - human]
                         ? "You win! Well played."
                         : (game.score[human] == game.score[1 - human] ? "A tie!" : "Tilefish wins."));
    std::cout << "Tilefish's last rack: " << game.rack[1 - human].str() << "\n";
  }

  void cmd_autoplay(const std::vector<std::string>& a) {
    if (!need_lex()) return;
    if (a.size() < 3) {
      std::cout << "usage: autoplay N PLAYER_A PLAYER_B [threads=T] [seed=S]\n";
      return;
    }
    const int n = std::max(1, std::atoi(a[0].c_str()));
    EngineConfig ca, cb;
    std::string err;
    if (!EngineConfig::parse(a[1], ca, err) || !EngineConfig::parse(a[2], cb, err)) {
      std::cout << "error: " << err << "\n";
      return;
    }
    int th = threads;
    u64 seed = time_seed();
    for (size_t i = 3; i < a.size(); ++i) {
      auto kv = parse_kv(a[i]);
      if (kv.count("threads")) th = std::atoi(kv["threads"].c_str());
      if (kv.count("seed")) seed = std::strtoull(kv["seed"].c_str(), nullptr, 10);
    }
    // Parallel games: each engine searches single-threaded.
    ca.threads = cb.threads = 1;
    ca.sim.threads = cb.sim.threads = 1;
    std::cout << "Match: " << ca.name << " vs " << cb.name << ", " << n << " games on " << th << " thread(s)...\n";
    const MatchResult R = run_match(lex, leaves, wm, ca, cb, n, th, seed, true);
    std::cout << R.summary(ca.name, cb.name) << "\n";
  }

  void cmd_train(const std::vector<std::string>& a) {
    if (!need_lex()) return;
    TrainParams tp;
    tp.threads = threads;
    tp.out = lex.name.empty() ? "trained" : lex.name;
    for (const auto& s : a) {
      auto kv = parse_kv(s);
      for (auto& p : kv) {
        if (p.first == "games") tp.games = std::atoi(p.second.c_str());
        else if (p.first == "gens") tp.generations = std::atoi(p.second.c_str());
        else if (p.first == "threads") tp.threads = std::atoi(p.second.c_str());
        else if (p.first == "out") tp.out = p.second;
        else if (p.first == "shrink") tp.shrink = std::atof(p.second.c_str());
        else if (p.first == "leaves") tp.learn_leaves = std::atoi(p.second.c_str()) != 0;
        else if (p.first == "seed") tp.seed = std::strtoull(p.second.c_str(), nullptr, 10);
        else if (p.first == "fresh") {
          leaves.init_default();
          wm.set_default();
        }
      }
    }
    train(lex, leaves, wm, tp, std::cout);
    engine.reset();
    leaves_src = tp.out + ".leaves";
    win_src = tp.out + ".win";
  }


  // Game review: for every move of a game record with a known rack, compare the move
  // played with the engine's choice (win-probability and spread lost).
  // How good was `actual` in position P, from the mover's point of view (only what they
  // could see)?  Exact in the endgame, the one-tile solver with one tile in the bag,
  // simulation (with `actual` among the candidates) otherwise.  False: no verdict.
  struct ReviewAlt {
    std::string move;
    int score;
    double win, value;
  };
  struct ReviewVerdict {
    Move best;
    double win_loss = 0, eq_loss = 0, best_win = 0.5;
    std::string method;
    bool exact = false;
    std::vector<ReviewAlt> alts;
  };
  bool review_position(const Position& P, Move actual, double secs, ReviewVerdict& v) {
    MoveGen gen(&lex, &leaves);
    const EvalCtx ctx = Simulator::ctx_for(P);
    actual.equity = gen.equity_of(actual, P.rack, ctx);
    Move best;
    double win_loss = 0, eq_loss = 0;
    double best_win = 0.5;  // win probability of the player to move with best play
    if (P.bag_n == 0) {
      // exact: solve before the move, and after the actual move
      EndgameParams ep;
      ep.time_limit = secs;
      ep.threads = threads;
      const EndgameResult er = eng().endgame().solve(P.board, P.rack, P.unseen, P.zeros, ep);
      best = er.best;
      int va;
      if (actual.type == MT_PLACE && actual.ntiles == P.rack.n) {
        va = actual.score + 2 * P.unseen.face();
      } else {
        Board nb = P.board;
        if (actual.type == MT_PLACE) nb.place(lex, actual);
        Rack mine = P.rack;
        if (actual.type == MT_PLACE) mine.sub_all(actual.used());
        const EndgameResult ea = eng().endgame().solve(nb, P.unseen, mine, (actual.type == MT_PLACE && actual.score) ? 0 : P.zeros + 1, ep);
        va = (actual.type == MT_PLACE ? actual.score : 0) - ea.value;
      }
      eq_loss = std::max(0, er.value - va);
      v.method = er.solved ? "endgame (solved)" : "endgame (best found)";
      v.exact = er.solved;
    v.method = er.solved ? "endgame (solved)" : "endgame (best found)";
    v.exact = er.solved;
      auto res = [&](int v) { const int f = P.spread() + v; return f > 0 ? 1.0 : (f == 0 ? 0.5 : 0.0); };
      win_loss = std::max(0.0, res(er.value) - res(va));
      best_win = res(er.value);
    } else if (P.bag_n == 1) {
      const PegResult pr = eng().preendgame().solve(P, cfg.peg_candidates, secs, threads, false, &actual);
      if (pr.rows.empty()) return false;
      best = pr.rows[0].move;
      best_win = pr.rows[0].win;
      v.method = "pre-endgame";
      for (size_t r = 0; r < pr.rows.size() && r < 5; ++r)
        v.alts.push_back({move_str(P.board, pr.rows[r].move), pr.rows[r].move.type == MT_PLACE ? pr.rows[r].move.score : 0,
                          pr.rows[r].win, pr.rows[r].spread});
      bool found = false;
      for (const auto& r : pr.rows)
        if (r.move.same_as(actual)) {
          found = true;
          win_loss = std::max(0.0, pr.rows[0].win - r.win);
          eq_loss = std::max(0.0, pr.rows[0].spread - r.spread);
        }
      if (!found && actual.type == MT_PLACE) return false;  // not evaluated in time: no verdict
    } else {
      Simulator& sim = eng().simulator();
      std::vector<Move> cands = sim.candidates(P, cfg.sim.max_candidates);
      bool found = false;
      for (const auto& m : cands) found |= m.same_as(actual);
      if (!found) cands.push_back(actual);
      SimParams sp = cfg.sim;
      sp.threads = threads;
      sp.time_limit = secs;
      const SimResult sr = sim.run(P, cands, sp);
      if (sr.cands.empty()) return false;
      best = sr.cands[0].move;
      best_win = sr.cands[0].mean_win();
      v.method = "simulation";
      for (size_t r = 0; r < sr.cands.size() && r < 5; ++r)
        v.alts.push_back({move_str(P.board, sr.cands[r].move), sr.cands[r].move.type == MT_PLACE ? sr.cands[r].move.score : 0,
                          sr.cands[r].mean_win(), sr.cands[r].mean_eq()});
      for (const auto& c : sr.cands)
        if (c.move.same_as(actual)) {
          win_loss = std::max(0.0, sr.cands[0].mean_win() - c.mean_win());
          eq_loss = std::max(0.0, sr.cands[0].mean_eq() - c.mean_eq());
        }
    }
    v.best = best;
    if (best.same_as(actual)) win_loss = eq_loss = 0;
    v.win_loss = win_loss;
    v.eq_loss = eq_loss;
    v.best_win = best_win;
    return true;
  }

  void cmd_review(const std::vector<std::string>& a) {
    if (!need_lex()) return;
    if (a.empty()) {
      std::cout << "usage: review FILE.gcg [SECONDS_PER_MOVE]\n";
      return;
    }
    const std::string path = a[0];
    const double secs = a.size() > 1 ? std::max(0.1, std::atof(a[1].c_str())) : 3.0;
    std::vector<GcgEvent> ev;
    std::string err;
    Game tmp;
    if (!load_gcg(path, lex, -1, rng, tmp, ev, err)) {
      std::cout << "error: " << err << "\n";
      return;
    }
    std::cout << fmt("Reviewing %s: %zu lines, %.1fs per move, %d thread(s)\n", path.c_str(), ev.size(), secs, threads);
    std::cout << "  #  P  rack      played                     score  engine prefers              win% lost  spread lost  P1 win%\n";
    double lost_win[2] = {0, 0}, lost_eq[2] = {0, 0};
    int moves[2] = {0, 0}, best_count[2] = {0, 0};
    for (size_t k = 0; k < ev.size(); ++k) {
      const GcgEvent& e = ev[k];
      if (e.withdrawn || e.move.type == 255 || !e.rack_known) continue;
      Game g;
      std::vector<GcgEvent> ev2;
      if (!load_gcg(path, lex, (int)k, rng, g, ev2, err)) break;
      if (g.over) break;
      Position P = Position::from_game(g);
      if (P.rack != e.rack) continue;  // rack inconsistent with the board
      Move actual;
      if (e.move.type == MT_PLACE) {
        if (!parse_move(P.board, e.text, actual, err)) continue;
        actual.score = (i16)score_move(P.board, actual);
      } else if (e.move.type == MT_EXCHANGE) {
        actual = e.move;
        if (!P.rack.contains(actual.used()) || actual.len == 0) {
          // count-only exchange ("-7"): can't evaluate which tiles were thrown
          continue;
        }
      } else {
        actual = Move();
      }
      if (actual.type != MT_PASS && !P.rack.contains(actual.used())) continue;
      ReviewVerdict v;
      if (!review_position(P, actual, secs, v)) continue;
      const Move best = v.best;
      double win_loss = v.win_loss, eq_loss = v.eq_loss;
      const double best_win = v.best_win;
      const int p = e.player;
      moves[p]++;
      if (best.same_as(actual)) {
        best_count[p]++;
        win_loss = eq_loss = 0;
      }
      lost_win[p] += win_loss;
      lost_eq[p] += eq_loss;
      const double p1_win = p == 0 ? best_win : 1.0 - best_win;
      std::cout << fmt("%3zu  %d  %-8s  %-26s %4d   %-26s %6.1f  %8.1f    %5.1f%s\n", k + 1, p + 1, P.rack.str().c_str(),
                       move_str(P.board, actual).c_str(), actual.type == MT_PLACE ? actual.score : 0,
                       best.same_as(actual) ? "=" : move_str(P.board, best).c_str(), 100 * win_loss, eq_loss, 100 * p1_win,
                       win_loss >= 0.05 ? "  ??" : (win_loss >= 0.02 ? "  ?" : ""))
                << std::flush;
    }
    for (int p = 0; p < 2; ++p)
      if (moves[p])
        std::cout << fmt("Player %d: %d moves, engine's choice %d times, total win%% lost %.1f, spread lost %.1f (%.1f per move)\n",
                         p + 1, moves[p], best_count[p], 100 * lost_win[p], lost_eq[p], lost_eq[p] / moves[p]);
    std::cout << "(? = cost 2-5% win probability, ?? = more than 5%)\n";
  }

  void cmd_selftest(bool quick);
  void cmd_bench();
  void cmd_benchgen(int games, int reps, const std::string& mode);
  void cmd_benchsim(double secs, int nthreads, int iters);
  void cmd_benchendgame(int n, double secs);
  int cmd_verifybest(int npos, bool quiet = false);
  int cmd_verifyendgame(int n, bool quiet = false);

  // Returns false on quit.
  bool execute(const std::string& raw) {
    const std::string line = trim(raw);
    if (line.empty() || line[0] == '#') return true;
    std::vector<std::string> t = split_ws(line);
    const std::string cmd = to_lower(t[0]);
    std::vector<std::string> args(t.begin() + 1, t.end());
    const std::string rest = trim(line.substr(t[0].size()));
    auto num = [&](size_t i, double def) { return i < args.size() ? std::atof(args[i].c_str()) : def; };
    if (cmd == "quit" || cmd == "exit" || cmd == "q") return false;
    if (cmd == "isready") {
      std::cout << "readyok" << std::endl;
      return true;
    }
    if (cmd == "help" || cmd == "?" || cmd == "h") print_help();
    else if (cmd == "lexicon" || cmd == "lex") {
      if (args.empty()) std::cout << (lex.loaded() ? "lexicon: " + lex.name + fmt(" (%zu words)\n", lex.nwords) : "no lexicon\n");
      else load_lexicon(rest, true);
    } else if (cmd == "leaves") {
      if (args.empty()) std::cout << "leave values: " << leaves_src << "\n";
      else load_leaves(rest);
    } else if (cmd == "savewords") {
      // The playable words (up to 15 letters), one a line: the plain list that
      // tools/referee.py checks moves against, from any lexicon including a .kwg.
      if (!need_lex()) return true;
      std::ofstream f(rest);
      size_t n = 0;
      if (f)
        lex.for_each_word([&](const std::string& w) {
          if (w.size() > (size_t)N) return;
          std::string text(w.size(), ' ');
          for (size_t i = 0; i < w.size(); ++i) text[i] = char('A' + (u8)w[i] - 1);
          f << text << '\n';
          ++n;
        });
      std::cout << (f ? fmt("saved %zu words\n", n) : std::string("error: cannot write\n"));
    } else if (cmd == "savekwg") {
      // The compiled graph as a .kwg, which loads much faster than a word list is built.
      if (!need_lex()) return true;
      std::cout << (lex.save_kwg(rest) ? "saved\n" : "error: cannot write\n");
    } else if (cmd == "saveleaves") {
      const std::string low = to_lower(rest);
      const bool klv = low.size() > 5 && low.compare(low.size() - 5, 5, ".klv2") == 0;
      std::cout << ((klv ? save_klv2(leaves, rest) : leaves.save(rest)) ? "saved\n" : "error: cannot write\n");
    } else if (cmd == "win") {
      if (args.empty()) std::cout << "win model: " << win_src << "\n";
      else load_win(rest);
    } else if (cmd == "savewin") {
      std::cout << (wm.save(rest) ? "saved\n" : "error: cannot write\n");
    } else if (cmd == "threads") {
      if (!args.empty()) threads = std::max(1, std::atoi(args[0].c_str()));
      cfg.threads = threads;
      std::cout << "threads: " << threads << "\n";
    } else if (cmd == "player" || cmd == "engine") {
      if (args.empty()) std::cout << "player: " << cfg.name << "\n";
      else {
        std::string err;
        EngineConfig c;
        if (!EngineConfig::parse(args[0], c, err)) std::cout << "error: " << err << "\n";
        else {
          cfg = c;
          cfg.threads = threads;
          std::cout << "player: " << cfg.name << "\n";
        }
      }
    } else if (cmd == "color" || cmd == "colour") {
      color = args.empty() || args[0] == "on";
    } else if (cmd == "new") {
      if (!args.empty()) rng.seed_with(std::strtoull(args[0].c_str(), nullptr, 10));
      game.reset(rng);
      show_position();
    } else if (cmd == "board" || cmd == "show") {
      show_position();
    } else if (cmd == "cgp") {
      if (!need_lex()) return true;
      std::string err;
      if (!from_cgp(rest, lex, game, rng, err)) std::cout << "error: " << err << "\n";
      else show_position();
    } else if (cmd == "gcg") {
      if (!need_lex()) return true;
      if (args.empty()) {
        std::cout << "usage: gcg FILE [N]   (N = position before the N-th move line, 1-based)\n";
        return true;
      }
      std::vector<GcgEvent> ev;
      std::string err;
      const int n = args.size() > 1 ? std::atoi(args[1].c_str()) - 1 : -1;
      if (!load_gcg(args[0], lex, n, rng, game, ev, err)) {
        std::cout << "error: " << err << "\n";
        return true;
      }
      if (args.size() == 1) {
        for (size_t k = 0; k < ev.size(); ++k)
          std::cout << fmt("%3zu. P%d %-8s %-20s %5d\n", k + 1, ev[k].player + 1, ev[k].rack_known ? ev[k].rack.str().c_str() : "?",
                           ev[k].withdrawn ? "(withdrawn)" : ev[k].text.c_str(), ev[k].total);
        std::cout << "Showing the final position; `gcg FILE N` sets up the position before move N.\n";
      }
      show_position();
    } else if (cmd == "review") {
      cmd_review(args);
    } else if (cmd == "savegcg") {
      if (args.empty()) std::cout << "usage: savegcg FILE\n";
      else std::cout << (save_gcg(game, args[0], lex.name) ? "saved " + args[0] + "\n" : "error: cannot write\n");
    } else if (cmd == "showcgp") {
      std::cout << to_cgp(game, lex.name) << "\n";
    } else if (cmd == "rack") {
      Rack r;
      std::string err;
      if (args.empty() || !Rack::parse(args[0], r) || r.n > RACK_SIZE) std::cout << "usage: rack LETTERS (up to 7, ? = blank)\n";
      else if (!set_rack(r, err)) std::cout << "error: " << err << "\n";
      else show_position();
    } else if (cmd == "move" || cmd == "m") {
      cmd_move(rest);
      show_position();
    } else if (cmd == "gen") {
      cmd_gen((int)num(0, 15));
    } else if (cmd == "sim") {
      cmd_sim(num(0, 0));
    } else if (cmd == "endgame" || cmd == "eg") {
      cmd_endgame(num(0, 0));
    } else if (cmd == "peg") {
      cmd_peg(num(0, 0));
    } else if (cmd == "position") {
      // Engine protocol:  position cgp <CGP>
      if (!need_lex()) return true;
      std::string err;
      if (args.empty() || to_lower(args[0]) != "cgp") std::cout << "error: expected position cgp <CGP>\n";
      else if (!from_cgp(trim(rest.substr(rest.find_first_of(" \t") == std::string::npos ? rest.size() : rest.find_first_of(" \t"))),
                         lex, game, rng, err))
        std::cout << "error: " << err << "\n";
      else {
        attach_history();
        show_position();
      }
      pending_history.clear();
    } else if (cmd == "go" || cmd == "best" || cmd == "analyze" || cmd == "analyse") {
      bool json = false, protocol = false;
      double secs = 0;
      std::string include;
      for (size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "json") json = true;
        else if (args[i] == "movetime" && i + 1 < args.size()) {
          secs = std::atof(args[++i].c_str()) / 1000.0;
          protocol = true;
        } else if (args[i] == "include") {  // analysis: also simulate this move
          for (size_t j = i + 1; j < args.size(); ++j) include += (j > i + 1 ? " " : "") + args[j];
          break;
        } else secs = std::atof(args[i].c_str());
      }
      if (protocol) cmd_go_protocol(secs, include);
      else cmd_go(secs, true, json);
    } else if (cmd == "auto") {
      if (!need_lex()) return true;
      const int n = args.empty() ? 1 : std::max(1, std::atoi(args[0].c_str()));
      EngineConfig c = cfg;
      c.threads = c.sim.threads = threads;
      for (int k = 0; k < n && !game.over; ++k) {
        Position P = Position::from_game(game);
        const Decision D = eng().choose(P, c, false);
        std::cout << fmt("P%d %-8s %-26s %4d  [%s]\n", game.turn + 1, game.rack[game.turn].str().c_str(),
                         move_str(game.board, D.move).c_str(), D.move.type == MT_PLACE ? D.move.score : 0, D.method.c_str());
        game.apply(lex, D.move, rng);
      }
      show_position();
    } else if (cmd == "unseen") {
      Rack u = unseen_from(game.board, game.rack[game.turn]);
      std::cout << "Unseen (" << u.n << "): " << u.str() << "\n";
    } else if (cmd == "history" && !args.empty()) {
      // Engine protocol: the game so far as GCG lines joined by " | ", for the next position.
      pending_history.clear();
      size_t a = 0;
      for (size_t b; (b = rest.find(" | ", a)) != std::string::npos; a = b + 3) pending_history += rest.substr(a, b - a) + "\n";
      pending_history += rest.substr(a) + "\n";
    } else if (cmd == "history") {
      Board b;
      for (size_t i = 0; i < game.events.size(); ++i) {
        const auto& e = game.events[i];
        std::cout << fmt("%3zu. P%d %-8s %-26s %4d\n", i + 1, e.player + 1, e.rack_before.str().c_str(),
                         move_str(b, e.move).c_str(), e.score_after);
        if (e.move.type == MT_PLACE) b.place(lex, e.move);
      }
    } else if (cmd == "ui") {
      cmd_ui(args, rest);
    } else if (cmd == "play") {
      cmd_play(args);
    } else if (cmd == "autoplay" || cmd == "match") {
      cmd_autoplay(args);
    } else if (cmd == "train") {
      cmd_train(args);
    } else if (cmd == "selftest") {
      cmd_selftest(!args.empty() && args[0] == "quick");
    } else if (cmd == "bench") {
      cmd_bench();
    } else if (cmd == "benchendgame") {
      cmd_benchendgame(args.empty() ? 20 : std::atoi(args[0].c_str()), args.size() > 1 ? std::atof(args[1].c_str()) : 10.0);
    } else if (cmd == "benchsim") {
      cmd_benchsim(args.empty() ? 3.0 : std::atof(args[0].c_str()), args.size() > 1 ? std::atoi(args[1].c_str()) : 1,
                   args.size() > 2 ? std::atoi(args[2].c_str()) : 0);
    } else if (cmd == "verifyendgame") {
      cmd_verifyendgame(args.empty() ? 30 : std::atoi(args[0].c_str()));
    } else if (cmd == "verifybest") {
      cmd_verifybest(args.empty() ? 200 : std::atoi(args[0].c_str()));
    } else if (cmd == "benchgen") {
      cmd_benchgen(args.empty() ? 30 : std::atoi(args[0].c_str()), args.size() > 1 ? std::atoi(args[1].c_str()) : 3,
                   args.size() > 2 ? args[2] : "");
    } else {
      std::cout << "unknown command '" << cmd << "' (type help)\n";
    }
    return true;
  }
};

}  // namespace tf
namespace tf {

// =====================================================================================
// §20  Self-tests and benchmarks
// =====================================================================================

// Every legal play for `rack`, found the slow way: try every word at every position.
inline std::set<std::string> brute_force_plays(const Lexicon& lex, const Board& b, const Rack& rack,
                                               const std::vector<std::vector<std::string>>& bylen) {
  auto key_of = [](const Move& m) {
    std::string k;
    if (m.ntiles == 1) {
      for (int i = 0; i < m.len; ++i)
        if (m.tiles[i]) return "S" + std::to_string(m.square(i)) + ":" + std::to_string(m.tiles[i]);
    }
    k = std::to_string(m.row) + "," + std::to_string(m.col) + "," + std::to_string(m.dir) + ":";
    for (int i = 0; i < m.len; ++i) k += std::to_string(m.tiles[i]) + ".";
    return k;
  };
  std::set<std::string> out;
  for (int d = 0; d < (b.empty() ? 1 : 2); ++d)
    for (int line = 0; line < N; ++line)
      for (int start = 0; start < N; ++start) {
        auto sqi = [&](int k) { return d == 0 ? line * N + k : k * N + line; };
        if (start > 0 && b.sq[sqi(start - 1)]) continue;
        for (int len = 2; start + len <= N; ++len) {
          const int end = start + len - 1;
          if (end < N - 1 && b.sq[sqi(end + 1)]) continue;
          for (const auto& w : bylen[len]) {
            bool ok = true, touch = false, center = false;
            int need[NLET] = {0}, placed = 0;
            for (int i = 0; i < len && ok; ++i) {
              const int s = sqi(start + i);
              if (b.sq[s]) ok = (b.sq[s] & 31) == (u8)w[i];
              else {
                need[(u8)w[i]]++;
                ++placed;
                touch |= b.has_neighbor(s);
                center |= s == CENTER;
              }
            }
            if (!ok || !placed || placed > rack.n || (b.empty() ? !center : !touch)) continue;
            int shortfall = 0;
            for (int L = 1; L < NLET; ++L) shortfall += std::max(0, need[L] - rack.c[L]);
            if (shortfall > rack.c[BLANK]) continue;
            Move m;
            m.type = MT_PLACE;
            m.dir = (u8)d;
            m.row = (u8)(d == 0 ? line : start);
            m.col = (u8)(d == 0 ? start : line);
            m.len = (u8)len;
            m.ntiles = (u8)placed;
            Rack rk = rack;
            std::function<void(int)> assign = [&](int i) {
              if (i == len) {
                std::string e;
                if (validate_move(lex, b, m, e)) out.insert(key_of(m) + "=" + std::to_string(score_move(b, m)));
                return;
              }
              const int s = sqi(start + i);
              if (b.sq[s]) {
                m.tiles[i] = 0;
                assign(i + 1);
                return;
              }
              const int L = (u8)w[i];
              if (rk.c[L] > 0) {
                rk.sub(L);
                m.tiles[i] = (u8)L;
                assign(i + 1);
                rk.add(L);
              }
              if (rk.c[BLANK] > 0) {
                rk.sub(BLANK);
                m.tiles[i] = (u8)(L | BLANK_BIT);
                assign(i + 1);
                rk.add(BLANK);
              }
            };
            assign(0);
          }
        }
      }
  return out;
}

inline int brute_endgame(const Lexicon& lex, const Board& b, Rack* r, int side, int passes, int zeros) {
  if (passes >= 2 || zeros >= 6) return r[side ^ 1].face() - r[side].face();
  MoveGen gen(&lex, nullptr);
  std::vector<Move> mv;
  EvalCtx ctx;
  ctx.bag = 0;
  ctx.allow_exchange = false;
  ctx.use_leaves = false;
  gen.generate_all(b, r[side], ctx, mv);
  int best = -100000;
  for (const auto& m : mv) {
    int v;
    if (m.type == MT_PLACE && m.ntiles == r[side].n) v = m.score + 2 * r[side ^ 1].face();
    else if (m.type == MT_PASS) v = -brute_endgame(lex, b, r, side ^ 1, passes + 1, zeros + 1);
    else {
      Board nb = b;
      nb.place(lex, m);
      Rack nr[2] = {r[0], r[1]};
      nr[side].sub_all(m.used());
      v = m.score - brute_endgame(lex, nb, nr, side ^ 1, 0, m.score == 0 ? zeros + 1 : 0);
    }
    best = std::max(best, v);
  }
  return best;
}

inline void App::cmd_selftest(bool quick) {
  if (!need_lex()) {
    exit_code = 1;  // nothing was tested
    return;
  }
  int failures = 0;
  auto check = [&](bool ok, const std::string& what) {
    std::cout << (ok ? "  ok    " : "  FAIL  ") << what << "\n" << std::flush;
    if (!ok) ++failures;
  };
  std::cout << "Self-test (" << (quick ? "quick" : "full") << ") with lexicon " << lex.name << "\n";
  // 1. Lexicon
  std::vector<std::vector<std::string>> bylen(N + 1);
  size_t nw = 0, bad = 0;
  lex.for_each_word([&](const std::string& w) {
    if (w.size() > (size_t)N) return;  // KWGs may hold longer, unplayable words
    ++nw;
    if (!lex.is_word((const u8*)w.data(), (int)w.size())) ++bad;
    bylen[w.size()].push_back(w);
  });
  check(nw == lex.nwords && bad == 0, fmt("lexicon: %zu words enumerated, all accepted", nw));
  {
    // every GADDAG path of a sample of words exists
    size_t tested = 0, missing = 0;
    for (size_t len = 2; len <= 8; ++len)
      for (size_t i = 0; i < bylen[len].size(); i += 97) {
        const std::string& w = bylen[len][i];
        for (size_t split = 1; split <= w.size(); ++split) {
          std::string g;
          for (size_t k = split; k-- > 0;) g += w[k];
          if (split < w.size()) {
            g += (char)SEP;
            g += w.substr(split);
          }
          u32 list = lex.gaddag_root, arc = 0;
          for (char ch : g) {
            arc = lex.find(list, (u8)ch);
            if (!arc) break;
            list = Lexicon::child(lex.nodes[arc]);
          }
          ++tested;
          if (!arc || !Lexicon::accepts(lex.nodes[arc])) ++missing;
        }
      }
    check(missing == 0, fmt("GADDAG: %zu sampled paths present", tested));
  }
  // 2. Positions from self-play: cross-checks, move generation, scoring, shadow pruning
  MoveGen gen(&lex, &leaves);
  Rng r(20240601);
  const int npos = quick ? 4 : 14;
  int xbad = 0, gbad = 0, sbad = 0, shbad = 0;
  long total = 0;
  for (int p = 0; p < npos; ++p) {
    Game g;
    g.reset(r);
    const int moves = (int)r.below(16);
    for (int k = 0; k < moves && !g.over; ++k) {
      Position P = Position::from_game(g);
      std::vector<Move> mv;
      gen.generate_all(P.board, P.rack, Simulator::ctx_for(P), mv);
      sort_by_equity(mv);
      const Move m = mv[r.below((u32)std::min<size_t>(3, mv.size()))];
      g.apply(lex, m, r);
    }
    Board full = g.board;
    full.recompute_all(lex);
    for (int d = 0; d < 2; ++d)
      for (int s = 0; s < NSQ; ++s)
        if (!g.board.sq[s] && (full.xchk[d][s] != g.board.xchk[d][s] || full.xsc[d][s] != g.board.xsc[d][s])) ++xbad;
    Rack test;
    {
      Rack pool = g.bag;
      pool.add_all(g.rack[0]);
      pool.add_all(g.rack[1]);
      while (test.n < RACK_SIZE && pool.n) test.add(draw_tile(pool, r));
      if (p % 2 == 0 && !test.c[BLANK] && test.n) {
        for (int L = 1; L < NLET; ++L)
          if (test.c[L]) {
            test.sub(L);
            test.add(BLANK);
            break;
          }
      }
    }
    std::vector<Move> mv;
    EvalCtx ctx;
    ctx.bag = 40;
    ctx.allow_exchange = false;
    ctx.add_pass = false;
    gen.generate_all(g.board, test, ctx, mv);
    std::set<std::string> got;
    for (const auto& m : mv) {
      std::string k;
      if (m.ntiles == 1) {
        for (int i = 0; i < m.len; ++i)
          if (m.tiles[i]) k = "S" + std::to_string(m.square(i)) + ":" + std::to_string(m.tiles[i]);
      } else {
        k = std::to_string(m.row) + "," + std::to_string(m.col) + "," + std::to_string(m.dir) + ":";
        for (int i = 0; i < m.len; ++i) k += std::to_string(m.tiles[i]) + ".";
      }
      got.insert(k + "=" + std::to_string(m.score));
      if (score_move(g.board, m) != m.score) ++sbad;
    }
    const std::set<std::string> want = brute_force_plays(lex, g.board, test, bylen);
    if (got != want || got.size() != mv.size()) ++gbad;
    total += (long)mv.size();
    // shadow-pruned best == best of the full list
    sort_by_equity(mv);
    const Move b = gen.generate_best(g.board, test, ctx);
    if (!mv.empty() && std::fabs(b.equity - mv[0].equity) > 1e-3) ++shbad;
    std::cout << fmt("\r  ...position %d/%d", p + 1, npos) << std::flush;
  }
  std::cout << "\r";
  check(xbad == 0, "incremental cross-checks match full recomputation");
  check(gbad == 0, fmt("move generator == brute force on %d positions (%ld plays, blanks included)", npos, total));
  check(sbad == 0, "move scores match independent scoring");
  check(shbad == 0, "shadow-pruned best move == best of full generation");
  // 3. Endgames vs brute-force minimax
  {
    int tested = 0, wrong = 0;
    for (int gi = 0; gi < (quick ? 12 : 40) && tested < (quick ? 3 : 10); ++gi) {
      Rng rr(777 + gi);
      Game g;
      g.reset(rr);
      while (!g.over && (g.bag.n > 0 || g.rack[0].n > 3 || g.rack[1].n > 3)) {
        Position P = Position::from_game(g);
        g.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), rr);
      }
      if (g.over) continue;
      Position P = Position::from_game(g);
      EndgameParams ep;
      ep.time_limit = 30;
      ep.tt_bits = 18;
      ep.threads = (tested % 2) ? 2 : 1;  // exercise the multi-threaded search too
      EndgameSolver es(&lex, 18);
      const EndgameResult er = es.solve(P.board, P.rack, P.unseen, P.zeros, ep);
      Rack rr2[2] = {P.rack, P.unseen};
      const int bv = brute_endgame(lex, P.board, rr2, 0, 0, P.zeros);
      ++tested;
      if (bv != er.value || !er.solved) ++wrong;
    }
    check(wrong == 0 && tested > 0, fmt("endgame solver == brute-force minimax on %d endgames", tested));
  }
  // 4. CGP round trip + notation
  {
    Rng rr(99);
    Game g;
    g.reset(rr);
    for (int k = 0; k < 6; ++k) {
      Position P = Position::from_game(g);
      g.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), rr);
    }
    const std::string c1 = to_cgp(g, lex.name);
    Game g2;
    std::string err;
    const bool ok = from_cgp(c1, lex, g2, rr, err);
    const std::string c2 = ok ? to_cgp(g2, lex.name) : "";
    check(ok && c1 == c2, "CGP round trip");
    int nbad = 0;
    Position P = Position::from_game(g);
    std::vector<Move> mv;
    gen.generate_all(P.board, P.rack, Simulator::ctx_for(P), mv);
    for (const auto& m : mv) {
      Move back;
      if (!parse_move(P.board, move_str(P.board, m), back, err) || !back.same_as(m)) ++nbad;
    }
    check(nbad == 0, fmt("notation round trip for %zu moves", mv.size()));
  }
  // 5. Fast best-move search (bounds + word maps) vs full generation, and the
  //    endgame solver's move source vs full generation along random lines.
  {
    const int np = quick ? 120 : 600;
    check(cmd_verifybest(np, true) == 0, fmt("fast best-move search == full generation on %d random positions", np));
    const int ne = quick ? 6 : 20;
    check(cmd_verifyendgame(ne, true) == 0, fmt("endgame move source == full generation, values == minimax (%d endgames)", ne));
  }
  // 6. The opponent's-rack prior draws racks with the probabilities it defines: exact
  //    letter means by enumeration of every rack, against the sampler's means.
  {
    Rack pool;
    for (char ch : std::string("??AEEIQSSUV")) pool.add(ch == '?' ? BLANK : ch - 'A' + 1);
    double odds[NLET];
    for (int L = 0; L < NLET; ++L) odds[L] = 1.0;
    odds[BLANK] = 1.6;
    odds['S' - 'A' + 1] = 1.2;
    odds['Q' - 'A' + 1] = 0.7;
    double worst = 0;
    for (int pass = 0; pass < 2; ++pass) {
      const double* w = pass == 0 ? odds : nullptr;
      double ones[NLET];
      for (int L = 0; L < NLET; ++L) ones[L] = 1.0;
      RackPrior rp;
      rp.build(pool, RACK_SIZE, w ? w : ones);
      // exact means: enumerate racks letter by letter
      double total = 0, mean[NLET] = {0};
      std::vector<int> letters;
      for (int L = 0; L < NLET; ++L)
        if (pool.c[L]) letters.push_back(L);
      std::function<void(size_t, int, double, std::array<int, NLET>&)> walk = [&](size_t i, int left, double wt,
                                                                                    std::array<int, NLET>& r) {
        if (i == letters.size()) {
          if (left) return;
          total += wt;
          for (int L = 0; L < NLET; ++L) mean[L] += wt * r[L];
          return;
        }
        const int L = letters[i];
        for (int j = 0; j <= std::min<int>(pool.c[L], left); ++j) {
          r[L] = j;
          walk(i + 1, left - j, wt * RackPrior::choose(pool.c[L], j) * std::pow(w ? w[L] : 1.0, j), r);
        }
        r[L] = 0;
      };
      std::array<int, NLET> r{};
      walk(0, RACK_SIZE, 1.0, r);
      Rng rr(7 + pass);
      const int ns = quick ? 40000 : 200000;
      double got[NLET] = {0};
      for (int s = 0; s < ns; ++s) {
        Rack x;
        rp.sample(rr, x);
        if (x.n != RACK_SIZE || !pool.contains(x)) worst = 1e9;
        for (int L = 0; L < NLET; ++L) got[L] += x.c[L];
      }
      for (int L = 0; L < NLET; ++L) worst = std::max(worst, std::fabs(got[L] / ns - mean[L] / total));
      if (!w)  // with equal odds the draw is uniform: mean = 7 * count / pool size
        for (int L = 0; L < NLET; ++L)
          worst = std::max(worst, std::fabs(mean[L] / total - (double)RACK_SIZE * pool.c[L] / pool.n));
    }
    check(worst < 0.02, fmt("opponent's-rack prior samples its exact distribution (worst letter mean off by %.4f)", worst));
  }
  // 7. Deeper simulations choose a legal move from the first stage's finalists.
  {
    EngineConfig ec;
    std::string err;
    const bool parsed = EngineConfig::parse("champion:deep=3,deepplies=4,deepfrac=0.5,time=0.3", ec, err);
    check(parsed && ec.sim.deep_k == 3 && ec.sim.deep_plies == 4 && ec.sim.deep_frac == 0.5,
          "two-stage simulation options parsed");
    int tested = 0, wrong = 0;
    if (parsed) {
      // Fixed iterations and seed let us reproduce the first-stage ranking exactly,
      // and keep this check quick even with a large lexicon.  The iteration cap, not the
      // clock, must end both stages: with 0.3 s a slow build (a sanitizer's, say) stopped
      // the first stage early and failed the check.
      ec.inference = false;
      ec.threads = ec.sim.threads = std::min(2, std::max(1, threads));
      ec.sim.time_limit = 600;
      ec.sim.max_iterations = 16;
      ec.sim.seed = 99;
      Rng rr(5);
      Game g;
      g.reset(rr);
      for (int turn = 0; turn < 10 && !g.over; ++turn) {
        Position P = Position::from_game(g);
        if (turn >= 5 && turn % 2 == 1 && P.bag_n > ec.sim.playout_bag) {
          const auto cands = eng().simulator().candidates(P, ec.sim.max_candidates);
          if ((int)cands.size() > ec.sim.deep_k) {
            SimParams sp = ec.sim;
            sp.time_limit *= sp.deep_frac;
            const SimResult sr = eng().simulator().run(P, cands, sp);
            const Decision D = eng().choose(P, ec);
            std::vector<Move> legal;
            gen.generate_all(P.board, P.rack, Simulator::ctx_for(P), legal);
            bool found = false, finalist = false;
            for (const auto& m : legal) found |= m.same_as(D.move);
            for (int i = 0; i < ec.sim.deep_k; ++i) finalist |= sr.cands[i].move.same_as(D.move);
            bool rows_ok = D.rows.size() == sr.cands.size();
            if (rows_ok)
              for (size_t i = 0; i < D.rows.size(); ++i) {
                if ((int)i < ec.sim.deep_k) {
                  bool in = false;
                  for (int k = 0; k < ec.sim.deep_k; ++k) in |= D.rows[i].move.same_as(sr.cands[k].move);
                  rows_ok &= in;
                } else {
                  rows_ok &= D.rows[i].move.same_as(sr.cands[i].move) && D.rows[i].pruned;
                }
              }
            if (!found || !finalist || !rows_ok || sr.iterations != sp.max_iterations ||
                D.info.find("\"deep\":{\"k\":3,\"plies\":4") == std::string::npos) ++wrong;
            ++tested;
          }
        }
        g.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), rr);
      }
    }
    check(wrong == 0 && tested == 3, fmt("two-stage simulation: legal finalist chosen, other rows pruned (%d positions)", tested));
  }
  // 8. Inference cut short by the clock weighs a fair sample of the opponent's possible
  //    leaves, not the first ones in letter order (which hold mostly blanks and A's).
  {
    Position P;
    Move om;
    std::string err;
    const bool parsed = parse_move(P.board, "8D TRAIN", om, err) && Rack::parse("EIOURST", P.rack);
    double worst = 1e9;
    size_t full_n = 0, cut_n = 0;
    bool tiny_empty = false;
    if (parsed) {
      om.score = score_move(P.board, om);
      P.board_before_opp = P.board;
      P.board.place(lex, om);
      P.opp_score = om.score;
      P.has_opp_last = true;
      P.opp_last = om;
      P.derive();
      // Share of the leaves weighed that hold each letter.  It depends only on which leaves
      // were weighed, not on their weights, so the check is the same for every lexicon.
      auto holds = [](const OppModel& M) {
        std::array<double, NLET> h{};
        for (const Rack& r : M.leaves)
          for (int L = 0; L < NLET; ++L)
            if (r.c[L]) h[L] += 1.0 / M.leaves.size();
        return h;
      };
      InferenceParams ip;
      ip.time_limit = 1e9;
      const OppModel full = eng().inference().infer(P, ip);
      full_n = full.leaves.size();
      ip.max_evals = (int)(full_n * 2 / 5);
      const OppModel cut = eng().inference().infer(P, ip);
      cut_n = cut.leaves.size();
      if (!full.empty() && !cut.empty()) {
        const auto a = holds(full), b = holds(cut);
        worst = 0;
        for (int L = 0; L < NLET; ++L) worst = std::max(worst, std::fabs(a[L] - b[L]));
      }
      ip.max_evals = 10;  // too few leaves weighed to trust: no model
      tiny_empty = eng().inference().infer(P, ip).empty();
    }
    check(parsed && full_n > 100 && cut_n == full_n * 2 / 5 && worst < 0.06 && tiny_empty,
          fmt("inference cut short weighs a fair sample (%zu of %zu leaves; worst letter off by %.3f)", cut_n, full_n, worst));
  }
  std::cout << (failures ? fmt("SELF-TEST FAILED (%d problem(s))\n", failures) : std::string("All self-tests passed.\n"));
  if (failures) exit_code = 1;
}

// Endgame checks: (1) the solver's move source equals full move generation along
// random lines of play; (2) solved values equal plain minimax on small endgames.
inline int App::cmd_verifyendgame(int n, bool quiet) {
  if (!need_lex()) return -1;
  MoveGen gen(&lex, &leaves);
  Rng r(4711);
  int src_bad = 0, val_bad = 0, tested = 0, small = 0;
  while (tested < n) {
    Game G;
    G.reset(r);
    while (!G.over && G.bag.n > 0) {
      Position P = Position::from_game(G);
      G.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), r);
    }
    if (G.over) continue;
    Position P = Position::from_game(G);
    EndgameSolver es(&lex, 18);
    src_bad += es.check_move_source(P.board, P.rack, P.unseen, r, 6);
    ++tested;
    // small endgames (few tiles left): exact value vs minimax
    Game H = G;
    while (!H.over && (H.rack[0].n > 3 || H.rack[1].n > 3)) {
      Position Q = Position::from_game(H);
      H.apply(lex, gen.generate_best(Q.board, Q.rack, Simulator::ctx_for(Q)), r);
    }
    if (H.over) continue;
    Position Q = Position::from_game(H);
    EndgameParams ep;
    ep.time_limit = 60;
    ep.tt_bits = 18;
    ep.threads = 1 + (small % 2);
    const EndgameResult er = es.solve(Q.board, Q.rack, Q.unseen, Q.zeros, ep);
    Rack rr[2] = {Q.rack, Q.unseen};
    const int bv = brute_endgame(lex, Q.board, rr, 0, 0, Q.zeros);
    ++small;
    if (bv != er.value || !er.solved) {
      ++val_bad;
      if (!quiet) std::cout << fmt("  value mismatch: %s vs %s: solver %d, minimax %d\n", Q.rack.str().c_str(), Q.unseen.str().c_str(), er.value, bv);
    }
  }
  if (!quiet)
    std::cout << fmt("verifyendgame: move source mismatches %d (in %d endgames); values: %d of %d small endgames wrong\n", src_bad,
                     tested, val_bad, small);
  return src_bad + val_bad;
}

// Checks that the fast best-move search (bounds, refinement, word maps) finds the
// same best equity as sorting the full move list, on random positions and racks
// (with and without blanks, in the midgame and with few tiles in the bag).
inline int App::cmd_verifybest(int npos, bool quiet) {
  if (!need_lex()) return -1;
  MoveGen fast(&lex, &leaves), full(&lex, &leaves);
  Rng r(123457);
  int bad = 0, done = 0;
  while (done < npos) {
    Game g;
    g.reset(r);
    const int moves = (int)r.below(22);
    for (int k = 0; k < moves && !g.over; ++k) {
      Position P = Position::from_game(g);
      g.apply(lex, full.generate_best(P.board, P.rack, Simulator::ctx_for(P)), r);
    }
    if (g.over) continue;
    for (int t = 0; t < 4 && done < npos; ++t, ++done) {
      // random rack from the unseen tiles; sometimes force one or two blanks
      Rack pool = g.bag;
      pool.add_all(g.rack[0]);
      pool.add_all(g.rack[1]);
      Rack rack;
      const int want = t == 3 ? 1 + (int)r.below(7) : RACK_SIZE;
      while (rack.n < want && pool.n) rack.add(draw_tile(pool, r));
      // t == 1: at least one blank (two on every other position)
      const int blanks_wanted = t == 1 ? 1 + (done / 4) % 2 : 0;
      while (rack.c[BLANK] < blanks_wanted && rack.n > rack.c[BLANK]) {
        for (int L = 1; L < NLET; ++L)
          if (rack.c[L]) {
            rack.sub(L);
            rack.add(BLANK);
            break;
          }
      }
      EvalCtx ctx;
      ctx.bag = t == 2 ? (int)r.below(8) : 40;
      ctx.opp_face = ctx.bag == 0 ? 10 : 0;
      ctx.allow_exchange = ctx.bag >= RACK_SIZE;
      std::vector<Move> all;
      full.generate_all(g.board, rack, ctx, all);
      float want_eq = -1e30f;
      for (const auto& m : all) want_eq = std::max(want_eq, m.equity);
      const Move b = fast.generate_best(g.board, rack, ctx);
      const bool legal = b.type != MT_PLACE || score_move(g.board, b) == b.score;
      if (std::fabs(b.equity - want_eq) > 1e-3 || !legal) {
        ++bad;
        if (!quiet && bad <= 10)
          std::cout << fmt("  mismatch: rack %s bag %d: fast %s (%.2f) vs full best %.2f%s\n", rack.str().c_str(), ctx.bag,
                           move_str(g.board, b).c_str(), b.equity, want_eq, legal ? "" : " [bad score]");
      }
    }
  }
  if (!quiet) std::cout << fmt("verifybest: %d positions, %d mismatches\n", npos, bad);
  return bad;
}

// Deterministic move-generation benchmark: positions from `games` static self-play
// games, best-move generation timed over `reps` passes.
inline void App::cmd_benchgen(int games, int reps, const std::string& mode) {
  if (!need_lex() || games <= 0 || reps <= 0) return;
  MoveGen gen(&lex, &leaves);
  gen.set_refine(mode != "norefine" && mode != "wmponly");
  gen.set_wmp(mode != "norefine" && mode != "nowmp");
  // The positions come from games played by the plain generator, so every variant
  // is timed on exactly the same positions.
  MoveGen ref(&lex, &leaves);
  ref.set_refine(false);
  ref.set_wmp(false);
  std::vector<std::pair<Board, Rack>> pos;
  std::vector<int> bags;
  Rng r(4242);
  for (int g = 0; g < games; ++g) {
    Game G;
    G.reset(r);
    while (!G.over && G.bag.n > 0) {
      Position P = Position::from_game(G);
      pos.push_back({P.board, P.rack});
      bags.push_back(P.bag_n);
      G.apply(lex, ref.generate_best(P.board, P.rack, Simulator::ctx_for(P)), r);
    }
  }
  double checksum = 0;
  const double t0 = now_s();
  for (int k = 0; k < reps; ++k)
    for (size_t i = 0; i < pos.size(); ++i) {
      EvalCtx ctx;
      ctx.bag = bags[i];
      ctx.allow_exchange = bags[i] >= RACK_SIZE;
      checksum += gen.generate_best(pos[i].first, pos[i].second, ctx).equity;
    }
  const double t = now_s() - t0;
  const bool wmp = mode != "norefine" && mode != "nowmp";  // word maps search spans, not anchors
  std::cout << fmt("best move: %.1f us/position over %zu positions x %d (checksum %.3f); %s searched %.1f of %.1f\n",
                   1e6 * t / (pos.size() * reps), pos.size(), reps, checksum / reps, wmp ? "spans" : "anchors",
                   (double)gen.anchors_searched / (pos.size() * reps), (double)gen.anchors_total / (pos.size() * reps));
}

// Endgame solver: n endgames from static self-play (bag empty, both racks full-ish),
// each solved with a time limit; reports how many were proven and how fast.  The games
// are played by the plain generator, so every build is timed on the same endgames (the
// fast search may break ties between equal plays differently).
inline void App::cmd_benchendgame(int n, double secs) {
  if (!need_lex() || n <= 0) return;
  MoveGen gen(&lex, &leaves);
  gen.set_refine(false);
  gen.set_wmp(false);
  Rng r(31337);
  int solved = 0, done = 0;
  double tsum = 0, tmax = 0;
  long nodes = 0;
  std::vector<double> times;
  while (done < n) {
    Game G;
    G.reset(r);
    while (!G.over && G.bag.n > 0) {
      Position P = Position::from_game(G);
      G.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), r);
    }
    if (G.over) continue;
    Position P = Position::from_game(G);
    EndgameSolver es(&lex, 22);
    EndgameParams ep;
    ep.time_limit = secs;
    ep.tt_bits = 22;
    ep.threads = threads;
    const EndgameResult er = es.solve(P.board, P.rack, P.unseen, P.zeros, ep);
    ++done;
    solved += er.solved;
    tsum += er.seconds;
    tmax = std::max(tmax, er.seconds);
    nodes += er.nodes;
    times.push_back(er.seconds);
    std::cout << fmt("  %2d. %-8s vs %-8s  %s %+4d  depth %2d  %8ld nodes  %6.2fs\n", done, P.rack.str().c_str(), P.unseen.str().c_str(),
                     er.solved ? "solved" : "open  ", er.value, er.depth, er.nodes, er.seconds);
  }
  std::sort(times.begin(), times.end());
  std::cout << fmt("endgames: %d/%d solved within %.1fs; mean %.2fs, median %.2fs, max %.2fs, %.0f nodes/s\n", solved, n, secs,
                   tsum / n, times[n / 2], tmax, nodes / std::max(1e-9, tsum));
}

// Simulation throughput on a fixed early-midgame position (single thread).
// With `iters` > 0 the run is a fixed number of iterations (deterministic, for
// profiling) instead of a fixed time.
inline void App::cmd_benchsim(double secs, int nthreads, int iters) {
  if (!need_lex()) return;
  MoveGen gen(&lex, &leaves);
  Game G;
  Rng rr(5);
  G.reset(rr);
  for (int k = 0; k < 5; ++k) {
    Position P = Position::from_game(G);
    G.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), rr);
  }
  Position P = Position::from_game(G);
  SimParams sp;
  sp.threads = std::max(1, nthreads);
  sp.time_limit = iters > 0 ? 1e9 : secs;
  sp.max_iterations = iters > 0 ? iters : 1000000;
  sp.prune_z = 100;
  sp.seed = 99;
  Simulator& sim = eng().simulator();
  const auto cands = sim.candidates(P, 10);
  const SimResult R = sim.run(P, cands, sp);
  double checksum = 0;  // with fixed iterations on one thread: identical across builds that play identically
  for (const auto& c : R.cands) checksum += c.mean_eq() + 100 * c.mean_win();
  std::cout << fmt("simulation (rack %s, bag %d, %d thread(s)): %.0f plies/s  (%d iterations x %zu candidates, %ld plies in %.1fs; checksum %.4f)\n",
                   P.rack.str().c_str(), P.bag_n, sp.threads, R.positions / R.seconds, R.iterations, cands.size(), R.positions,
                   R.seconds, checksum);
}

inline void App::cmd_bench() {
  if (!need_lex()) return;
  MoveGen gen(&lex, &leaves);
  std::vector<std::pair<Board, Rack>> pos;
  Rng r(4242);
  for (int g = 0; g < 30; ++g) {
    Game G;
    G.reset(r);
    while (!G.over && G.bag.n > 0) {
      Position P = Position::from_game(G);
      pos.push_back({P.board, P.rack});
      G.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), r);
    }
  }
  std::vector<Move> mv;
  mv.reserve(20000);
  double t0 = now_s();
  long moves = 0;
  for (auto& p : pos) {
    mv.clear();
    EvalCtx ctx;
    ctx.bag = 40;
    gen.generate_all(p.first, p.second, ctx, mv);
    moves += (long)mv.size();
  }
  const double t_all = now_s() - t0;
  t0 = now_s();
  for (auto& p : pos) {
    EvalCtx ctx;
    ctx.bag = 40;
    (void)gen.generate_best(p.first, p.second, ctx);
  }
  const double t_best = now_s() - t0;
  std::cout << fmt("move generation over %zu midgame positions:\n", pos.size());
  std::cout << fmt("  all moves : %7.1f us/position  (%.0f moves/position, %.2fM moves/s)\n", 1e6 * t_all / pos.size(),
                   (double)moves / pos.size(), moves / t_all / 1e6);
  std::cout << fmt("  best move : %7.1f us/position  (shadow pruning)\n", 1e6 * t_best / pos.size());
  // simulation throughput
  {
    Game G;
    Rng rr(5);
    G.reset(rr);
    for (int k = 0; k < 5; ++k) {
      Position P = Position::from_game(G);
      G.apply(lex, gen.generate_best(P.board, P.rack, Simulator::ctx_for(P)), rr);
    }
    Position P = Position::from_game(G);
    SimParams sp;
    sp.threads = threads;
    sp.time_limit = 3;
    sp.max_iterations = 100000;
    sp.prune_z = 100;  // no pruning: pure throughput
    Simulator& sim = eng().simulator();
    const auto cands = sim.candidates(P, 10);
    const SimResult R = sim.run(P, cands, sp);
    std::cout << fmt("  simulation: %.0f plies/s on %d thread(s)  (%d iterations x %zu candidates in %.1fs)\n",
                     R.positions / R.seconds, threads, R.iterations, cands.size(), R.seconds);
  }
}

}  // namespace tf

// =====================================================================================
// §21  main
// =====================================================================================

#ifdef __EMSCRIPTEN__
// Browser build (web/build.sh): the page's worker calls tf_run with ';'-separated
// commands and receives everything they printed.  One thread, no prompt.
extern "C" const char* tf_run(const char* commands) {
  static tf::App* app = nullptr;
  static std::string out;
  std::ostringstream sink;
  std::streambuf* saved = std::cout.rdbuf(sink.rdbuf());
  if (!app) {
    app = new tf::App();
    app->threads = app->cfg.threads = 1;
    app->quiet = true;
  }
  std::istringstream is(commands);
  std::string item;
  while (std::getline(is, item, ';')) app->execute(item);
  std::cout.rdbuf(saved);
  out = sink.str();
  return out.c_str();
}
#else
int main(int argc, char** argv) {
  using namespace tf;
  App app;
  std::vector<std::string> cmds;
  std::string lexpath, leavespath, winpath;
  bool quiet = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "--lexicon" || a == "-l") lexpath = next();
    else if (a == "--leaves") leavespath = next();
    else if (a == "--win") winpath = next();
    else if (a == "--threads" || a == "-t") app.threads = std::max(1, std::atoi(next().c_str()));
    else if (a == "--color" || a == "--colour") app.color = true;
    else if (a == "--quiet" || a == "-q") quiet = true;
    else if (a == "--help" || a == "-h") {
      std::cout << "usage: tilefish [--lexicon FILE(.txt|.kwg)] [--leaves FILE] [--win FILE] [--threads N] [--color] [--quiet] "
                   "[COMMAND...]\n"
                   "Without a command, starts the interactive prompt (type help).  Commands separated by ';'.\n"
                   "--quiet: no banner or prompt, for driving the engine from another program over stdin/stdout.\n";
      return 0;
    } else {
      std::string c;
      for (; i < argc; ++i) {
        if (!c.empty()) c += ' ';
        c += argv[i];
      }
      cmds.push_back(c);
    }
  }
  app.cfg.threads = app.threads;
  std::streambuf* saved = nullptr;
  std::ostringstream sink;
  if (quiet) saved = std::cout.rdbuf(sink.rdbuf());  // silence start-up messages
  std::cout << "Tilefish 2.2 - Scrabble engine (" << app.threads << " threads)\n";
  if (lexpath.empty()) {
    // A tournament lexicon placed in the folder wins over the bundled ENABLE list.  The
    // folder is the current one or else the program's own, since a program started by
    // double-clicking it (in the macOS Finder, say) may start in another folder.
    std::vector<std::string> dirs{""};
    const std::string self = argc > 0 ? argv[0] : "";
    const size_t slash = self.find_last_of("/\\");
    if (slash != std::string::npos) dirs.push_back(self.substr(0, slash + 1));
    for (const std::string& dir : dirs) {
      for (const char* cand : {"CSW24.kwg", "NWL23.kwg", "CSW24.txt", "NWL2023.txt", "CSW21.kwg", "NWL20.kwg", "CSW21.txt",
                               "NWL2020.txt", "lexicon.kwg", "lexicon.txt", "ENABLE.txt", "enable1.txt"}) {
        std::ifstream f(dir + cand);
        if (f) {
          lexpath = dir + cand;
          break;
        }
      }
      if (!lexpath.empty()) break;
    }
  }
  if (!lexpath.empty()) app.load_lexicon(lexpath, leavespath.empty() && winpath.empty());
  if (!leavespath.empty()) app.load_leaves(leavespath);
  if (!winpath.empty()) app.load_win(winpath);
  if (quiet) {
    app.quiet = true;
    std::cout.rdbuf(saved);
    if (!app.lex.loaded()) std::cout << "error: no lexicon loaded" << std::endl;
    else std::cout << "ready" << std::endl;
  }
  if (!cmds.empty()) {
    for (const auto& c : cmds) {
      std::string item;
      std::istringstream is(c);
      while (std::getline(is, item, ';'))
        if (!app.execute(item)) return app.exit_code;
    }
    return app.exit_code;
  }
  if (!quiet) std::cout << "Type 'help' for commands, 'play' to play a game.\n";
  std::string line;
  while (true) {
    if (!quiet) std::cout << "tilefish> " << std::flush;
    if (!std::getline(std::cin, line)) break;
    if (!app.execute(line)) break;
    if (quiet) std::cout << std::flush;
  }
  return 0;
}
#endif
