// SPDX-License-Identifier: AGPL-3.0-only
// Phase-1 audio demos rendered with the header-only core (48 kHz, 16-bit WAV, peak-normalised to -1 dBFS).
// Build: zig c++ -std=c++20 -O3 -march=native -Iinclude tools/render_demos.cpp -o build/render_demos
#include "morpho_phs/morpho_phs.hpp"
#include <cstdio>
#include <fstream>
#include <string>

using namespace morpho_phs;
using R = double;
static const double FS = 48000, WU = 2 * M_PI * 1500;

static void write_wav(const std::string& path, const std::vector<float>& L, const std::vector<float>& Rr) {
    const int ch = 2; const std::size_t n = L.size(); float peak = 1e-12f;
    for (std::size_t i = 0; i < n; ++i) peak = std::max(peak, std::max(std::abs(L[i]), std::abs(Rr[i])));
    const float gn = 0.891f / peak;   // -1 dBFS
    std::ofstream f(path, std::ios::binary);
    auto w32 = [&](uint32_t v) { f.write((const char*)&v, 4); }; auto w16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    f.write("RIFF", 4); w32(36 + uint32_t(n * ch * 2)); f.write("WAVEfmt ", 8); w32(16); w16(1); w16(ch); w32(48000); w32(48000 * ch * 2); w16(ch * 2); w16(16);
    f.write("data", 4); w32(uint32_t(n * ch * 2));
    for (std::size_t i = 0; i < n; ++i) for (float s : {L[i], Rr[i]}) { int16_t v = int16_t(std::lround(std::clamp(s * gn, -1.f, 1.f) * 32767)); f.write((const char*)&v, 2); }
}
static EsavLF<R>::Config base_cfg() { EsavLF<R>::Config c; c.fs = FS; c.k0 = 0.1 * WU * WU; c.kc = WU * WU; c.potential = Potential::Quartic; return c; }
static void set_uniform(SiteParams<R>& P, int N, double g, double mu, double alpha, double eta) {
    for (int i = 0; i < N; ++i) { P.m[i] = 1; P.alpha[i] = alpha; P.beta[i] = 0; P.mu[i] = mu; P.eta[i] = eta; P.g[i] = g; }
}

// Demo 1: chiral edge-mode disperser on a 40x14 flake.  In: drum loop at a bottom-edge site; out: pickups 8 cells
// left (L) and right (R) of the injection along the same edge.  sign = +1 / 0 / -1 (Omega+, Omega=0, Omega-).
// Radial absorbing sponge (mu<=0 only, so passivity is untouched) beyond 14 lattice units from the source, so the
// chiral wave is absorbed after passing the pickup instead of circulating the whole flake perimeter back to the
// other pickup.  bandpass=true feeds the drum loop through a 2-pole resonator at 776 Hz (inside bulk gap 1).
static void demo_edge(int sign, const std::vector<float>& x_in, const char* path, FILE* js, bool bandpass = false, const char* key = nullptr) {
    std::vector<float> x(x_in);
    if (bandpass) {   // RBJ band-pass (constant 0 dB peak), f0 = 776 Hz, Q = 1.5
        const double w0 = 2 * M_PI * 776.0 / FS, al = std::sin(w0) / (2 * 1.5), a0 = 1 + al;
        const double b0 = al / a0, b2 = -al / a0, a1 = -2 * std::cos(w0) / a0, a2 = (1 - al) / a0;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& s : x) { double y = b0 * s + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = s; y2 = y1; y1 = y; s = float(y); }
    }
    Honeycomb lat(40, 14); const int N = lat.N; EsavLF<R> eng; auto cfg = base_cfg(); eng.prepare(lat, cfg, 3);
    set_uniform(eng.params(), N, sign * 0.6 * WU, -8.0, 0.05 * WU * WU, 0.0);
    for (int i = 0; i < N; ++i) { double d = std::hypot(lat.xy[i][0] - 20.0, lat.xy[i][1]); if (d > 14) eng.params().mu[i] = -8.0 - 3000.0 * std::min(1.0, (d - 14) / 6) * std::min(1.0, (d - 14) / 6); }
    const int src = lat.nearest_site(20.0, 0.0), pl = lat.nearest_site(12.0, 0.0), pr = lat.nearest_site(28.0, 0.0);
    Port<R> ports[3] = {{src, 1, 0}, {pl, 1, 0}, {pr, 1, 0}}; eng.set_ports(ports);
    const std::size_t n = x.size() + std::size_t(FS * 1.0);
    std::vector<float> L(n), Rr(n); R u[3] = {0, 0, 0}, y[3]; double Emax = 0, W = 0, E0 = eng.energy();
    for (std::size_t i = 0; i < n; ++i) {
        u[0] = i < x.size() ? R(x[i]) * 2e4 : 0.0;
        W += eng.process_sample(u, y); L[i] = float(y[1]); Rr[i] = float(y[2]);
        Emax = std::max(Emax, eng.tracked_energy());
    }
    double eL = 0, eR = 0; for (std::size_t i = 0; i < n; ++i) { eL += double(L[i]) * L[i]; eR += double(Rr[i]) * Rr[i]; }
    write_wav(path, L, Rr);
    char kb[64]; if (key) std::snprintf(kb, 64, "%s", key); else std::snprintf(kb, 64, "edge_sign%+d", sign);
    std::fprintf(js, "  \"%s\": {\"bandpass_776Hz\": %s, \"file\": \"%s\", \"seconds\": %.3f, \"pickup_energy_R_over_L_dB\": %.2f, \"E_final\": %.6e, \"E0_plus_input_work\": %.6e, \"passive_ok\": %s},\n",
                 kb, bandpass ? "true" : "false", path, n / FS, 10 * std::log10(eR / eL), eng.energy(), E0 + W, (eng.energy() <= E0 + W + 1e-9 * std::abs(E0 + W)) ? "true" : "false");
}

// Demo 2: self-oscillating patch (12x12): mu>0 gain with energy gate + eta saturation; a Barkley spiral modulates
// g (Chern-sign flips where excited) and mu at 750 Hz through ENPU.  Stereo pickups at two far sites.
static void demo_selfosc(const char* path, FILE* js) {
    Honeycomb lat(12, 12); const int N = lat.N; EsavLF<R> eng; auto cfg = base_cfg(); cfg.E_cap = 2e6; eng.prepare(lat, cfg, 2);
    set_uniform(eng.params(), N, 0.6 * WU, 40.0, 0.05 * WU * WU, 2e-4);
    std::vector<R> q0(2 * N, 0.0), v0(2 * N, 0.0); v0[2 * lat.nearest_site(3, 3)] = 1.0; eng.set_state(q0, v0);
    Port<R> ports[2] = {{lat.nearest_site(2, 1), 1, 0}, {lat.nearest_site(14, 9), 0, 1}}; eng.set_ports(ports);
    ParamMap<R> map; map.base.resize(N); map.base.assign_from(eng.params()); map.g_depth = 1.0; map.mu_depth = 60; map.mu_max = 120;
    ControlLayer<R> ctl; ctl.prepare(lat, map); ctl.medium.substeps = 2;   // tau = 41.7 ms -> spiral period ~0.24 s
    { auto u = ctl.medium.u_mut(); auto v = ctl.medium.v_mut(); for (int i = 0; i < N; ++i) { double xx = lat.xy[i][0] - lat.xy[i][1] / std::sqrt(3.0), yy = lat.xy[i][1]; if (yy < 5 && xx >= 3 && xx < 4.5) u[i] = 1; if (yy < 5 && xx < 3) v[i] = 0.5; } }
    const std::size_t n = std::size_t(FS * 12);
    std::vector<float> L(n), Rr(n); R u[2] = {0, 0}, y[2]; double Emax = 0; int scaled = 0, ticks = 0, chg_end = 0;
    for (std::size_t i = 0; i < n; ++i) {
        eng.process_sample(u, y); L[i] = float(y[0]); Rr[i] = float(y[1]);
        if ((i & 63) == 63) { scaled += ctl.control_tick(eng) < 1; ++ticks; }
        Emax = std::max(Emax, eng.tracked_energy());
    }
    chg_end = ctl.medium.abs_charge();
    const double h = 1 / FS, mumax = 120, gam = eng.cfl_gamma(), del = h * mumax / (1 - gam * gam), rho = (1 + del) / (1 - del), nd = 2.0 * N;
    double rmsLast = 0; for (std::size_t i = n - 48000; i < n; ++i) rmsLast += double(L[i]) * L[i]; rmsLast = std::sqrt(rmsLast / 48000);
    write_wav(path, L, Rr);
    std::fprintf(js, "  \"selfosc\": {\"file\": \"%s\", \"seconds\": %.1f, \"E_cap\": %.3e, \"max_E\": %.6e, \"bound_rho_Ecap_tilde\": %.6e, \"rho\": %.6f, \"bounded_ok\": %s, \"enpu_ticks\": %d, \"enpu_scaled\": %d, \"spiral_charge_end\": %d, \"rms_last_second_pickup\": %.4e},\n",
                 path, n / FS, cfg.E_cap, Emax, rho * (cfg.E_cap + 0.5 * nd), rho, (Emax + 0.5 * nd <= rho * (cfg.E_cap + 0.5 * nd)) ? "true" : "false", ticks, scaled, chg_end, rmsLast);
}

// Demo 3: MPE chord on ONE shared 16x16 lattice: 3 voices (C3, E3, G3), each its own collocated injection/pickup port;
// excitation = band-limited pulse train at the note f0 with per-voice pressure envelope; voices panned L/C/R.
static void demo_mpe(const char* path, FILE* js) {
    Honeycomb lat(16, 16); const int N = lat.N; EsavLF<R> eng; auto cfg = base_cfg(); eng.prepare(lat, cfg, 3);
    set_uniform(eng.params(), N, 0.6 * WU, -6.0, 0.05 * WU * WU, 0.0);
    Port<R> ports[3] = {{lat.nearest_site(4, 3), 1, 0}, {lat.nearest_site(12, 7), 0.6, 0.8}, {lat.nearest_site(20, 11), 0, 1}}; eng.set_ports(ports);
    const double f0[3] = {130.81, 164.81, 196.00}, t_on[3] = {0.0, 0.6, 1.2}, t_off[3] = {5.0, 5.2, 5.4}, pan[3] = {0.15, 0.5, 0.85};
    const std::size_t n = std::size_t(FS * 7);
    std::vector<float> L(n), Rr(n); R u[3], y[3]; double ph[3] = {0, 0, 0}, W = 0, E0 = eng.energy(); double vpow[3] = {0, 0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        const double t = i / FS;
        for (int k = 0; k < 3; ++k) {
            double env = (t < t_on[k]) ? 0 : (t < t_off[k] ? std::min(1.0, (t - t_on[k]) / 0.05) : std::exp(-(t - t_off[k]) / 0.08));
            env *= 1.0 + 0.15 * std::sin(2 * M_PI * (4.5 + k) * t);           // per-voice pressure (MPE) wobble
            ph[k] += f0[k] / FS; if (ph[k] >= 1) ph[k] -= 1;
            double s = 0; for (int hN = 1; hN <= 20; ++hN) s += std::sin(2 * M_PI * hN * ph[k]) / hN;   // band-limited saw
            u[k] = 3e3 * env * s;
        }
        W += eng.process_sample(u, y);
        double l = 0, r = 0; for (int k = 0; k < 3; ++k) { l += (1 - pan[k]) * y[k]; r += pan[k] * y[k]; vpow[k] += u[k] * y[k] / FS; }
        L[i] = float(l); Rr[i] = float(r);
    }
    write_wav(path, L, Rr);
    std::fprintf(js, "  \"mpe_chord\": {\"file\": \"%s\", \"seconds\": %.1f, \"voices\": 3, \"per_voice_input_work\": [%.6e, %.6e, %.6e], \"E_final\": %.6e, \"E0_plus_input_work\": %.6e, \"passive_ok\": %s}\n",
                 path, n / FS, vpow[0], vpow[1], vpow[2], eng.energy(), E0 + W, (eng.energy() <= E0 + W + 1e-9 * std::abs(E0 + W)) ? "true" : "false");
}

int main() {
    std::ifstream in("experiments/audio/drum_loop_48k.f32", std::ios::binary | std::ios::ate);
    std::vector<float> x(std::size_t(in.tellg()) / 4); in.seekg(0); in.read((char*)x.data(), x.size() * 4);
    FILE* js = std::fopen("experiments/results/phase1/p1_f_audio_demos.json", "w"); std::fprintf(js, "{\n");
    std::vector<float> dry(x.begin(), x.end()); write_wav("experiments/audio/00_input_drum_loop.wav", dry, dry);
    demo_edge(+1, x, "experiments/audio/01_edge_disperser_OmegaPlus.wav", js);
    demo_edge(0, x, "experiments/audio/02_edge_disperser_Omega0_reference.wav", js);
    demo_edge(-1, x, "experiments/audio/03_edge_disperser_OmegaMinus.wav", js);
    demo_edge(+1, x, "experiments/audio/06_edge_disperser_gapband_OmegaPlus.wav", js, true, "edge_gapband_sign+1");
    demo_edge(-1, x, "experiments/audio/07_edge_disperser_gapband_OmegaMinus.wav", js, true, "edge_gapband_sign-1");
    demo_selfosc("experiments/audio/04_selfoscillating_spiral_patch.wav", js);
    demo_mpe("experiments/audio/05_mpe_chord_shared_lattice.wav", js);
    std::fprintf(js, "}\n"); std::fclose(js);
    std::printf("done\n");
}
