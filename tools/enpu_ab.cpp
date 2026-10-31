// SPDX-License-Identifier: AGPL-3.0-only
// Phase-2 ENPU A/B(/C) comparison renders.  EsavLFMT (1 thread), 48 kHz, control tick every `block` samples.
//   scen 0 "glide_g_alpha": 16x16 passive plate (mu=-6), struck every 0.5 s; g = 0.6 wu cos(2 pi 0.25 t) (Chern flip),
//                           alpha = 0.05 wu^2 (1 + 4 (1+sin(2 pi 0.5 t))/2)
//   scen 1 "glide_mass"   : same plate, m = 2^(0.5 sin(2 pi 0.3 t))  (pitch glide, frequency ~ m^-1/2: +-1/4 octave)
//   scen 2 "spiral"       : demo-04 self-oscillating 12x12 patch, Barkley spiral -> g, mu (ControlLayer)
// variant 0 = A reproject, 1 = B keep-state, 2 = C selective.  block = 1 gives the per-sample reference.
// Out: experiments/results/phase2/enpu/<scen>_v<variant>_b<block>.f32 (stereo) + .csv (per tick)
#include "morpho_phs/morpho_phs.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
using namespace morpho_phs;
using R = double;
int main(int argc, char** argv) {
    const int scen = std::atoi(argv[1]), variant = std::atoi(argv[2]), block = std::atoi(argv[3]); const double secs = argc > 4 ? std::atof(argv[4]) : 8.0;
    const int interp = argc > 5 ? std::atoi(argv[5]) : 0; const std::string outdir = argc > 6 ? argv[6] : "experiments/results/phase2/enpu";   // Phase 3: g/mu ramp step
    const double FS = 48000, WU = 2 * M_PI * 1500; const char* sn[3] = {"glide_g_alpha", "glide_mass", "spiral"};
    const int L = scen == 2 ? 12 : 16;
    Honeycomb lat(L, L); const int N = lat.N; EsavLFMT<R> eng; EsavLFMT<R>::Config cfg; cfg.fs = FS; cfg.k0 = 0.1 * WU * WU; cfg.kc = WU * WU;
    cfg.potential = Potential::Quartic; cfg.threads = 1; cfg.interp_step = interp; if (scen == 2) cfg.E_cap = 2e6; eng.prepare(lat, cfg, 2);
    auto& P = eng.params();
    for (int i = 0; i < N; ++i) { P.m[i] = 1; P.alpha[i] = 0.05 * WU * WU; P.beta[i] = 0; P.mu[i] = scen == 2 ? 40 : -6; P.eta[i] = scen == 2 ? 2e-4 : 0; P.g[i] = 0.6 * WU; }
    std::vector<R> q0(2 * N, 0.0), v0(2 * N, 0.0); if (scen == 2) v0[2 * lat.nearest_site(3, 3)] = 1.0; eng.set_state(q0, v0);
    Port<R> ports[2] = {{lat.nearest_site(2, 1), 1, 0}, {lat.nearest_site(scen == 2 ? 14 : 19, scen == 2 ? 9 : 12), 0, 1}}; eng.set_ports(ports);
    ParamMap<R> map; map.base.resize(N); map.base.assign_from(P); map.g_depth = 1.0; map.mu_depth = 60; map.mu_max = 120;
    ControlLayer<R, EsavLFMT<R>> ctl; ctl.block = 64; ctl.enpu_mode = variant; ctl.prepare(lat, map); ctl.set_tau(1.0 / (750 * 0.016 * 2));
    if (scen == 2) { auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); for (int i = 0; i < N; ++i) { double xx = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), yy = lat.xy[i][1]; if (yy < 5 && xx >= 3 && xx < 4.5) u[i] = 1; if (yy < 5 && xx < 3) v[i] = 0.5; } }
    SiteParams<R> Pn; Pn.resize(N); Pn.assign_from(P);
    (void)0;
    std::string base = outdir + "/" + sn[scen] + "_v" + std::to_string(variant) + "_b" + std::to_string(block) + (interp ? "_i" + std::to_string(interp) : std::string());
    FILE* fa = std::fopen((base + ".f32").c_str(), "wb"); FILE* fc = std::fopen((base + ".csv").c_str(), "w");
    std::fprintf(fc, "sample,E_before,E_after,s,sav_inconsistency\n");
    const std::size_t n = std::size_t(FS * secs); R u[2], y[2]; double removed = 0, Emax = 0;

    for (std::size_t i = 0; i < n; ++i) {
        const double t = i / FS;
        u[0] = u[1] = 0;
        if (scen < 2 && (i % 24000) < 24) {   // 0.5 ms raised-cosine strike at a rotating site through port 0
            // strike through fixed port 0 (no port relocation -> no pickup jumps)
            u[0] = 4e5 * (1 - std::cos(2 * M_PI * (i % 24000) / 24.0));
        }
        eng.process_block(u, y, 1);
        float o[2] = {float(y[0]), float(y[1])}; std::fwrite(o, 4, 2, fa);
        Emax = std::max(Emax, eng.tracked_energy());
        if ((i + 1) % block == 0) {
            const double Eb = eng.energy(); R s = 1;
            if (scen == 2) {
                if ((i + 1) % 64 == 0 || block == 1) {
                    if (block == 1) {   // reference: medium ticks every 64 samples, parameters interpolated linearly per sample
                        static SiteParams<R> prev, next; static bool init = false;
                        if (!init) { prev.resize(N); next.resize(N); prev.assign_from(P); next.assign_from(P); init = true; }
                        if ((i + 1) % 64 == 0) { prev.assign_from(next); ctl.medium.tick(); ctl.map().apply(ctl.medium.u(), next); }
                        const double w = double(((i + 1) % 64) + 1) / 64.0;
                        for (int k = 0; k < N; ++k) { Pn.g[k] = prev.g[k] + w * (next.g[k] - prev.g[k]); Pn.mu[k] = prev.mu[k] + w * (next.mu[k] - prev.mu[k]); }
                        s = variant == 0 ? eng.enpu(Pn) : (variant == 1 ? eng.enpu_keep_psi(Pn) : eng.enpu_selective(Pn));
                    } else s = ctl.control_tick(eng);
                }
            } else {
                const double tn = (i + 1) / FS;
                for (int k = 0; k < N; ++k) {
                    if (scen == 0) { Pn.g[k] = 0.6 * WU * std::cos(2 * M_PI * 0.25 * tn); Pn.alpha[k] = 0.05 * WU * WU * (1 + 2 * (1 + std::sin(2 * M_PI * 0.5 * tn))); }
                    else Pn.m[k] = std::pow(2.0, 0.5 * std::sin(2 * M_PI * 0.3 * tn));
                }
                s = variant == 0 ? eng.enpu(Pn) : (variant == 1 ? eng.enpu_keep_psi(Pn) : eng.enpu_selective(Pn));
            }
            const double Ea = eng.energy(); removed += std::max(0.0, Eb - Ea);
            if (block == 64 || (i + 1) % 64 == 0) std::fprintf(fc, "%zu,%.9e,%.9e,%.6f,%.4e\n", i + 1, Eb, Ea, double(s), eng.sav_inconsistency());
        }
        (void)t;
    }
    std::fclose(fa); std::fclose(fc);
    std::printf("{\"scen\":\"%s\",\"variant\":%d,\"block\":%d,\"secs\":%.1f,\"enpu_calls\":%lld,\"enpu_scaled\":%lld,\"fallback\":%lld,\"energy_removed_by_enpu\":%.6e,\"Emax\":%.6e,\"finite\":%s}\n",
                sn[scen], variant, block, secs, eng.enpu_calls_, eng.enpu_scaled_, eng.enpu_fallback_, removed, Emax, std::isfinite(Emax) ? "true" : "false");
}
