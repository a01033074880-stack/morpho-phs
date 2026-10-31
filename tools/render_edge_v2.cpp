// SPDX-License-Identifier: AGPL-3.0-only
// Phase-2 fixed chiral-edge demo renderer (raw pickups; the in-gap Butterworth bank is applied by
// python/scripts/phase2/p2_edge_demos_v2.py).  40x14 flake, drum loop injected at bottom-edge site x=20,
// pickups at x = 20 -+ d on the bottom edge.  args: sign(+1/0/-1) absorber(0 radial sponge, 1 frame) d [ingap_prefilter 0/1 unused]
// out: experiments/results/phase2/edge_v2/raw_s<sign>_a<abs>_d<d>.f32 (stereo L,R) ; prints JSON (passivity check).
#include "morpho_phs/morpho_phs.hpp"
#include <cstdio>
#include <fstream>
#include <string>
using namespace morpho_phs; using R = double;
int main(int argc, char** argv) {
    const int sign = std::atoi(argv[1]), ab = std::atoi(argv[2]); const double d = std::atof(argv[3]);
    const double FS = 48000, WU = 2 * M_PI * 1500; const int Lx = 40, Ly = 14;
    std::ifstream fin("experiments/audio/drum_loop_48k.f32", std::ios::binary | std::ios::ate); const std::size_t nx = fin.tellg() / 4; fin.seekg(0);
    std::vector<float> x(nx); fin.read((char*)x.data(), nx * 4);
    Honeycomb lat(Lx, Ly); const int N = lat.N; EsavLF<R> eng; EsavLF<R>::Config c; c.fs = FS; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = Potential::Quartic;
    eng.prepare(lat, c, 3); auto& P = eng.params();
    for (int i = 0; i < N; ++i) {
        P.m[i] = 1; P.alpha[i] = 0.05 * WU * WU; P.beta[i] = 0; P.eta[i] = 0; P.g[i] = sign * 0.6 * WU; P.mu[i] = -8.0;
        const double X = lat.xy[i][0], Y = lat.xy[i][1];
        if (ab == 0) { double r = std::hypot(X - 20.0, Y); double s = std::clamp((r - 14) / 6, 0.0, 1.0); P.mu[i] = -8.0 - 3000.0 * s * s; }
        else { double u = X - Y / std::sqrt(3.0); double ds = std::max(0.0, std::max(u - (Lx - 7), 6 - u)); double dt = std::max(0.0, Y - (Ly * std::sqrt(3.0) / 2 - 5));
               double a = std::clamp(ds / 6, 0.0, 1.0), b = std::clamp(dt / 5, 0.0, 1.0); P.mu[i] = -8.0 - 4000.0 * (a * a + b * b); }
    }
    const int src = lat.nearest_site(20.0, 0.0), pl = lat.nearest_site(20.0 - d, 0.0), pr = lat.nearest_site(20.0 + d, 0.0);
    Port<R> ports[3] = {{src, 1, 0}, {pl, 1, 0}, {pr, 1, 0}}; eng.set_ports(ports);
    const std::size_t n = nx + std::size_t(FS * 1.0); R u[3] = {0, 0, 0}, y[3]; double W = 0, E0 = eng.energy();
    char nm[256]; std::snprintf(nm, 256, "experiments/results/phase2/edge_v2/raw_s%+d_a%d_d%.0f.f32", sign, ab, d);
    FILE* f = std::fopen(nm, "wb");
    for (std::size_t i = 0; i < n; ++i) { u[0] = i < nx ? R(x[i]) * 2e4 : 0.0; W += eng.process_sample(u, y); float o[2] = {float(y[1]), float(y[2])}; std::fwrite(o, 4, 2, f); }
    std::fclose(f);
    std::printf("{\"file\":\"%s\",\"sign\":%d,\"absorber\":\"%s\",\"d\":%.1f,\"E_final\":%.6e,\"E0_plus_work\":%.6e,\"passive_ok\":%s}\n", nm, sign, ab ? "frame" : "sponge", d,
                eng.energy(), E0 + W, eng.energy() <= E0 + W + 1e-9 * std::abs(E0 + W) ? "true" : "false");
}
