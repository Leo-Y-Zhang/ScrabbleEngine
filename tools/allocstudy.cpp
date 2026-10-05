// SPDX-License-Identifier: GPL-3.0-or-later
//
// allocstudy: how well does the simulation spend its time?  (experiments/README.md, section 13)
//
// One long simulation per position, with no pruning, records every candidate's result for
// every iteration.  The engine's way of sharing the iterations out (and alternatives) can
// then be replayed on the first half of those results, at any budget, and each choice is
// scored on the second half, which no policy has seen.  Iterations use common random
// numbers, so iteration k deals the same tiles to every candidate in both halves.
//
//   g++ -O3 -march=native -std=c++17 -pthread tools/allocstudy.cpp -o allocstudy
//   ./allocstudy gen LEXICON DIR NPOS ITERS SEED [late] [cands N] [threads T]
//       self-play positions (static players; bag 20-70, or 2-7 with `late`), each with its
//       top N candidates (default 30) simulated ITERS iterations; DIR/posK.bin
//   ./allocstudy replay DIR NPOS BUDGET [late]
//       the policies below with BUDGET candidate-iterations on the first half of each dump;
//       the mean loss of each policy's choice against the second half's best, in win %
//   ./allocstudy wide DIR NPOS
//       dumps made with `cands 60`: how often the best candidate ranked 31-60 by static
//       equity beats the best of the top 30 (each chosen on the first half, compared on the second)
//
// It includes the engine's source and calls its simulator directly, so it measures the
// engine as it is.  The ranking and pruning are re-implemented here from Simulator::rank
// and Simulator::prune (keep them in step if those change).
#define main tilefish_main
#include "../tilefish.cpp"
#undef main
#include <cstdio>

using namespace tf;

namespace {

const double TB = SimParams().equity_tiebreak;

struct Dump {
  int C = 0, N = 0, bag = 0;
  std::vector<float> st;  // static equity, candidates in static order
  std::vector<std::vector<float>> eq, win;
  double obj(int c, int k) const { return (double)win[c][k] + TB * eq[c][k]; }
  double mean(int c, int k0, int k1, int what) const {  // what: 0 objective, 1 equity, 2 win
    double s = 0;
    for (int k = k0; k < k1; ++k) s += what == 0 ? obj(c, k) : what == 1 ? eq[c][k] : win[c][k];
    return k1 > k0 ? s / (k1 - k0) : 0;
  }
};

bool save(const std::string& path, const Dump& D) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(&D.C, sizeof(int), 1, f) == 1 && std::fwrite(&D.N, sizeof(int), 1, f) == 1 &&
            std::fwrite(&D.bag, sizeof(int), 1, f) == 1 && std::fwrite(D.st.data(), sizeof(float), D.C, f) == (size_t)D.C;
  for (int c = 0; c < D.C && ok; ++c)
    ok = std::fwrite(D.eq[c].data(), sizeof(float), D.N, f) == (size_t)D.N &&
         std::fwrite(D.win[c].data(), sizeof(float), D.N, f) == (size_t)D.N;
  return std::fclose(f) == 0 && ok;
}

bool load(const std::string& path, Dump& D) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  bool ok = std::fread(&D.C, sizeof(int), 1, f) == 1 && std::fread(&D.N, sizeof(int), 1, f) == 1 &&
            std::fread(&D.bag, sizeof(int), 1, f) == 1 && D.C > 0 && D.C < 1000 && D.N > 0;
  if (ok) {
    D.st.resize(D.C);
    D.eq.assign(D.C, std::vector<float>(D.N));
    D.win.assign(D.C, std::vector<float>(D.N));
    ok = std::fread(D.st.data(), sizeof(float), D.C, f) == (size_t)D.C;
  }
  for (int c = 0; c < D.C && ok; ++c)
    ok = std::fread(D.eq[c].data(), sizeof(float), D.N, f) == (size_t)D.N &&
         std::fread(D.win[c].data(), sizeof(float), D.N, f) == (size_t)D.N;
  std::fclose(f);
  return ok;
}

// Simulator::rank on the first n[c] iterations of each candidate: the posterior relative to
// the most-simulated candidate, the static equity as the prior.  Returns the choice.
int rank_choice(const Dump& D, const std::vector<int>& n, double tau) {
  int ref = 0;
  for (int i = 1; i < D.C; ++i)
    if (n[i] > n[ref] || (n[i] == n[ref] && D.mean(i, 0, n[i], 0) > D.mean(ref, 0, n[ref], 0))) ref = i;
  double sxy = 0, sxx = 0;
  const double rq = D.mean(ref, 0, n[ref], 1), rw = D.mean(ref, 0, n[ref], 2);
  for (int c = 0; c < D.C; ++c) {
    const int m = std::min(n[c], n[ref]);
    if (m < 8 || c == ref) continue;
    const double x = D.mean(c, 0, n[c], 1) - rq, y = D.mean(c, 0, n[c], 2) - rw;
    sxy += m * x * y;
    sxx += m * x * x;
  }
  const double scale = (sxx > 0 ? std::max(0.0, std::min(0.03, sxy / sxx)) : 0.005) + TB;
  const double pv = 2.0 * std::pow(scale * tau, 2);
  int best = ref;
  double best_post = 0;
  for (int c = 0; c < D.C; ++c) {
    if (c == ref) continue;
    const double pm = scale * ((double)D.st[c] - D.st[ref]);
    const int m = std::min(n[c], n[ref]);
    double post = pm;
    if (m >= 2) {
      double s = 0, s2 = 0;
      for (int k = 0; k < m; ++k) {
        const double d = D.obj(c, k) - D.obj(ref, k);
        s += d;
        s2 += d * d;
      }
      const double mu = s / m, se2 = std::max(1e-12, (s2 / m - mu * mu) / (m - 1));
      post = (mu / se2 + pm / pv) / (1.0 / se2 + 1.0 / pv);
    }
    if (post > best_post) {
      best_post = post;
      best = c;
    }
  }
  return best;
}

// Simulator::prune.  `keep`: survivors kept at least (the engine keeps 1, the closest).
void prune(const Dump& D, const std::vector<int>& n, std::vector<char>& active, double z, int keep) {
  int best = -1;
  double bo = -1e300;
  for (int c = 0; c < D.C; ++c)
    if (active[c] && D.mean(c, 0, n[c], 0) > bo) {
      bo = D.mean(c, 0, n[c], 0);
      best = c;
    }
  if (best < 0) return;
  int survivors = 0;
  std::vector<std::pair<double, int>> dropped;  // (z, candidate)
  for (int c = 0; c < D.C; ++c) {
    if (!active[c] || c == best) continue;
    const int m = std::min(n[c], n[best]);
    if (m < 2) {
      ++survivors;
      continue;
    }
    double s = 0, s2 = 0;
    for (int k = 0; k < m; ++k) {
      const double d = D.obj(best, k) - D.obj(c, k);
      s += d;
      s2 += d * d;
    }
    const double mu = s / m, se = std::sqrt(std::max(1e-12, (s2 / m - mu * mu) * m / (m - 1)) / m);
    if (mu - z * se > 0) {
      active[c] = 0;
      dropped.push_back({mu / se, c});
    } else {
      ++survivors;
    }
  }
  std::sort(dropped.begin(), dropped.end());
  for (size_t i = 0; i < dropped.size() && survivors < keep; ++i, ++survivors) active[dropped[i].second] = 1;
}

// The engine's loop on one thread: batches of max(48, iterations/16) after a first batch of
// 2, and a pruning check after each batch once 96 iterations are done.  `delay`: no pruning
// before this share of the budget is spent.
int policy_engine(const Dump& D, long budget, int half, double z, double tau, double delay, int keep,
                  std::vector<int>* counts = nullptr) {
  std::vector<int> n(D.C, 0);
  std::vector<char> active(D.C, 1);
  long spent = 0;
  int iters = 0, last = 0;
  while (true) {
    int na = 0;
    for (char a : active) na += a;
    if (na <= 1) break;
    int batch = iters == 0 ? 2 : std::max(48, iters / 16);
    batch = (int)std::min<long>(batch, (budget - spent) / na);
    batch = std::min(batch, half - iters);
    if (batch <= 0) break;
    iters += batch;
    spent += (long)batch * na;
    for (int c = 0; c < D.C; ++c)
      if (active[c]) n[c] = iters;
    if (iters >= 96 && iters - last >= std::max(48, iters / 16) && spent >= delay * budget) {
      last = iters;
      prune(D, n, active, z, keep);
    }
  }
  if (counts) *counts = n;
  return rank_choice(D, n, tau);
}

// Every candidate the same number of iterations.
int policy_uniform(const Dump& D, long budget, int half, double tau) {
  return rank_choice(D, std::vector<int>(D.C, (int)std::min<long>(half, budget / D.C)), tau);
}

// Sequential halving: ceil(log2 C) rounds with equal shares of the budget; after each, the
// better half by simulated mean (paired against the most simulated survivor) goes on.
int policy_halving(const Dump& D, long budget, int half, double tau) {
  std::vector<int> n(D.C, 0), S(D.C);
  for (int c = 0; c < D.C; ++c) S[c] = c;
  const int rounds = (int)std::ceil(std::log2((double)D.C));
  for (int r = 0; r < rounds && S.size() > 1; ++r) {
    const int add = (int)(budget / rounds / (long)S.size());
    for (int c : S) n[c] = std::min(half, n[c] + add);
    int ref = S[0];
    for (int c : S)
      if (n[c] > n[ref]) ref = c;
    std::vector<std::pair<double, int>> v;
    for (int c : S) {
      const int m = std::min(n[c], n[ref]);
      double s = 0;
      for (int k = 0; k < m; ++k) s += D.obj(c, k) - D.obj(ref, k);
      v.push_back({m ? -s / m : 0.0, c});
    }
    std::sort(v.begin(), v.end());
    S.clear();
    for (size_t i = 0; i < (v.size() + 1) / 2; ++i) S.push_back(v[i].second);
  }
  return rank_choice(D, n, tau);
}

int gen(int argc, char** argv) {
  if (argc < 7) return 2;
  const std::string lexicon = argv[2], dir = argv[3];
  const int npos = std::atoi(argv[4]), iters = std::atoi(argv[5]);
  const u64 seed = std::strtoull(argv[6], nullptr, 10);
  bool late = false;
  int ncand = 30, threads = 4;
  for (int i = 7; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "late") late = true;
    else if (a == "cands" && i + 1 < argc) ncand = std::atoi(argv[++i]);
    else if (a == "threads" && i + 1 < argc) threads = std::atoi(argv[++i]);
  }
  App app;
  if (!app.load_lexicon(lexicon, true)) return 1;
  Simulator sim(&app.lex, &app.leaves, &app.wm);
  MoveGen gen(&app.lex, &app.leaves);
  Rng rng(seed);
  const int lo = late ? 2 : 20, hi = late ? 7 : 70;
  for (int made = 0; made < npos;) {
    Game g;
    g.reset(rng);
    const int target = lo + (int)rng.below((u32)(hi - lo + 1));
    while (!g.over && g.bag.n > target) {
      const Position Q = Position::from_game(g);
      g.apply(app.lex, gen.generate_best(Q.board, Q.rack, Simulator::ctx_for(Q)), rng);
    }
    if (g.over || g.bag.n < lo || g.bag.n > hi) continue;
    Position P = Position::from_game(g);
    P.has_opp_last = false;  // no inference: the opponent's rack is a uniform draw
    const std::vector<Move> cands = sim.candidates(P, ncand);
    if ((int)cands.size() < ncand) continue;
    SimParams sp;
    sp.threads = threads;
    sp.time_limit = 1e9;
    sp.max_iterations = iters;
    sp.min_iterations = 1 << 30;  // no pruning
    sp.seed = seed * 1000 + made + 1;
    const double t0 = now_s();
    const SimResult R = sim.run(P, cands, sp);
    Dump D;
    D.C = ncand;
    D.N = iters;
    D.bag = P.bag_n;
    for (const Move& m : cands)  // back to static order
      for (const auto& c : R.cands)
        if (c.move.same_as(m)) {
          D.st.push_back(c.static_eq);
          D.eq.push_back(c.eq);
          D.win.push_back(c.win);
          break;
        }
    if ((int)D.st.size() != ncand || !save(dir + "/pos" + std::to_string(made) + ".bin", D)) return 1;
    std::fprintf(stderr, "position %d: bag %d, %.1fs\n", made, P.bag_n, now_s() - t0);
    ++made;
  }
  return 0;
}

int replay(int argc, char** argv) {
  if (argc < 5) return 2;
  const std::string dir = argv[2];
  const int npos = std::atoi(argv[3]);
  const long budget = std::atol(argv[4]);
  const bool late = argc > 5 && std::string(argv[5]) == "late";
  // The engine's prior: tau 4, times 2.5 in the play-out phase (Simulator::run).
  const double tau = SimParams().shrink_tau * (late ? 2.5 : 1.0);
  struct Policy {
    const char* name;
    std::function<int(const Dump&, int)> choose;
  };
  const std::vector<Policy> policies = {
      {"engine (z 2.4)", [&](const Dump& D, int h) { return policy_engine(D, budget, h, 2.4, tau, 0, 1); }},
      {"z 3.2", [&](const Dump& D, int h) { return policy_engine(D, budget, h, 3.2, tau, 0, 1); }},
      {"prune after 10%", [&](const Dump& D, int h) { return policy_engine(D, budget, h, 2.4, tau, 0.10, 1); }},
      {"prune after 25%", [&](const Dump& D, int h) { return policy_engine(D, budget, h, 2.4, tau, 0.25, 1); }},
      {"prune after 50%", [&](const Dump& D, int h) { return policy_engine(D, budget, h, 2.4, tau, 0.50, 1); }},
      {"keep 4", [&](const Dump& D, int h) { return policy_engine(D, budget, h, 2.4, tau, 0, 4); }},
      {"uniform", [&](const Dump& D, int h) { return policy_uniform(D, budget, h, tau); }},
      {"halving", [&](const Dump& D, int h) { return policy_halving(D, budget, h, tau); }},
  };
  const size_t P = policies.size();
  std::vector<std::vector<double>> loss(P);
  std::vector<int> hits(P, 0);
  long pruned = 0, pruned_better = 0, capped = 0;
  double top2_share = 0, most = 0;
  int used = 0;
  for (int i = 0; i < npos; ++i) {
    Dump D;
    if (!load(dir + "/pos" + std::to_string(i) + ".bin", D)) continue;
    ++used;
    const int half = D.N / 2;
    std::vector<double> truth(D.C);
    for (int c = 0; c < D.C; ++c) truth[c] = D.mean(c, half, D.N, 0);
    const int tb = (int)(std::max_element(truth.begin(), truth.end()) - truth.begin());
    for (size_t p = 0; p < P; ++p) {
      const int ch = policies[p].choose(D, half);
      loss[p].push_back(truth[tb] - truth[ch]);
      hits[p] += ch == tb;
    }
    std::vector<int> n;
    const int ch = policy_engine(D, budget, half, 2.4, tau, 0, 1, &n);
    std::vector<int> s = n;
    std::sort(s.rbegin(), s.rend());
    long tot = 0;
    for (int x : n) tot += x;
    top2_share += (double)(s[0] + s[1]) / tot;
    most += s[0];
    capped += s[0] >= half;
    for (int c = 0; c < D.C; ++c)
      if (n[c] < s[0] / 2) {
        ++pruned;
        pruned_better += truth[c] > truth[ch];
      }
  }
  if (!used) return 1;
  std::printf("%d positions (%s), %ld candidate-iterations each; the engine's top two get %.1f%% of them "
              "(%.0f iterations for the most simulated%s)\n",
              used, late ? "play-out phase, tau 10" : "2-ply phase, tau 4", budget, 100 * top2_share / used, most / used,
              capped ? ", capped by the dump in some positions" : "");
  std::printf("  %-16s %14s %10s %24s\n", "policy", "loss (win %)", "best hit", "vs engine (win %, s.e.)");
  for (size_t p = 0; p < P; ++p) {
    double l = 0, d = 0, d2 = 0;
    for (int i = 0; i < used; ++i) {
      l += loss[p][i];
      const double x = loss[0][i] - loss[p][i];
      d += x;
      d2 += x * x;
    }
    const double md = d / used, se = std::sqrt(std::max(0.0, d2 / used - md * md) / std::max(1, used - 1));
    std::printf("  %-16s %14.3f %9.1f%% %+16.3f (%.3f)\n", policies[p].name, 100 * l / used, 100.0 * hits[p] / used,
                100 * md, 100 * se);
  }
  std::printf("  the engine pruned %ld candidates early; the second half rates %ld of them above its choice\n", pruned,
              pruned_better);
  return 0;
}

int wide(int argc, char** argv) {
  if (argc < 4) return 2;
  const std::string dir = argv[2];
  const int npos = std::atoi(argv[3]);
  int used = 0, beyond = 0, beyond_sig = 0;
  double gain = 0;
  for (int i = 0; i < npos; ++i) {
    Dump D;
    if (!load(dir + "/pos" + std::to_string(i) + ".bin", D) || D.C <= 30) continue;
    ++used;
    // Chosen on the first half, measured on the second, so the best of each group is not
    // flattered by the noise that made it look best.
    const int half = D.N / 2, m = D.N - half;
    int b30 = 0, bw = 30;
    for (int c = 0; c < D.C; ++c) {
      const double x = D.mean(c, 0, half, 0);
      if (c < 30 && x > D.mean(b30, 0, half, 0)) b30 = c;
      if (c >= 30 && x > D.mean(bw, 0, half, 0)) bw = c;
    }
    double s = 0, s2 = 0;
    for (int k = half; k < D.N; ++k) {
      const double d = D.obj(bw, k) - D.obj(b30, k);
      s += d;
      s2 += d * d;
    }
    const double mu = s / m, se = std::sqrt(std::max(0.0, s2 / m - mu * mu) / (m - 1));
    if (mu > 0) {
      ++beyond;
      gain += mu;
      beyond_sig += mu > 2 * se;
    }
    std::printf("position %d (bag %d): best of 31-%d (#%d) %+.3f win %% against best of top 30 (#%d), s.e. %.3f\n", i,
                D.bag, D.C, bw + 1, 100 * mu, b30 + 1, 100 * se);
  }
  if (!used) return 1;
  std::printf("%d positions: a candidate ranked 31 or lower is best in %d (by more than 2 s.e. in %d); "
              "mean gain where it is %.3f win %%\n",
              used, beyond, beyond_sig, beyond ? 100 * gain / beyond : 0.0);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string mode = argc > 1 ? argv[1] : "";
  const int rc = mode == "gen" ? gen(argc, argv) : mode == "replay" ? replay(argc, argv) : mode == "wide" ? wide(argc, argv) : 2;
  if (rc == 2)
    std::fprintf(stderr,
                 "usage: allocstudy gen LEXICON DIR NPOS ITERS SEED [late] [cands N] [threads T]\n"
                 "       allocstudy replay DIR NPOS BUDGET [late]\n"
                 "       allocstudy wide DIR NPOS\n");
  return rc;
}
