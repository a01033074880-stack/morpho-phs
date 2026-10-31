// SPDX-License-Identifier: AGPL-3.0-only
// Phase-2 exp3: control-layer coupling.  12x12 gated self-oscillating patch (demo-04 parameters), 48 kHz.
//   mode 0 "none"    : static parameters, no medium
//   mode 1 "oneway"  : Barkley spiral -> ParamMap -> ENPU (demo 04), medium unaffected by the acoustics
//   mode 2 "closed"  : as 1 + acoustic -> medium feedback: per tick, sites whose kinetic energy exceeds
//                      2x the patch mean are stimulated, stim_i = kappa*clip(e_i/mean(e) - 2, 0, 1)
// Writes experiments/results/phase2/exp3/<mode>_s<seed>.f32 (2-ch interleaved pickups) and a CSV per tick.
// Build: zig c++ -std=c++20 -O3 -march=native -Iinclude tools/exp3_coupling.cpp -o build/exp3_coupling
#include "morpho_phs/morpho_phs.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace morpho_phs;
using R = double;
int main(int argc, char** argv) {
    const int mode = std::atoi(argv[1]), seed = std::atoi(argv[2]); const double secs = argc > 3 ? std::atof(argv[3]) : 30.0;
    const double kappa = argc > 4 ? std::atof(argv[4]) : 0.3, tau = argc > 5 ? std::atof(argv[5]) : 1.0 / (750 * 0.016 * 2);
    const double FS = 48000, WU = 2 * M_PI * 1500;
    Honeycomb lat(12, 12); const int N = lat.N; EsavLF<R> eng; EsavLF<R>::Config cfg; cfg.fs = FS; cfg.k0 = 0.1 * WU * WU; cfg.kc = WU * WU;
    cfg.potential = Potential::Quartic; cfg.E_cap = 2e6; eng.prepare(lat, cfg, 2);
    for (int i = 0; i < N; ++i) { auto& P = eng.params(); P.m[i] = 1; P.alpha[i] = 0.05 * WU * WU; P.beta[i] = 0; P.mu[i] = 40; P.eta[i] = 2e-4; P.g[i] = 0.6 * WU; }
    const double kick[4][2] = {{3, 3}, {9, 4}, {5, 8}, {12, 2}};
    std::vector<R> q0(2 * N, 0.0), v0(2 * N, 0.0); v0[2 * lat.nearest_site(kick[seed % 4][0], kick[seed % 4][1])] = 1.0; eng.set_state(q0, v0);
    Port<R> ports[2] = {{lat.nearest_site(2, 1), 1, 0}, {lat.nearest_site(14, 9), 0, 1}}; eng.set_ports(ports);
    ParamMap<R> map; map.base.resize(N); map.base.assign_from(eng.params()); map.g_depth = 1.0; map.mu_depth = 60; map.mu_max = 120;
    ControlLayer<R> ctl; ctl.prepare(lat, map); ctl.set_tau(tau);
    {   // broken front -> spiral; position varied with seed
        auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); const double x0 = 3 + (seed % 3);
        for (int i = 0; i < N; ++i) { double xx = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), yy = lat.xy[i][1]; if (yy < 5 && xx >= x0 && xx < x0 + 1.5) u[i] = 1; if (yy < 5 && xx < x0) v[i] = 0.5; }
    }
    const char* names[3] = {"none", "oneway", "closed"};
    std::string base = std::string("experiments/results/phase2/exp3/") + names[mode] + "_s" + std::to_string(seed);
    if (argc > 4) base += "_k" + std::to_string(kappa).substr(0, 4);
    if (argc > 5) base += "_tau" + std::to_string(tau).substr(0, 6);
    FILE* fa = std::fopen((base + ".f32").c_str(), "wb"); FILE* fc = std::fopen((base + ".csv").c_str(), "w");
    std::fprintf(fc, "tick,E,u_mean,u_frac_excited,stim_sum,enpu_s\n");
    const std::size_t n = std::size_t(FS * secs); R u[2] = {0, 0}, y[2]; std::vector<float> buf(2 * 64); std::vector<double> e(N);
    double Emax = 0; long scaled = 0, ticks = 0;
    for (std::size_t i = 0; i < n; ++i) {
        eng.process_sample(u, y); buf[2 * (i & 63)] = float(y[0]); buf[2 * (i & 63) + 1] = float(y[1]);
        Emax = std::max(Emax, eng.tracked_energy());
        if ((i & 63) == 63) {
            std::fwrite(buf.data(), 4, buf.size(), fa);
            double stim = 0, s = 1;
            if (mode == 2) {
                double em = 0; for (int k = 0; k < N; ++k) { const R vx = eng.v_at(2 * k), vy = eng.v_at(2 * k + 1); e[k] = 0.5 * (vx * vx + vy * vy); em += e[k]; }
                em /= N;
                for (int k = 0; k < N; ++k) { const double a = kappa * std::clamp(e[k] / (em + 1e-300) - 2.0, 0.0, 1.0); if (a > 0) { ctl.medium.stimulate(k, R(a)); stim += a; } }
            }
            if (mode >= 1) { s = ctl.control_tick(eng); scaled += s < 1; }
            ++ticks;
            double um = 0, ex = 0; for (int k = 0; k < N; ++k) { um += ctl.medium.u()[k]; ex += ctl.medium.u()[k] > 0.5; }
            std::fprintf(fc, "%ld,%.6e,%.5f,%.5f,%.4f,%.6f\n", ticks, eng.tracked_energy(), um / N, ex / N, stim, double(s));
        }
    }
    std::fclose(fa); std::fclose(fc);
    std::printf("{\"mode\":\"%s\",\"seed\":%d,\"secs\":%.1f,\"kappa\":%.3f,\"tau_s\":%.5f,\"Emax\":%.6e,\"E_cap\":%.3e,\"enpu_scaled\":%ld,\"ticks\":%ld,\"finite\":%s}\n",
                names[mode], seed, secs, kappa, ctl.medium.tau_seconds(FS / 64), Emax, cfg.E_cap, scaled, ticks, std::isfinite(Emax) ? "true" : "false");
}
