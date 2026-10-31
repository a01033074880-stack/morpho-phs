// SPDX-License-Identifier: AGPL-3.0-only
// Phase-2 benchmark: multithreaded ESAV-LF scaling (deterministic kernel, one barrier per sample).
// Build: zig c++ -std=c++20 -O3 -march=native -Iinclude bench/bench_mt.cpp -o build/bench_mt
// Usage: bench_mt <secs> <reps> [L].  Each config: process_block(64) + control tick (Barkley + ParamMap + ENPU) per block.
// Reports median us/sample, % of one core at 48 kHz, and the p99 / max wall time of a 64-sample block vs its
// real-time budget (1333.3 us), over all reps.
#include "morpho_phs/morpho_phs.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>

using namespace morpho_phs;
template <class R>
static void run(int L, Potential pot, int T, bool control, int secs, int reps) {
    Honeycomb lat(L, L); const int N = lat.N; const double wu = 2 * M_PI * 1500;
    EsavLFMT<R> eng; typename EsavLFMT<R>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; cfg.potential = pot; cfg.E_cap = 1e9; cfg.threads = T;
    eng.prepare(lat, cfg, 16);
    auto& P = eng.params();
    for (int i = 0; i < N; ++i) { P.m[i] = 1; P.alpha[i] = R(cfg.kc * 0.05); P.beta[i] = R(cfg.k0 * 0.2); P.mu[i] = R(-3); P.eta[i] = R(1e-6); P.g[i] = R(0.6 * wu); }
    const int np = 4; std::vector<Port<R>> ports; for (int k = 0; k < np; ++k) ports.push_back({(k * 37 + 5) % N, R(1), R(0)});
    eng.set_ports(ports);
    ParamMap<R> map; map.base.resize(N); map.base.assign_from(P); map.g_depth = R(0.5); map.mu_depth = R(50); map.mu_max = R(60);
    ControlLayer<R, EsavLFMT<R>> ctl; ctl.prepare(lat, map);
    { auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); for (int i = 0; i < N; ++i) { double x = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), y = lat.xy[i][1]; if (y < L * 0.43 && x >= L * 0.25 && x < L * 0.25 + 1.5) u[i] = 1; if (y < L * 0.43 && x < L * 0.25) v[i] = R(0.5); } }
    std::vector<R> u(64 * np), y(64 * np);
    const int nb = 48000 * secs / 64; std::vector<double> med, blk; blk.reserve(size_t(nb) * reps);
    for (int r = 0; r < reps + 1; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < nb; ++b) {
            auto tb = std::chrono::steady_clock::now();
            for (int n = 0; n < 64; ++n) for (int k = 0; k < np; ++k) u[n * np + k] = ((b * 64 + n) % 4800 == k) ? R(1e4) : R(0);
            eng.process_block(u.data(), y.data(), 64);
            if (control) ctl.control_tick(eng);
            if (r > 0) blk.push_back(std::chrono::duration<double>(std::chrono::steady_clock::now() - tb).count() * 1e6);
        }
        auto t1 = std::chrono::steady_clock::now();
        if (r > 0) med.push_back(std::chrono::duration<double>(t1 - t0).count() / (nb * 64.0) * 1e6);
    }
    std::sort(med.begin(), med.end()); std::sort(blk.begin(), blk.end());
    const double us = med[med.size() / 2], p99 = blk[size_t(blk.size() * 0.99)], mx = blk.back();
    std::printf("bench_mt,%d,%d,%s,%s,%d,%d,%.4f,%.2f,%.1f,%.1f,%s\n", L, 4 * L * L, sizeof(R) == 4 ? "f32" : "f64",
                pot == Potential::Quartic ? "quartic" : (pot == Potential::None ? "linear" : "quartic+logcosh"), int(control), T, us, us * 4.8, p99, mx, eng.tracked_energy() == eng.tracked_energy() ? "finite" : "NaN");
    std::fflush(stdout);
}
int main(int argc, char** argv) {
    const int secs = argc > 1 ? std::atoi(argv[1]) : 2, reps = argc > 2 ? std::atoi(argv[2]) : 3;
    std::printf("config,L,dof,precision,potential,control,threads,us_per_sample_median,pct_one_core_48k,block64_p99_us,block64_max_us,status\n");
    const int only_L = argc > 3 ? std::atoi(argv[3]) : 0;   // optional: restrict to one lattice size
    for (int L : {32, 16, 48})
        for (Potential pot : {Potential::Quartic, Potential::QuarticLogcosh})
            for (int prec = 0; prec < 2; ++prec)
                for (int T : {1, 2, 3, 4, 6, 8}) {
                    if (L == 48 && prec == 0) continue;
                    if (only_L && L != only_L) continue;
                    if (prec) run<double>(L, pot, T, true, secs, reps); else run<float>(L, pot, T, true, secs, reps);
                }
}
