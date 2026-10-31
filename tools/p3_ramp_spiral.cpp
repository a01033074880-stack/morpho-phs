// SPDX-License-Identifier: AGPL-3.0-only
// Phase-3 check of the in-kernel g/mu staircase ramp: Phase-2 "spiral" scenario (12x12 self-oscillating patch,
// Barkley spiral -> g, mu via ControlLayer, ENPU-C), processed in 64-sample blocks (control tick between blocks).
// args: interp_step (0 = block-rate step, 8, 1) [secs]  -> experiments/results/phase3/ramp/spiral_blk64_i<step>.f32
#include "morpho_phs/morpho_phs.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace morpho_phs; using R = double;
int main(int argc, char** argv) {
    const int interp = std::atoi(argv[1]); const double secs = argc > 2 ? std::atof(argv[2]) : 8.0;
    const double FS = 48000, WU = 2 * M_PI * 1500; const int L = 12;
    Honeycomb lat(L, L); const int N = lat.N; EsavLFMT<R> eng; EsavLFMT<R>::Config cfg; cfg.fs = FS; cfg.k0 = 0.1 * WU * WU; cfg.kc = WU * WU;
    cfg.potential = Potential::Quartic; cfg.threads = 1; cfg.E_cap = 2e6; cfg.interp_step = interp; eng.prepare(lat, cfg, 2);
    auto& P = eng.params();
    for (int i = 0; i < N; ++i) { P.m[i] = 1; P.alpha[i] = 0.05 * WU * WU; P.beta[i] = 0; P.mu[i] = 40; P.eta[i] = 2e-4; P.g[i] = 0.6 * WU; }
    std::vector<R> q0(2 * N, 0.0), v0(2 * N, 0.0); v0[2 * lat.nearest_site(3, 3)] = 1.0; eng.set_state(q0, v0);
    Port<R> ports[2] = {{lat.nearest_site(2, 1), 1, 0}, {lat.nearest_site(14, 9), 0, 1}}; eng.set_ports(ports);
    ParamMap<R> map; map.base.resize(N); map.base.assign_from(P); map.g_depth = 1.0; map.mu_depth = 60; map.mu_max = 120;
    ControlLayer<R, EsavLFMT<R>> ctl; ctl.block = 64; ctl.enpu_mode = 2; ctl.prepare(lat, map); ctl.set_tau(1.0 / (750 * 0.016 * 2));
    { auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); for (int i = 0; i < N; ++i) { double xx = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), yy = lat.xy[i][1]; if (yy < 5 && xx >= 3 && xx < 4.5) u[i] = 1; if (yy < 5 && xx < 3) v[i] = 0.5; } }
    const std::string path = "experiments/results/phase3/ramp/spiral_blk64_i" + std::to_string(interp) + ".f32";
    FILE* fa = std::fopen(path.c_str(), "wb");
    const int nb = int(FS * secs) / 64; std::vector<R> u(128, 0.0), y(128); double Emax = 0;
    for (int b = 0; b < nb; ++b) {
        eng.process_block(u.data(), y.data(), 64);
        for (int n = 0; n < 64; ++n) { float o[2] = {float(y[2 * n]), float(y[2 * n + 1])}; std::fwrite(o, 4, 2, fa); }
        ctl.control_tick(eng); Emax = std::max(Emax, eng.tracked_energy());
    }
    std::fclose(fa);
    std::printf("{\"file\":\"%s\",\"interp_step\":%d,\"Emax\":%.6e,\"enpu_scaled\":%lld,\"finite\":%s}\n", path.c_str(), interp, Emax, eng.enpu_scaled_, std::isfinite(Emax) ? "true" : "false");
}
