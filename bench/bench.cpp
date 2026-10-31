// SPDX-License-Identifier: AGPL-3.0-only
// Phase-1 benchmark: us/sample and % of one core at 48 kHz.  Build:
//   zig c++ -std=c++20 -O3 -march=native -Iinclude bench/bench.cpp -o build/bench
#include "morpho_phs/morpho_phs.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

using namespace morpho_phs;
template <class R>
double run(int L, int nonlinear, bool control, int nports, int reps, int secs) {
    Honeycomb lat(L, L); const int N = lat.N; const double wu = 2 * M_PI * 1500;
    EsavLF<R> eng; typename EsavLF<R>::Config cfg; cfg.k0 = 0.1 * wu * wu; cfg.kc = wu * wu; cfg.potential = nonlinear == 0 ? Potential::None : (nonlinear == 1 ? Potential::Quartic : Potential::QuarticLogcosh); cfg.E_cap = 1e9;
    eng.prepare(lat, cfg, 16);
    auto& P = eng.params();
    for (int i = 0; i < N; ++i) { P.m[i] = 1; P.alpha[i] = R(cfg.kc * 0.05); P.beta[i] = R(cfg.k0 * 0.2); P.mu[i] = R(-3); P.eta[i] = R(1e-6); P.g[i] = R(0.6 * wu); }
    std::vector<Port<R>> ports; for (int k = 0; k < nports; ++k) ports.push_back({(k * 37 + 5) % N, R(1), R(0)});
    eng.set_ports(ports);
    ParamMap<R> map; map.base.resize(N); map.base.assign_from(P); map.g_depth = R(0.5); map.mu_depth = R(50); map.mu_max = R(60);
    ControlLayer<R> ctl; ctl.prepare(lat, map);
    { auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); for (int i = 0; i < N; ++i) { double x = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), y = lat.xy[i][1]; if (y < L * 0.43 && x >= L * 0.25 && x < L * 0.25 + 1.5) u[i] = 1; if (y < L * 0.43 && x < L * 0.25) v[i] = R(0.5); } }
    std::vector<R> u(nports), y(nports);
    const int nS = 48000 * secs; std::vector<double> t;
    volatile R sink = 0;
    for (int r = 0; r < reps + 1; ++r) {
        auto t0 = std::chrono::steady_clock::now();
        for (int n = 0; n < nS; ++n) {
            for (int k = 0; k < nports; ++k) u[k] = (n % 4800 == k) ? R(1e4) : R(0);
            eng.process_sample(u, y);
            if (control && (n & 63) == 63) ctl.control_tick(eng);
            sink = sink + y[0];
        }
        auto t1 = std::chrono::steady_clock::now();
        if (r > 0) t.push_back(std::chrono::duration<double>(t1 - t0).count() / nS * 1e6);   // first rep = warm-up
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}
int main(int argc, char** argv) {
    int reps = argc > 1 ? std::atoi(argv[1]) : 5, secs = argc > 2 ? std::atoi(argv[2]) : 2;
    std::printf("config,L,sites,dof,precision,potential(0=none 1=quartic 2=quartic+logcosh),control(ENPU+Barkley/64),ports,us_per_sample_median,pct_one_core_48k\n");
    for (int L : {8, 16, 32})
        for (int prec = 0; prec < 2; ++prec)
            for (int nl = 0; nl < 3; ++nl)
                for (int ctl = 0; ctl < 2; ++ctl) {
                    const int nports = 4;
                    double us = prec ? run<double>(L, nl, ctl, nports, reps, secs) : run<float>(L, nl, ctl, nports, reps, secs);
                    std::printf("bench,%d,%d,%d,%s,%d,%d,%d,%.4f,%.2f\n", L, 2 * L * L, 4 * L * L, prec ? "f64" : "f32", nl, ctl, nports, us, us * 48000 / 1e6 * 100);
                    std::fflush(stdout);
                }
}
