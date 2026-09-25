/*
 * =====================================================================================
 *   TILEFISH  -  a championship-style Scrabble(R) engine in one C++17 file
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
 *  columns such as definitions are ignored).  Tournament play uses CSW (Collins,
 *  WESPA / world championship) or NWL (NASPA, North America).  Those lists are
 *  copyrighted, so they are not bundled: load your own copy with --lexicon and run
 *  `train` so the engine learns leave values for that exact dictionary.
 *
 *  HOW THE ENGINE THINKS  (chess analogies in brackets)
 *    1. Move generation   GADDAG (Gordon 1994) compiled into a compact node array.
 *                         Generates every legal play, exchange and pass.  [movegen]
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
 *                         solutions.   [tablebase-like]
 *    5. Pre-endgame       One tile in the bag: every possible draw is enumerated
 *                         and each resulting endgame is solved.
 *    6. Inference         The opponent's last play tells us about the tiles they
 *                         kept; the sampler weights their possible racks.
 *    7. Self-play         `train` plays thousands of games against itself and
 *                         re-learns leave values + the win model. [AlphaZero-ish]
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
    std::string base = path;
    const size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos && dot > 0) base = base.substr(0, dot);
    name = base;
    return true;
  }

  // Builds from words encoded as bytes 1..26.  Sorts/deduplicates in place.
  bool build(std::vector<std::string>& words, std::string& err);

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
  float peg[16] = {0};         // bonus by tiles left in the bag after the move (pre-endgame)
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
    const int tp = (m.type == MT_PLACE) ? m.ntiles : 0;
    if (m.type == MT_PASS) {
      if (ctx.bag > 0) return (leave.n <= 6 && ctx.use_leaves ? leave_value(leave) : 0.f) - STATIC_PARAMS.pass_penalty;
      return -(STATIC_PARAMS.not_out_mult * (float)leave.face() + STATIC_PARAMS.not_out_const);
    }
    if (m.type == MT_EXCHANGE) return ctx.use_leaves ? leave_value(leave) : 0.f;
    if (ctx.bag > 0) {
      int after = ctx.bag - tp;
      if (after < 0) after = 0;
      return (float)m.score + (ctx.use_leaves ? leave_value(leave) : 0.f) + (after < 16 ? STATIC_PARAMS.peg[after] : 0.f);
    }
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
  };
  AnchorInfo anchors_[2 * NSQ];
  int ts_[RACK_SIZE];                 // rack tile scores, descending
  float best_rest_[RACK_SIZE + 1];    // best (leave + adjustment) part of equity by tiles played
  u32 rack_letters_ = 0;
  bool has_blank_ = false;
  bool use_shadow_ = true;
  bool bingo7_ = true;      // can the rack form a 7-letter word?
  u32 bingo8_ = ALL_LETTERS;  // board letters that complete an 8-letter word

 public:
  void set_shadow(bool on) { use_shadow_ = on; }
  long anchors_searched = 0, anchors_total = 0;

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

  inline float equity(int score, int tp, u32 m) {
    if (ctx_.bag > 0) {
      int after = ctx_.bag - tp;
      if (after < 0) after = 0;
      return (float)score + leave_val(m) + (after < 16 ? STATIC_PARAMS.peg[after] : 0.f);
    }
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
    if (ctx_.allow_exchange && ctx_.bag >= RACK_SIZE && nr_ > 0) gen_exchanges();
    if (can_play) {
      if (b.empty()) {
        load_line(0, 7);
        anchor_ = 7;
        last_anchor_ = -1;
        no_right_ = true;
        rec(7, lex_->gaddag_root, 0, 1, 0, 7);
      } else if (mode_ == GEN_BEST && use_shadow_) {
        run_best_shadow();
      } else {
        for (int s = 0; s < NSQ; ++s) is_anchor_[s] = !b.sq[s] && b.has_neighbor(s);
        for (int d = 0; d < 2; ++d)
          for (int line = 0; line < N; ++line) {
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
    } else if (best_eq_ <= -1e29f) {
      best_ = Move();
      best_.type = MT_PASS;
      best_.equity = pass_equity();
      best_eq_ = best_.equity;
    }
  }

  // --- Shadow pruning -----------------------------------------------------------------
  // For each anchor compute an upper bound on the equity of any play generated from it:
  // the highest rack tile scores are matched with the highest effective multipliers
  // (rearrangement inequality) for every feasible span, plus the best possible leave
  // for that many tiles played.  Anchors are then searched best-bound-first and the
  // search stops once no remaining anchor can beat the best play found.

  inline bool placeable(int k) const {
    const u32 x = lx_[k];
    return (x & rack_letters_) != 0 || (has_blank_ && x != 0);
  }

  void prepare_shadow() {
    int n = 0;
    for (int i = 0; i < nr_; ++i) ts_[n++] = TILE_SCORE[rt_[i]];
    std::sort(ts_, ts_ + n, [](int x, int y) { return x > y; });
    rack_letters_ = 0;
    for (int L = 1; L < NLET; ++L)
      if (rk_[L]) rack_letters_ |= 1u << L;
    has_blank_ = rk_[BLANK] > 0;
    bingo7_ = true;
    bingo8_ = ALL_LETTERS;
    if (nr_ == RACK_SIZE) lex_->bingo_info(rk_, bingo7_, bingo8_);
    for (int k = 0; k <= RACK_SIZE; ++k) best_rest_[k] = -1e30f;
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
          int after = ctx_.bag - played;
          if (after < 0) after = 0;
          rest = leave_val(m) + (after < 16 ? STATIC_PARAMS.peg[after] : 0.f);
        } else if (m == 0) {
          rest = (float)(2 * ctx_.opp_face);
        } else {
          leave_val(m);
          rest = -STATIC_PARAMS.not_out_mult * (float)lf_[m] - STATIC_PARAMS.not_out_const;
        }
        if (rest > best_rest_[played]) best_rest_[played] = rest;
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
  }

  // Highest-scoring rack tile that may be placed on square k of the loaded line.
  inline int square_cap(int k) const {
    const u32 x = lx_[k] & rack_letters_;
    int best = 0;
    for (int i = 0; i < nr_; ++i) {
      const int L = rt_[i];
      if (L && ((x >> L) & 1u) && TILE_SCORE[L] > best) best = TILE_SCORE[L];
    }
    return best;
  }

  // Upper bound for anchor a of the loaded line (last_anchor_ must be set).
  float shadow_bound(int a) const {
    float best = -1e30f;
    if (!placeable(a)) return best;
    int plm[N], pwm[N], pxs[N], pcap[N];  // placed squares: left part (from anchor leftwards), then right part
    int lk = 1;
    plm[0] = llm_[a];
    pwm[0] = lwm_[a];
    pxs[0] = lxs_[a];
    pcap[0] = square_cap(a);
    int l_through = 0, l_tc = 0, l_tl = 0;  // through-tile face sum, count, last letter
    int L = a;
    while (true) {
      if (L == 0 || lt_[L - 1] == 0) {
        int k = lk;
        int r_through = 0, r_tc = 0, r_tl = 0;
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
            int sorted_sum = 0;
            for (int j = 0; j < k; ++j) sorted_sum += ts_[j] * eff[j];
            const int sc = (l_through + r_through) * wmt + cross + (k == RACK_SIZE ? BINGO_BONUS : 0) +
                           std::min(sorted_sum, capped);
            const float v = (float)sc + best_rest_[k];
            if (v > best) best = v;
          }
          if (R == N - 1) break;
          const int nx = R + 1;
          if (lt_[nx]) {
            r_through += tile_face(lt_[nx]);
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
    return best;
  }

  void run_best_shadow() {
    const Board& b = *B_;
    prepare_shadow();
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
          if (bound > best_eq_) anchors_[na++] = {bound, (u8)d, (u8)line, (u8)k, (int8_t)last_anchor_};
          last_anchor_ = k;
        }
      }
    std::sort(anchors_, anchors_ + na, [](const AnchorInfo& x, const AnchorInfo& y) { return x.bound > y.bound; });
    anchors_total += na;
    int cur_d = -1, cur_line = -1;
    for (int i = 0; i < na; ++i) {
      const AnchorInfo& A = anchors_[i];
      if (A.bound <= best_eq_) break;
      if (A.dir != cur_d || A.line != cur_line) {
        load_line(A.dir, A.line);
        cur_d = A.dir;
        cur_line = A.line;
      }
      anchor_ = A.col;
      last_anchor_ = A.last;
      no_right_ = (A.col == N - 1) || lt_[A.col + 1] == 0;
      ++anchors_searched;
      rec(A.col, lex_->gaddag_root, 0, 1, 0, A.col);
    }
  }

  float pass_equity() {
    if (ctx_.bag > 0) return (nr_ <= 6 ? leave_val(full_mask_) : 0.f) - STATIC_PARAMS.pass_penalty;
    leave_val(full_mask_);
    return -(STATIC_PARAMS.not_out_mult * (float)lf_[full_mask_] + STATIC_PARAMS.not_out_const);
  }

  void rec(int col, u32 list, int lsum, int wmul, int xsum, int leftmost) {
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
    const float eq = equity(score, tp, mask_);
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
//  (paired z-test) are dropped early, focusing the remaining time on close decisions.

struct SimParams {
  int plies = 2;
  int playout_bag = 7;        // with this many tiles or fewer in the bag, play out to the end
  int max_candidates = 20;
  int max_iterations = 5000;  // per candidate
  double time_limit = 5.0;    // seconds
  int threads = 1;
  bool win_objective = true;      // rank by win probability (else by spread/equity)
  double equity_tiebreak = 0.0008;  // win-objective: + this much per point of equity
  double prune_z = 2.4;           // successive-elimination threshold
  int min_iterations = 96;        // before any pruning
  int batch = 48;                 // iterations between pruning checks
  double shrink_tau = 4.0;        // prior: static equity is right to within ~this many points
  u64 seed = 0;                   // 0 = random
  bool verbose = false;
};

struct SimCandidate {
  Move move;
  float static_eq = 0;
  std::vector<float> eq, win;
  int n = 0;
  bool active = true;
  double post = 0;  // posterior objective relative to the most-simulated candidate (ranking key)
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

  // Top candidates by static equity for position P.
  std::vector<Move> candidates(const Position& P, int max_n) const {
    MoveGen gen(lex_, lt_);
    std::vector<Move> all;
    EvalCtx ctx = ctx_for(P);
    gen.generate_all(P.board, P.rack, ctx, all);
    sort_by_equity(all);
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
    std::vector<std::unique_ptr<MoveGen>> gens;
    for (int t = 0; t < nthreads; ++t) gens.emplace_back(new MoveGen(lex_, lt_));
    std::atomic<long> positions{0};
    int iters = 0;
    int last_prune_check = 0;
    while (true) {
      int n_active = 0;
      for (auto& c : R.cands) n_active += c.active;
      if (n_active <= 1) break;
      if (iters >= sp.max_iterations) break;
      if (now_s() - t0 >= sp.time_limit && iters > 0) break;
      // Batch size adapts to the remaining time so the clock is respected.
      int batch = sp.batch;
      if (iters > 0) {
        const double per_iter = (now_s() - t0) / iters;
        const double left = sp.time_limit - (now_s() - t0);
        batch = std::max(nthreads, std::min(sp.batch, (int)(left / std::max(1e-6, per_iter)) + 1));
      } else {
        batch = std::max(nthreads, std::min(sp.batch, 2 * nthreads));
      }
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
          make_deal(P, seed, k, opp, deal);
          for (auto& c : R.cands) {
            if (!c.active) continue;
            float e, w;
            const int done = simulate(P, deal, c.move, gen, plies, seed ^ (u64)k * 0x9E3779B97F4A7C15ULL, e, w);
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
      iters = it1;
      for (auto& c : R.cands)
        if (c.active) c.n = iters;
      if (iters >= sp.min_iterations && iters - last_prune_check >= sp.batch) {
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

  static void make_deal(const Position& P, u64 seed, int k, const OppModel* opp, Deal& D) {
    Rng rng(mix64(seed + (u64)k * 0xD1B54A32D192ED03ULL));
    Rack pool = P.unseen;
    D.opp_n = 0;
    if (opp && !opp->empty() && P.bag_n > 0) {
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
  int simulate(const Position& P, const Deal& D, const Move& cand, MoveGen& gen, int plies, u64 salt, float& out_eq,
               float& out_win) const {
    Board b = P.board;
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

    auto play = [&](int side, const Move& m) {
      Rack& r = rk[side];
      const int bag_before = bn - bp;
      if (m.type == MT_PLACE) {
        b.place(*lex_, m);
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

    play(0, cand);
    int side = 1;
    int played = 1;
    for (int ply = 0; ply < plies && !over; ++ply, ++played) {
      EvalCtx ctx;
      ctx.bag = bn - bp;
      ctx.opp_face = rk[1 - side].face();
      ctx.allow_exchange = ctx.bag >= RACK_SIZE;
      const Move m = gen.generate_best(b, rk[side], ctx);
      play(side, m);
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
        continue;
      }
      const int n = std::min(c.n, Rc.n);
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
    for (size_t i = 0; i < R.cands.size(); ++i) {
      auto& c = R.cands[i];
      if (!c.active || (int)i == best) continue;
      const int n = std::min(c.n, B.n);
      if (n < 2) continue;
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
      if (m - sp.prune_z * se > 0) c.active = false;
    }
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
  int depth = 0;
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

  // `me` is the side to move.
  EndgameResult solve(const Board& b, const Rack& me, const Rack& opp, int zeros, const EndgameParams& p) {
    EndgameResult R;
    const double t0 = now_s();
    deadline_ = t0 + p.time_limit;
    stop_.store(false);
    resize_tt(p.tt_bits > 0 ? p.tt_bits : default_bits_);
    salt_ = mix64(++solves_ * 0x9E3779B97F4A7C15ULL);  // entries from earlier solves no longer match
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
    u8 pad = 0;
  };
  enum { F_EXACT = 1, F_LOWER = 2, F_UPPER = 3 };
  static constexpr u8 DEPTH_EXACT = 255;

  struct Worker {
    MoveGen gen;
    std::vector<Move> stack[64];
    u32 killer[64][2];
    long nodes = 0;
    int id = 0;
    explicit Worker(const Lexicon* l) : gen(l, nullptr) { std::memset(killer, 0, sizeof killer); }
  };

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

  static int leaf_eval(const Rack& mine, const Rack& theirs) {
    // Cheap guess: whoever keeps more face value is worse off.
    return theirs.face() - mine.face();
  }

  bool out_of_time(Worker& w) {
    if (stop_.load(std::memory_order_relaxed)) return true;
    if ((w.nodes & 255) == 0 && now_s() > deadline_) {
      stop_.store(true);
      return true;
    }
    return false;
  }

  void gen_moves(Worker& w, const Board& b, const Rack& mine, const Rack& theirs, std::vector<Move>& mv) {
    mv.clear();
    EvalCtx ctx;
    ctx.bag = 0;
    ctx.opp_face = theirs.face();
    ctx.allow_exchange = false;
    ctx.use_leaves = false;
    w.gen.generate_all(b, mine, ctx, mv);
  }

  // Value (for the side to move at `b`) of playing m, i.e. m.score - value(child).
  int child_value(Worker& w, const Board& b, const Rack* r, int side, int passes, int zeros, u64 bh, const Move& m, int depth,
                  int alpha, int beta, int ply, bool& exact) {
    const Rack& mine = r[side];
    const Rack& theirs = r[side ^ 1];
    if (m.type == MT_PLACE && m.ntiles == mine.n) {
      exact = true;
      return m.score + 2 * theirs.face();
    }
    if (m.type == MT_PASS) {
      bool ex = true;
      const int v = -negamax(w, b, r, side ^ 1, passes + 1, zeros + 1, bh, depth - 1, -beta, -alpha, ply + 1, ex);
      exact = ex;
      return v;
    }
    Board nb = b;
    nb.place(*lex_, m);
    u64 nbh = bh;
    for (int i = 0; i < m.len; ++i)
      if (m.tiles[i]) nbh ^= zsq_[m.square(i)][tile_code(m.tiles[i])];
    Rack nr[2] = {r[0], r[1]};
    nr[side].sub_all(m.used());
    bool ex = true;
    const int v = m.score - negamax(w, nb, nr, side ^ 1, 0, m.score == 0 ? zeros + 1 : 0, nbh, depth - 1, -beta + m.score,
                                    -alpha + m.score, ply + 1, ex);
    exact = ex;
    return v;
  }

  int negamax(Worker& w, const Board& b, const Rack* r, int side, int passes, int zeros, u64 bh, int depth, int alpha, int beta,
              int ply, bool& exact) {
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
    if (depth <= 0 || ply >= 63) {
      exact = false;
      // With a small rack, see whether the side to move can simply play out now.
      if (mine.n <= 3 && ply < 63) {
        std::vector<Move>& mv = w.stack[ply];
        gen_moves(w, b, mine, theirs, mv);
        int best_out = -100000;
        for (const auto& m : mv)
          if (m.type == MT_PLACE && m.ntiles == mine.n) best_out = std::max(best_out, (int)m.score + 2 * theirs.face());
        if (best_out > -100000) return best_out;
      }
      return leaf_eval(mine, theirs);
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
        v = child_value(w, b, r, side, passes, zeros, bh, m, depth, alpha, beta, ply, ex);
      } else {
        v = child_value(w, b, r, side, passes, zeros, bh, m, depth, alpha, alpha + 1, ply, ex);
        if (!stop_.load(std::memory_order_relaxed) && v > alpha && v < beta) {
          ex = true;
          v = child_value(w, b, r, side, passes, zeros, bh, m, depth, alpha, beta, ply, ex);
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
      gen_moves(w, b, mine, theirs, mv);
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
    std::vector<Move> root;
    gen_moves(w, b, me, opp, root);
    order(w, root, me.n, opp.face(), -1);
    if (root.empty()) {
      R.best = Move();
      return;
    }
    std::vector<int> vals(root.size(), 0);
    Move best = root[0];
    int best_val = 0;
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
          v = child_value(w, b, r, 0, 0, zeros, bh, m, depth, alpha, beta, 0, ex);
        } else {
          v = child_value(w, b, r, 0, 0, zeros, bh, m, depth, alpha, alpha + 1, 0, ex);
          if (!stop_.load() && v > alpha) {
            ex = true;
            v = child_value(w, b, r, 0, 0, zeros, bh, m, depth, alpha, beta, 0, ex);
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
//  expected spread.

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
    if (must_include && must_include->type == MT_PLACE) {
      bool found = false;
      for (const auto& m : cands) found |= m.same_as(*must_include);
      if (!found) {
        for (const auto& m : all)
          if (m.same_as(*must_include)) {
            cands.push_back(m);
            break;
          }
      }
    }
    if (cands.empty()) return R;
    // Distinct possible bag tiles.
    std::vector<std::pair<int, int>> tiles;  // (letter, count)
    for (int L = 0; L < NLET; ++L)
      if (P.unseen.c[L]) tiles.push_back({L, P.unseen.c[L]});
    const int total = P.unseen.n;
    struct Job {
      int cand, tile;
      int value = 0;
      bool exact = false;
    };
    std::vector<Job> jobs;
    for (size_t c = 0; c < cands.size(); ++c)
      for (size_t t = 0; t < tiles.size(); ++t) jobs.push_back({(int)c, (int)t});
    // Two passes: a quick shallow pass over everything, then deeper for the best few.
    auto run_jobs = [&](std::vector<int> which, double per_job, int depth) {
      std::atomic<size_t> next{0};
      auto worker = [&]() {
        EndgameSolver eg(lex_, 18);
        while (true) {
          const size_t k = next.fetch_add(1);
          if (k >= which.size()) break;
          Job& J = jobs[which[k]];
          const Move& m = cands[J.cand];
          Board nb = P.board;
          nb.place(*lex_, m);
          Rack mine = P.rack;
          mine.sub_all(m.used());
          mine.add(tiles[J.tile].first);
          Rack opp = P.unseen;
          opp.sub(tiles[J.tile].first);
          EndgameParams ep;
          ep.time_limit = per_job;
          ep.max_depth = depth;
          ep.tt_bits = 18;
          const EndgameResult er = eg.solve(nb, opp, mine, m.score == 0 ? P.zeros + 1 : 0, ep);
          J.value = m.score - er.value;
          J.exact = er.solved;
        }
      };
      const int nt = std::max(1, threads);
      std::vector<std::thread> th;
      for (int i = 0; i < nt; ++i) th.emplace_back(worker);
      for (auto& x : th) x.join();
    };
    std::vector<int> every(jobs.size());
    std::iota(every.begin(), every.end(), 0);
    const double budget1 = time_limit * 0.4;
    run_jobs(every, std::max(0.02, budget1 * std::max(1, threads) / (double)jobs.size()), 3);
    auto tally = [&]() {
      R.rows.clear();
      for (size_t c = 0; c < cands.size(); ++c) {
        PegResult::Row row;
        row.move = cands[c];
        for (const auto& J : jobs) {
          if (J.cand != (int)c) continue;
          const double w = (double)tiles[J.tile].second / total;
          const int final_spread = P.spread() + J.value;
          row.win += w * (final_spread > 0 ? 1.0 : (final_spread == 0 ? 0.5 : 0.0));
          row.spread += w * J.value;
          row.exact = row.exact && J.exact;
        }
        R.rows.push_back(row);
      }
      std::stable_sort(R.rows.begin(), R.rows.end(), [](const PegResult::Row& a, const PegResult::Row& b) {
        if (std::fabs(a.win - b.win) > 1e-9) return a.win > b.win;
        return a.spread > b.spread;
      });
    };
    tally();
    // Deeper pass for the top few candidates.
    const double remaining = time_limit - (now_s() - t0);
    if (remaining > 0.05) {
      std::vector<int> top;
      const int ntop = std::min<int>(6, (int)R.rows.size());
      for (int i = 0; i < ntop; ++i)
        for (size_t j = 0; j < jobs.size(); ++j)
          if (cands[jobs[j].cand].same_as(R.rows[i].move)) top.push_back((int)j);
      run_jobs(top, std::max(0.05, remaining * std::max(1, threads) / std::max<size_t>(1, top.size())), 40);
      tally();
    }
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
    if (!too_many) {
      for (const Rack& leave : exact) {
        if (now_s() - t0 > ip.time_limit) break;
        double prior = 1;
        for (int L = 0; L < NLET; ++L)
          for (int k = 0; k < leave.c[L]; ++k) prior *= (double)(avail.c[L] - k) / (double)(k + 1);
        const double w = prior * likelihood(leave);
        M.leaves.push_back(leave);
        total += w;
        M.cum.push_back(total);
      }
    } else {
      Rng rng(time_seed());
      for (int s = 0; s < ip.max_samples; ++s) {
        if (now_s() - t0 > ip.time_limit) break;
        Rack pool = avail, leave;
        for (int k = 0; k < leave_n; ++k) leave.add(draw_tile(pool, rng));
        const double w = likelihood(leave);
        M.leaves.push_back(leave);
        total += w;
        M.cum.push_back(total);
      }
    }
    // Need a reasonable coverage before trusting it.
    if (M.leaves.size() < 30 && too_many) M = OppModel();
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
      o << "inferred opponent leave (" << M.leaves.size() << (too_many ? " sampled" : " exact") << "): P(holds)";
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
      c.sim.max_iterations = 20000;
      c.sim.max_candidates = 30;
      c.endgame_time = 12.0;
      c.peg_time = 12.0;
    } else {
      err = "unknown player '" + base + "' (use static, static+, sim or champion)";
      return false;
    }
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
      else if (k == "threads") c.threads = (int)v;
      else if (k == "z") c.sim.prune_z = v;
      else if (k == "tau") c.sim.shrink_tau = v;
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
};

class Engine {
 public:
  Engine(const Lexicon* lex, const LeaveTable* lt, const WinModel* wm)
      : lex_(lex), lt_(lt), wm_(wm), sim_(lex, lt, wm), eg_(lex, 20), peg_(lex, lt), inf_(lex, lt) {}

  Decision choose(const Position& P, const EngineConfig& cfg, bool verbose = false) {
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
      if (!pr.rows.empty()) {
        D.move = pr.rows[0].move;
        D.method = "pre-endgame";
        D.seconds = pr.seconds;
        D.exact = pr.rows[0].exact;
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
      std::vector<Move> cands = sim_.candidates(P, cfg.sim.max_candidates);
      if (cands.size() > 1) {
        OppModel opp;
        std::string note;
        if (cfg.inference) opp = inf_.infer(P, cfg.inf, &note);
        if (!note.empty()) D.report.push_back(note);
        SimParams sp = cfg.sim;
        sp.threads = std::max(sp.threads, cfg.threads);
        const SimResult sr = sim_.run(P, cands, sp, opp.empty() ? nullptr : &opp);
        D.move = sr.cands[0].move;
        D.method = "simulation";
        D.seconds = sr.seconds;
        for (const auto& c : sr.cands) {
          DecisionRow row;
          row.move = c.move;
          row.static_eq = c.static_eq;
          row.value = c.mean_eq();
          row.win = c.mean_win();
          row.iterations = c.n;
          row.pruned = !c.active;
          D.rows.push_back(row);
        }
        D.report.push_back(fmt("simulated %d iterations, %d plies, %.1fs:", sr.iterations, sp.plies, sr.seconds));
        for (size_t i = 0; i < std::min<size_t>(8, sr.cands.size()); ++i) {
          const auto& c = sr.cands[i];
          D.report.push_back(fmt("  %-24s static %6.1f  sim %+6.1f  win %5.1f%%  (%d it)%s",
                                 move_str(P.board, c.move).c_str(), c.static_eq, c.mean_eq(), 100 * c.mean_win(), c.n,
                                 c.active ? "" : " pruned"));
        }
        return D;
      }
      if (cands.size() == 1) {
        D.move = cands[0];
        D.method = "only move";
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

    // New leave values, smallest leaves first so larger ones can use them as priors.
    const double mean_all = all_cnt > 0 ? all_sum / all_cnt : 0;
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

inline bool load_gcg(const std::string& path, const Lexicon& lex, int stop_at, Rng& rng, Game& out,
                     std::vector<GcgEvent>& events, std::string& err) {
  std::ifstream in(path);
  if (!in) {
    err = "cannot open " + path;
    return false;
  }
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

inline bool save_gcg(const Game& g, const std::string& path, const std::string& lexname, const std::string& p1 = "player1",
                     const std::string& p2 = "player2") {
  std::ofstream o(path);
  if (!o) return false;
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
    if (!L.load_word_list(path, err)) {
      std::cout << "error: " << err << "\n";
      return false;
    }
    lex = std::move(L);
    engine.reset();
    std::cout << fmt("lexicon %s: %zu words, %zu graph nodes, built in %.1fs\n", lex.name.c_str(), lex.nwords, lex.nodes.size(),
                     now_s() - t0);
    if (auto_data) {
      std::string stem = path;
      const size_t dot = stem.find_last_of('.');
      const size_t slash = stem.find_last_of("/\\");
      if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) stem = stem.substr(0, dot);
      std::ifstream f1(stem + ".leaves"), f2(stem + ".win");
      if (f1) load_leaves(stem + ".leaves");
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
    lexicon FILE          load a word list (also loads FILE.leaves / FILE.win if present)
    leaves FILE           load leave values        saveleaves FILE   save them
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
    auto [N]              let the engine play the next N moves (either side)
    unseen                tiles you cannot see (bag + opponent rack)
    history               moves so far
  Engine development
    autoplay N A B [threads=T] [seed=S]   match between engine configs A and B
    train [games=N] [gens=G] [threads=T] [out=NAME]   self-play training of leaves + win model
    selftest              correctness checks (move generator vs brute force etc.)
    bench                 speed benchmarks
    quit
Player SPECs: static (no search), static+ (static + endgame solvers), sim (fast search),
champion (full strength).  Options: time=S iters=N plies=N cands=N threads=N win=0|1
eg=0|1 peg=0|1 inf=0|1   e.g.  champion:time=30,plies=3
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
      if (c == '"' || c == '\\') o += '\\';
      o += c;
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
        auto res = [&](int v) { const int f = P.spread() + v; return f > 0 ? 1.0 : (f == 0 ? 0.5 : 0.0); };
        win_loss = std::max(0.0, res(er.value) - res(va));
        best_win = res(er.value);
      } else if (P.bag_n == 1) {
        const PegResult pr = eng().preendgame().solve(P, cfg.peg_candidates, secs, threads, false, &actual);
        if (pr.rows.empty()) continue;
        best = pr.rows[0].move;
        best_win = pr.rows[0].win;
        for (const auto& r : pr.rows)
          if (r.move.same_as(actual)) {
            win_loss = std::max(0.0, pr.rows[0].win - r.win);
            eq_loss = std::max(0.0, pr.rows[0].spread - r.spread);
          }
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
        if (sr.cands.empty()) continue;
        best = sr.cands[0].move;
        best_win = sr.cands[0].mean_win();
        for (const auto& c : sr.cands)
          if (c.move.same_as(actual)) {
            win_loss = std::max(0.0, sr.cands[0].mean_win() - c.mean_win());
            eq_loss = std::max(0.0, sr.cands[0].mean_eq() - c.mean_eq());
          }
      }
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
    } else if (cmd == "go" || cmd == "best" || cmd == "analyze" || cmd == "analyse") {
      bool json = false;
      double secs = 0;
      for (const auto& a : args) {
        if (a == "json") json = true;
        else secs = std::atof(a.c_str());
      }
      cmd_go(secs, true, json);
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
    } else if (cmd == "history") {
      Board b;
      for (size_t i = 0; i < game.events.size(); ++i) {
        const auto& e = game.events[i];
        std::cout << fmt("%3zu. P%d %-8s %-26s %4d\n", i + 1, e.player + 1, e.rack_before.str().c_str(),
                         move_str(b, e.move).c_str(), e.score_after);
        if (e.move.type == MT_PLACE) b.place(lex, e.move);
      }
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
  if (!need_lex()) return;
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
    ++nw;
    if (!lex.is_word((const u8*)w.data(), (int)w.size())) ++bad;
    if (w.size() <= (size_t)N) bylen[w.size()].push_back(w);
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
  std::cout << (failures ? fmt("SELF-TEST FAILED (%d problem(s))\n", failures) : std::string("All self-tests passed.\n"));
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
      std::cout << "usage: tilefish [--lexicon FILE] [--leaves FILE] [--win FILE] [--threads N] [--color] [--quiet] [COMMAND...]\n"
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
  std::cout << "Tilefish 1.0 - Scrabble engine (" << app.threads << " threads)\n";
  if (lexpath.empty()) {
    for (const char* cand : {"ENABLE.txt", "enable1.txt", "CSW24.txt", "CSW21.txt", "NWL2023.txt", "NWL2020.txt", "lexicon.txt"}) {
      std::ifstream f(cand);
      if (f) {
        lexpath = cand;
        break;
      }
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
        if (!app.execute(item)) return 0;
    }
    return 0;
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
