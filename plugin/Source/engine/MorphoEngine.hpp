// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS plugin — real-time engine (header-only, no JUCE: shared by the plugin, the CLI renderer and benchmarks).
//
// Signal flow
//   FX Edge Disperser : in -> [dry / full-band path] --------------------------------------------+-> out
//                       in -> in-gap Cheby-II bank -> lattice edge port -> chiral edge -> pickups L/R --+
//   FX Defect Freezer : in -> lattice bulk port (input fades out with Freeze; Freeze -> lossless mu=eta=0)
//   Instrument        : MPE voices (bow saw / pluck) -> shared multi-port lattice -> pickups; Life = gated mu > 0
// Real-time rules: no allocation / locks in process(); lattice (re)builds happen in LatticeCore's constructor on a
// non-audio thread and are swapped in through an atomic pointer by the owner (see PluginProcessor).
#pragma once
#include "ChebyBank.hpp"
#include "Lockfree.hpp"
#include "Params.hpp"
#include "morpho_phs/morpho_phs.hpp"
#include "morpho_phs/rt_thread.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#ifndef M_PI  // MORPHO_M_PI_GUARD: MSVC/MinGW do not define M_PI without _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif

namespace morpho_plugin {

using Real = float;   // plugin engine precision (Phase 1 T7 / Phase 3 P2-P4: f32 passes balance & passivity checks)
constexpr int kCtl = 64;               // control period (samples): medium tick + ENPU-C
constexpr int kMaxVoices = 16;
constexpr int kPortSrc = 0, kPortPickL = 1, kPortPickR = 2, kPortBrush = 3, kPortVoice0 = 4;
constexpr int kNumPorts = kPortVoice0 + kMaxVoices;
constexpr int kMaxL = 32, kMaxN = 2 * kMaxL * kMaxL;
constexpr double kWU0 = 2.0 * M_PI * 1500.0;   // reference unit frequency; bond stiffness kc = kWU0^2 (fixed)

inline int lattice_size_from_choice(int c) noexcept { return c <= 0 ? 8 : (c == 1 ? 16 : 32); }
// Machine topology, queried once (first call allocates: message thread / prepareToPlay only).
inline const morpho_phs::rt::Topology& machine_topology() {
    static const morpho_phs::rt::Topology t = morpho_phs::rt::query_topology();
    return t;
}
// Potential choice: 0 Quartic, 1 HQ (fast log-cosh), 2 HQ exact.
// Phase 5c: HQ exact is limited to L <= kHqExactMaxL.  On the i9-13900H it costs 25-56 us/sample at 32x32 at every thread
// count (budget 20.8 us), so at L > 16 the choice runs as HQ fast (the parameter value itself is kept, so presets,
// automation and host state stay valid; the editor shows a note).  Benchmarks can lift the limit (allow_hq_exact_any_L).
constexpr int kHqExactMaxL = 16;
inline int effective_potential_choice(int c, int L, bool allow_hq_exact_any_L = false) noexcept {
    c = std::clamp(c, 0, 2);
    return (c == 2 && L > kHqExactMaxL && !allow_hq_exact_any_L) ? 1 : c;
}
inline bool hq_exact_limited(int c, int L) noexcept { return c == 2 && L > kHqExactMaxL; }
// "High CPU" combinations (editor label / tooltip): any HQ potential at 32x32, HQ exact at 16x16.
inline bool high_cpu_setting(int potential_choice, int L) noexcept {
    const int e = effective_potential_choice(potential_choice, L);
    return (e >= 1 && L > 16) || (e == 2 && L > 8);
}
inline morpho_phs::rt::AutoLoad auto_load(int mode, int potential_choice, int L, bool allow_hq_exact_any_L = false) noexcept {
    const int e = effective_potential_choice(potential_choice, L, allow_hq_exact_any_L);
    morpho_phs::rt::AutoLoad w; w.instrument = mode >= 2; w.hq = e >= 1; w.hq_exact = e == 2;
    return w;
}
inline int threads_from_choice(int c, int L, int mode = 0, int potential_choice = 0) {
    static const int t[7] = {0, 1, 2, 3, 4, 6, 8};
    const int v = t[std::clamp(c, 0, 6)];
    if (v > 0) return v;
    // Auto (Phase 5c): L <= 16: 1 (HQ exact at 16x16: 2); L = 32: FX 2, Instrument 3, HQ fast 3 (capped by the P-cores)
    return morpho_phs::rt::default_threads(L, machine_topology(), auto_load(mode, potential_choice, L));
}
inline int interp_from_choice(int c) noexcept { return c == 0 ? 1 : (c == 1 ? 8 : 0); }
inline morpho_phs::Potential potential_from_choice(int c) noexcept {
    using morpho_phs::Potential;
    return c == 1 ? Potential::QuarticLogcoshFast : (c == 2 ? Potential::QuarticLogcosh : Potential::Quartic);
}

// Real-time threading options of the lattice worker pool (see morpho_phs/rt_thread.hpp; Phase 5 defaults below).
struct RtOptions {
    // Phase 5 defaults: caller computes (host thread = compute thread 0) + 2 workers on the clean P-core CPU set,
    // pure-spin per-sample barrier, OS waits only between blocks after idle_spin (3.5 ms), EcoQoS off, MMCSS on.
    int pin = 3;                 // 0 none, 1 one worker per clean P-core (hard pin), 2 P-core set (Phase 4), 3 CPU set (soft)
    bool mmcss = true;           // Windows only (no-op elsewhere)
    int caller_computes = 1;     // -1 auto (= pin == 0), 0 dedicated pool, 1 host thread is compute thread 0
    long barrier_spin_ns = 50000, barrier_yield_ns = 200000, idle_spin_ns = 3500000, idle_yield_ns = 3600000;
    bool adaptive_spin = false;  // Phase 4 knob (workers spin through the host gap); superseded by idle_spin = 3.5 ms
    bool caller_pool_spin = false;   // dedicated pool: host thread spins (true) or blocks (false) while the pool computes
    int kernel = 1;              // Phase 5: 1 bipartite SIMD stencil, 0 Phase-2..4 CSR gather (bit-identical results)
    bool ersav = false;          // Phase 5: E-RSAV relaxation of the SAV variable (default OFF, see README)
    bool inblock_os_wait = false;    // Phase 5: false = pure-spin per-sample barrier; true = Phase 4 hybrid spin/yield/wait
    bool no_power_throttling = true; // Phase 5: opt compute threads out of EcoQoS (Windows)
    bool operator==(const RtOptions&) const = default;
};
struct BuildSettings {
    int L = 16, threads = 1, potential = 0, interp_choice = 0;
    double fs = 48000.0;
    long spin_iters = 20000;     // unused since Phase 4 (kept for source compatibility)
    bool allow_hq_exact_any_L = false;   // Phase 5c: benchmarks only (plugin: HQ exact runs as HQ fast above 16x16)
    RtOptions rt{};
};

// Effective control values (after smoothing + macro overrides), one set per control tick.
struct ControlValues {
    int mode = 0, potential = 0, exciter = 0, interp_choice = 0;   // D1 (Phase 4): per-sample g/mu ramp by default
    float tone = 1500, chirality = 0.6f, nonlinear = 0.25f, hq = 0.3f, decay = 0.6f, life = 0, morph = 0.3f, tau = 0.0833f,
          freeze = 0, wet = 0.7f, dry = 0.5f, in_gain = 1, out_gain = 1, spread = 0.25f, defect_depth = 0.6f;
    float edge_width = 1.f, edge_crosstalk = 0.f;   // D4 (Phase 4): wet-signal stereo matrix (identity at 1 / 0)
};

struct UiEvent {
    enum Type : int { Pluck = 0, Morphogen = 1, ChiselAdd = 2, ChiselRemove = 3, ClearDefects = 4, ResetField = 5, SeedSpiral = 6 };
    int type = 0; float x = 0.5f, y = 0.5f, amount = 1.f;   // x,y normalised to the flake bounding box (y up)
};

struct Snapshot {
    int L = 0, N = 0, mode = 0; std::uint32_t frame = 0;
    float energy_total = 0, peak_l = 0, peak_r = 0, gate_open = 1; int resets = 0, enpu_scaled = 0, threads = 1;
    std::array<float, kMaxN> energy{};    // per-site local energy density (arbitrary units)
    std::array<float, kMaxN> medium{};    // Barkley u in [0,1]
    std::array<std::uint8_t, kMaxN> defect{};
    // Phase 6a (hero visualiser): per-site displacement and the current port sites (src, pick L/R, brush, voices;
    // -1 = unused).  Filled on the audio thread only while an editor is subscribed (MorphoEngine::snapshot_hz > 0).
    std::array<float, kMaxN> qx{}, qy{};
    std::array<std::int16_t, kNumPorts> port{};
};
// Phase 6a: the audio thread publishes at most this often, and only while an editor is subscribed.
inline constexpr int kMaxSnapshotHz = 30;

// ---------------------------------------------------------------------------------------------------------------
// One lattice instance (size L, T threads).  Constructed/destroyed off the audio thread; everything else RT-safe.
class LatticeCore {
public:
    using Eng = morpho_phs::EsavLFMT<Real>;
    explicit LatticeCore(const BuildSettings& s) : set_(s), lat_(s.L, s.L) {
        N_ = lat_.N;
        Eng::Config cfg; cfg.fs = s.fs; cfg.k0 = 0.1 * kWU0 * kWU0; cfg.kc = kWU0 * kWU0; cfg.c0 = 1.0;
        cfg.E_cap = 2e6 * double(N_) / 288.0; cfg.potential = potential_from_choice(effective_potential_choice(s.potential, s.L, s.allow_hq_exact_any_L)); cfg.threads = std::max(1, s.threads);
        cfg.spin_iters = s.spin_iters; cfg.interp_step = interp_from_choice(s.interp_choice); cfg.ftz_workers = true;
        cfg.pin = s.rt.pin; cfg.mmcss = s.rt.mmcss; cfg.caller_computes = s.rt.caller_computes;
        cfg.barrier_spin_ns = s.rt.barrier_spin_ns; cfg.barrier_yield_ns = s.rt.barrier_yield_ns;
        cfg.idle_spin_ns = s.rt.idle_spin_ns; cfg.idle_yield_ns = s.rt.idle_yield_ns; cfg.adaptive_spin = s.rt.adaptive_spin; cfg.caller_pool_spin = s.rt.caller_pool_spin;
        cfg.kernel = s.rt.kernel; cfg.ersav = s.rt.ersav; cfg.inblock_os_wait = s.rt.inblock_os_wait; cfg.no_power_throttling = s.rt.no_power_throttling;
        eng_.prepare(lat_, cfg, kNumPorts);
        cur_potential_ = s.potential;
        // geometry (normalised bounding box)
        double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
        for (auto& p : lat_.xy) { x0 = std::min(x0, p[0]); x1 = std::max(x1, p[0]); y0 = std::min(y0, p[1]); y1 = std::max(y1, p[1]); }
        bx0_ = x0; bw_ = x1 - x0; by0_ = y0; bh_ = y1 - y0;
        // edge-mode absorbing frame (scaled from Phase 2 render_edge_v2: 6/40 of the width, 5/14 of the height)
        const double L = s.L, wA = std::max(1.2, 0.15 * L), wT = std::max(1.0, 0.3 * L * std::sqrt(3.0) / 2.0);
        absorb_.fill(0.f); defect_.fill(0);
        for (int i = 0; i < N_; ++i) {
            const double X = lat_.xy[i][0], Y = lat_.xy[i][1], u = X - Y / std::sqrt(3.0);
            const double ds = std::max(0.0, std::max(u - (L - 1 - wA), wA - u)), dt = std::max(0.0, Y - (bh_ - wT));
            const double a = std::clamp(ds / wA, 0.0, 1.0), b = std::clamp(dt / wT, 0.0, 1.0);
            absorb_[i] = float(a * a + b * b);
        }
        {
            morpho_phs::ParamMap<Real> init; init.base.resize(N_); init.base.assign_from(eng_.params());
            ctl_.block = kCtl; ctl_.enpu_mode = 2; ctl_.prepare(lat_, init);
        }
        ctl_.set_tau(0.0833, s.fs);
        for (auto& p : ports_) p = morpho_phs::Port<Real>{0, 1, 0};
        u_.fill(0); y_.fill(0);
    }
    LatticeCore(const LatticeCore&) = delete;
    LatticeCore& operator=(const LatticeCore&) = delete;

    int L() const noexcept { return set_.L; }
    int N() const noexcept { return N_; }
    const BuildSettings& settings() const noexcept { return set_; }
    const morpho_phs::Honeycomb& lattice() const noexcept { return lat_; }
    Eng& engine() noexcept { return eng_; }
    morpho_phs::Barkley<Real>& medium() noexcept { return ctl_.medium; }

    // normalised (x right, y up in [0,1]) <-> nearest site (O(N), called only on events / topology changes)
    int site_at(double xn, double yn) const noexcept { return lat_.nearest_site(bx0_ + xn * bw_, by0_ + yn * bh_); }
    float xn(int i) const noexcept { return float((lat_.xy[i][0] - bx0_) / bw_); }
    float yn(int i) const noexcept { return float((lat_.xy[i][1] - by0_) / bh_); }

    // ----- control tick (audio thread, every kCtl samples) -----
    void control_tick(const ControlValues& cv, double fs) noexcept {
        if (cv.potential != cur_potential_) {   // raw choice tracked; the engine gets the L-limited one (Phase 5c)
            eng_.set_potential(potential_from_choice(effective_potential_choice(cv.potential, set_.L, set_.allow_hq_exact_any_L)));
            cur_potential_ = cv.potential;
        }
        eng_.set_interp_step(interp_from_choice(cv.interp_choice));
        if (cv.tau != last_tau_) { ctl_.set_tau(cv.tau, fs); last_tau_ = cv.tau; }
        // physical mapping (docs/morpho_preset_spec.md, "Parameter -> physics")
        const double r = tone_ratio(cv.tone, fs), mt = 1.0 / (r * r), smt = std::sqrt(mt);
        const double dd = 1.0 - double(cv.decay);
        double mu0 = -(2.0 + 600.0 * dd * dd * dd);
        mu0 = mu0 + (120.0 - mu0) * double(cv.life);               // Life: blend toward gated negative damping +120
        const double eta0 = 2e-4 * double(cv.life);
        const double fr = cv.mode == 1 ? double(cv.freeze) : 0.0;  // Freeze: mu, eta -> 0 (lossless, conserves energy)
        const double g0 = double(cv.chirality) * 0.6 * kWU0 * smt;
        const double a0 = double(cv.nonlinear) * 0.2 * kWU0 * kWU0;
        const double b0 = cv.potential == 0 ? 0.0 : double(cv.hq) * 0.2 * kWU0 * kWU0;
        const double dm = 1.0 + 15.0 * double(cv.defect_depth);
        auto& M = ctl_.map();
        auto& B = M.base;
        for (int i = 0; i < N_; ++i) {
            const bool d = defect_[i] != 0;
            double mu = mu0 * mt;
            if (cv.mode == 0) mu -= 4000.0 * mt * absorb_[i];
            mu *= (1.0 - fr);
            B.m[i] = Real(mt * (d ? dm : 1.0));
            B.g[i] = Real(d ? 0.0 : g0);
            B.alpha[i] = Real(a0); B.beta[i] = Real(b0);
            B.mu[i] = Real(mu); B.eta[i] = Real(eta0 * mt * (1.0 - fr));
        }
        M.g_depth = Real(cv.morph);
        M.mu_depth = Real(60.0 * mt * cv.morph * (1.0 - fr));
        M.mu_max = Real(cv.life > 0 ? 120.0 * mt * (1.0 - fr) : 0.0);
        M.alpha_depth = Real(cv.morph);
        ctl_.control_tick(eng_);
        // ports
        place_ports(cv);
        tone_ratio_ = r;
    }
    static double tone_ratio(double tone, double fs) noexcept {
        // keep the CFL number h*omega_max/2 <= 0.8 (omega_max ~ 2.47 WU0 r for k0 = 0.1, kc = 1, degree 3)
        const double rmax = 0.8 * 2.0 * fs / (2.47 * kWU0);
        return std::clamp(tone / 1500.0, 0.05, rmax);
    }
    double current_tone_ratio() const noexcept { return tone_ratio_; }

    void place_ports(const ControlValues& cv) noexcept {
        if (cv.mode != port_mode_ || std::abs(cv.spread - port_spread_) > 0.01f) {
            port_mode_ = cv.mode; port_spread_ = cv.spread;
            const double sp = cv.spread;
            if (cv.mode == 0) {   // edge: source on the bottom edge, pickups along the same edge
                const double Lx = set_.L;
                ports_[kPortSrc] = {lat_.nearest_site(Lx / 2, 0.0), 1, 0};
                ports_[kPortPickL] = {lat_.nearest_site(Lx / 2 - sp * Lx, 0.0), 1, 0};
                ports_[kPortPickR] = {lat_.nearest_site(Lx / 2 + sp * Lx, 0.0), 1, 0};
            } else {
                ports_[kPortSrc] = {site_at(0.5, 0.5), Real(0.8), Real(0.6)};
                ports_[kPortPickL] = {site_at(0.5 - sp, 0.5 - 0.5 * sp), 1, 0};
                ports_[kPortPickR] = {site_at(0.5 + sp, 0.5 + 0.5 * sp), 0, 1};
            }
        }
    }
    morpho_phs::Port<Real>& port(int k) noexcept { return ports_[k]; }

    // ----- audio (sub-block of n <= kCtl samples): u = n x kNumPorts forces (sample-major) -----
    Real* inputs() noexcept { return u_.data(); }
    const Real* outputs() const noexcept { return y_.data(); }
    double process(int n) noexcept {
        eng_.set_ports(std::span<const morpho_phs::Port<Real>>(ports_.data(), kNumPorts));
        return eng_.process_block(u_.data(), y_.data(), n);
    }
    void clear_state() noexcept {
        eng_.clear(); eng_.snap_ramp();
        auto u = ctl_.medium.u_mut(); auto v = ctl_.medium.v_mut();
        for (int i = 0; i < N_; ++i) { u[i] = 0; v[i] = 0; }
    }
    void set_defect(int site, bool on) noexcept {
        defect_[site] = on;
        for (int k = lat_.nbr_ptr[site]; k < lat_.nbr_ptr[site + 1]; ++k) defect_[lat_.nbr_idx[k]] = on;
    }
    void clear_defects() noexcept { defect_.fill(0); }
    void seed_spiral(double xn, double yn) noexcept {   // Phase-1 demo seed: excited bar + refractory tail
        auto u = ctl_.medium.u_mut(); auto v = ctl_.medium.v_mut();
        const double cx = bx0_ + xn * bw_, cy = by0_ + yn * bh_, len = 0.35 * bh_;
        for (int i = 0; i < N_; ++i) {
            const double X = lat_.xy[i][0] - cx, Y = lat_.xy[i][1] - cy;
            if (Y > -len && Y < 0.0 && X >= 0 && X < 1.5) u[i] = 1;
            if (Y > -len && Y < 0.0 && X < 0 && X > -3.0) v[i] = Real(0.5);
        }
    }
    void fill_snapshot(Snapshot& s) const noexcept {
        s.L = set_.L; s.N = N_; s.threads = eng_.threads();
        const auto& P = eng_.params(); const double k0 = 0.1 * kWU0 * kWU0;
        auto mu = ctl_.medium.u();
        for (int i = 0; i < N_; ++i) {
            const double qx = eng_.q_at(2 * i), qy = eng_.q_at(2 * i + 1), vx = eng_.v_at(2 * i), vy = eng_.v_at(2 * i + 1);
            s.energy[i] = float(0.5 * P.m[i] * (vx * vx + vy * vy) + 0.5 * k0 * (qx * qx + qy * qy));
            s.medium[i] = float(mu[i]); s.defect[i] = defect_[i];
            s.qx[i] = float(qx); s.qy[i] = float(qy);
        }
        for (int k = 0; k < kNumPorts; ++k) {
            const auto& p = ports_[k];
            s.port[k] = std::int16_t((p.bx != 0 || p.by != 0) && p.site >= 0 && p.site < N_ ? p.site : -1);
        }
        s.energy_total = float(eng_.tracked_energy());
        s.enpu_scaled = int(eng_.enpu_scaled_);
        s.gate_open = eng_.tracked_energy() < eng_.config().E_cap ? 1.f : 0.f;
    }

private:
    BuildSettings set_;
    morpho_phs::Honeycomb lat_;
    int N_ = 0;
    Eng eng_;
    morpho_phs::ControlLayer<Real, Eng> ctl_;
    std::array<float, kMaxN> absorb_{};
    std::array<std::uint8_t, kMaxN> defect_{};
    std::array<morpho_phs::Port<Real>, kNumPorts> ports_{};
    std::array<Real, kCtl * kNumPorts> u_{}, y_{};
    double bx0_ = 0, bw_ = 1, by0_ = 0, bh_ = 1, tone_ratio_ = 1;
    int cur_potential_ = 0, port_mode_ = -1; float port_spread_ = -1, last_tau_ = -1;
};

// ---------------------------------------------------------------------------------------------------------------
// MPE voice manager.  Lower zone: channel 1 = master (bend +-2 st, applies to all), channels 2..16 = members
// (bend +-48 st, channel pressure, CC74 timbre).  Non-MPE input on channel 1 works as plain polyphony.
struct Voice {
    bool active = false, releasing = false;
    int ch = 0, note = 0, site = -1, timbre_q = -1; std::uint32_t age = 0;
    float vel = 0, pressure = 0, timbre = 0.5f, env = 0, phase = 0, pluck_left = 0, pluck_len = 1;
};

class VoiceBank {
public:
    std::array<Voice, kMaxVoices> v{};
    std::array<float, 17> bend{};          // semitones per channel (index 1..16)
    float master_bend_range = 2.f, member_bend_range = 48.f;
    std::uint32_t clock = 0;

    int note_on(int ch, int note, float vel) noexcept {
        int k = -1; std::uint32_t oldest = ~0u;
        for (int i = 0; i < kMaxVoices; ++i) if (!v[i].active) { k = i; break; }
        if (k < 0) for (int i = 0; i < kMaxVoices; ++i) if (v[i].age < oldest) { oldest = v[i].age; k = i; }   // steal oldest
        Voice& x = v[k];
        const bool keep_phase = x.active;
        x = Voice{}; x.active = true; x.ch = ch; x.note = note; x.vel = vel; x.pressure = ch == 1 ? 1.f : 0.f; x.age = ++clock;
        if (keep_phase) x.env = 0;
        return k;
    }
    void note_off(int ch, int note) noexcept {
        for (auto& x : v) if (x.active && !x.releasing && x.ch == ch && x.note == note) x.releasing = true;
    }
    void all_off() noexcept { for (auto& x : v) x.releasing = true; }
    void pitch_bend(int ch, int value14) noexcept {
        const float b = (float(value14) - 8192.f) / 8192.f;
        bend[std::clamp(ch, 1, 16)] = b * (ch == 1 ? master_bend_range : member_bend_range);
    }
    void pressure(int ch, float p) noexcept { for (auto& x : v) if (x.active && x.ch == ch) x.pressure = p; }
    void timbre(int ch, float t) noexcept { for (auto& x : v) if (x.active && x.ch == ch) x.timbre = t; }
    float freq(const Voice& x) const noexcept {
        const float semis = float(x.note - 69) + bend[std::clamp(x.ch, 1, 16)] + (x.ch != 1 ? bend[1] : 0.f);
        return 440.f * std::exp2(semis / 12.f);
    }
};

// ---------------------------------------------------------------------------------------------------------------
// Top-level RT engine: owns filter bank, voices, event queue and the snapshot buffer; uses an externally owned
// LatticeCore (swapped by the owner between process() calls).
class MorphoEngine {
public:
    SpscQueue<UiEvent, 256> ui_events;
    TripleBuffer<Snapshot> snapshots;
    VoiceBank voices;

    void prepare(double fs) noexcept {
        fs_ = fs; bank_.design(fs, 1.0); bank_.reset(); phase_ = 0; snap_counter_ = 1 << 30; fade_ = 1.f;
    }
    // Process n samples (any n).  in/out may alias.  core must be non-null.
    void process(LatticeCore& core, const ControlValues& cv_in, const float* inL, const float* inR, float* outL, float* outR, int n) noexcept {
        int done = 0;
        while (done < n) {
            if (phase_ == 0) { cv_ = cv_in; tick(core); }
            const int m = std::min(n - done, kCtl - phase_);
            render(core, inL ? inL + done : nullptr, inR ? inR + done : nullptr, outL + done, outR + done, m);
            done += m; phase_ = (phase_ + m) % kCtl;
        }
        // Phase 6a: decimated, subscription-gated snapshot.  No editor (snapshot_hz == 0) -> nothing is computed.
        // At most one publish per process() call and at most kMaxSnapshotHz per second; the first block after an
        // editor subscribes publishes immediately.  Lock-free, allocation-free (triple buffer, in-place fill).
        const int hz = std::min(snapshot_hz.load(std::memory_order_relaxed), kMaxSnapshotHz);
        if (hz <= 0) { snap_counter_ = 1 << 30; return; }
        snap_counter_ += n;
        if (snap_counter_ >= int(fs_ / double(hz))) {
            snap_counter_ = 0;
            if (time_publish) {
                const auto t0 = std::chrono::steady_clock::now();
                publish_snapshot(core);
                const auto ns = (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
                ++publish_count; publish_ns_sum += ns; publish_ns_max = std::max(publish_ns_max, ns);
            } else {
                publish_snapshot(core);
            }
        }
    }
    // Phase 6a: editor subscription (message thread writes, audio thread reads).  0 = no editor / hidden.
    std::atomic<int> snapshot_hz{0};
    // Phase 6a: optional publish timing for morpho_cli bench (set before processing; read after it stops)
    bool time_publish = false;
    std::uint64_t publish_count = 0, publish_ns_sum = 0, publish_ns_max = 0;
    int resets() const noexcept { return resets_; }
    void reset_dsp() noexcept { bank_.reset(); for (auto& x : voices.v) x = Voice{}; phase_ = 0; }
    void set_fade(float target, int samples) noexcept { fade_target_ = target; fade_step_ = (target - fade_) / float(std::max(1, samples)); }
    float fade() const noexcept { return fade_; }

private:
    void tick(LatticeCore& core) noexcept {
        UiEvent e;
        while (ui_events.pop(e)) handle_event(core, e);
        {   // in-gap bank follows the lattice: f -> sqrt((r f)^2 + beta/m/(2 pi)^2), beta/m = hq*0.2*(2 pi 1500 r)^2
            const double r = LatticeCore::tone_ratio(cv_.tone, fs_);
            const double onsite = cv_.potential == 0 ? 0.0 : double(cv_.hq) * 0.2 * (1500.0 * r) * (1500.0 * r);
            bank_.maybe_redesign(fs_, r, onsite);
        }
        // voices -> port sites (pitch class along x, timbre along y); note-on stimulates the medium
        for (int k = 0; k < kMaxVoices; ++k) {
            Voice& x = voices.v[k];
            auto& p = core.port(kPortVoice0 + k);
            if (!x.active) continue;
            const int tq = int(x.timbre * 15.99f);
            if (x.site < 0 || tq != x.timbre_q) {
                const bool fresh = x.site < 0;
                const double xn = 0.12 + 0.76 * double(((x.note % 24) + 24) % 24) / 23.0, yn = 0.2 + 0.6 * double(tq) / 15.0;
                x.site = core.site_at(xn, yn); x.timbre_q = tq;
                const double ang = 0.7 * x.note; p = {x.site, Real(std::cos(ang)), Real(std::sin(ang))};
                if (fresh) {
                    core.medium().stimulate(x.site, Real(0.6 * cv_.morph * x.vel));
                    if (cv_.exciter == 1) { x.pluck_len = std::clamp(float(fs_) / (2.f * voices.freq(x)), 12.f, 240.f); x.pluck_left = x.pluck_len; }
                }
            }
        }
        core.control_tick(cv_, fs_);
    }
    void handle_event(LatticeCore& core, const UiEvent& e) noexcept {
        const int s = core.site_at(std::clamp(e.x, 0.f, 1.f), std::clamp(e.y, 0.f, 1.f));
        switch (e.type) {
            case UiEvent::Pluck: core.port(kPortBrush) = {s, Real(0.8), Real(0.6)}; brush_left_ = brush_len_ = 24.f; brush_amp_ = e.amount; break;
            case UiEvent::Morphogen: core.medium().stimulate(s, Real(e.amount)); break;
            case UiEvent::ChiselAdd: core.set_defect(s, true); break;
            case UiEvent::ChiselRemove: core.set_defect(s, false); break;
            case UiEvent::ClearDefects: core.clear_defects(); break;
            case UiEvent::ResetField: core.clear_state(); break;
            case UiEvent::SeedSpiral: core.seed_spiral(e.x, e.y); break;
            default: break;
        }
    }
    void render(LatticeCore& core, const float* inL, const float* inR, float* outL, float* outR, int m) noexcept {
        Real* u = core.inputs();
        const double r = core.current_tone_ratio();
        const float drive = 2e4f, in_g = cv_.in_gain;
        const float src_gain = cv_.mode == 1 ? (1.f - cv_.freeze) : 1.f;
        for (int n = 0; n < m; ++n) {
            Real* un = u + n * kNumPorts;
            for (int k = 0; k < kNumPorts; ++k) un[k] = 0;
            const float xin = in_g * 0.5f * ((inL ? inL[n] : 0.f) + (inR ? inR[n] : (inL ? inL[n] : 0.f)));
            if (cv_.mode == 0) un[kPortSrc] = Real(drive * bank_.process(xin));
            else if (cv_.mode == 1) un[kPortSrc] = Real(drive * src_gain * xin);
            else un[kPortSrc] = Real(drive * xin * 0.25f);   // instrument: audio input as optional extra exciter
            if (brush_left_ > 0) {   // raised-cosine strike (0.5 ms) through the brush port
                const float ph = (brush_len_ - brush_left_) / brush_len_;
                un[kPortBrush] = Real(2e5f * brush_amp_ * 0.5f * (1.f - std::cos(6.2831853f * ph)));
                brush_left_ -= 1.f;
            }
            for (int k = 0; k < kMaxVoices; ++k) {
                Voice& x = voices.v[k];
                if (!x.active) continue;
                const float target = x.releasing ? 0.f : 1.f;
                x.env += (target - x.env) * (x.releasing ? 0.0015f : 0.004f);
                if (x.releasing && x.env < 1e-4f) { x.active = false; x.site = -1; continue; }
                float f = 0;
                if (cv_.exciter == 0) {   // PolyBLEP saw
                    const float dt = std::min(0.45f, voices.freq(x) / float(fs_));
                    x.phase += dt; if (x.phase >= 1.f) x.phase -= 1.f;
                    float s = 2.f * x.phase - 1.f; const float t = x.phase;
                    if (t < dt) { const float a = t / dt; s -= a + a - a * a - 1.f; }
                    else if (t > 1.f - dt) { const float a = (t - 1.f) / dt; s -= a * a + a + a + 1.f; }
                    f = 1.5e4f * s * x.env * x.vel * (0.35f + 0.65f * x.pressure);
                } else if (x.pluck_left > 0) {
                    const float ph = (x.pluck_len - x.pluck_left) / x.pluck_len;
                    f = 2e5f * x.vel * 0.5f * (1.f - std::cos(6.2831853f * ph)) * (24.f / x.pluck_len);
                    x.pluck_left -= 1.f;
                    if (x.pluck_left <= 0) x.releasing = true;
                }
                un[kPortVoice0 + k] = Real(kVoiceGain * f);
            }
        }
        core.process(m);
        const Real* y = core.outputs();
        const int li = core.L() <= 8 ? 0 : (core.L() <= 16 ? 1 : 2);
        const float norm = float(kOutNorm[cv_.mode] * kSizeNorm[cv_.mode][li] / r);
        bool finite = true;
        // D4: edge crosstalk c mixes the two edge pickups (a = 1 - c/2, b = c/2), then edge width w scales the side
        // signal (M/S).  Combined symmetric matrix [[m11, m12], [m12, m11]]; skipped at the identity (w = 1, c = 0) so
        // the default stays bit-identical to Phase 3.
        const float xa = 1.f - 0.5f * cv_.edge_crosstalk, xb = 0.5f * cv_.edge_crosstalk;
        const float wp = 0.5f * (1.f + cv_.edge_width), wq = 0.5f * (1.f - cv_.edge_width);
        const float m11 = wp * xa + wq * xb, m12 = wp * xb + wq * xa;
        const bool stereo_matrix = !(cv_.edge_width == 1.f && cv_.edge_crosstalk == 0.f);
        for (int n = 0; n < m; ++n) {
            float yl = float(y[n * kNumPorts + kPortPickL]) * norm, yr = float(y[n * kNumPorts + kPortPickR]) * norm;
            if (stereo_matrix) { const float a = yl, b = yr; yl = m11 * a + m12 * b; yr = m12 * a + m11 * b; }
            finite = finite && std::isfinite(yl) && std::isfinite(yr);
            const float dl = inL ? inL[n] : 0.f, dr = inR ? inR[n] : dl;
            fade_ = (fade_step_ > 0) ? std::min(fade_target_, fade_ + fade_step_) : std::max(fade_target_, fade_ + fade_step_);
            const float l = (cv_.dry * cv_.in_gain * dl + cv_.wet * yl) * cv_.out_gain * fade_;
            const float rr = (cv_.dry * cv_.in_gain * dr + cv_.wet * yr) * cv_.out_gain * fade_;
            outL[n] = safety(l); outR[n] = safety(rr);
            peak_l_ = std::max(peak_l_, std::abs(outL[n])); peak_r_ = std::max(peak_r_, std::abs(outR[n]));
        }
        if (!finite || !std::isfinite(core.engine().tracked_energy())) {   // never expected; recover silently
            core.clear_state(); ++resets_;
            for (int n = 0; n < m; ++n) { outL[n] = 0; outR[n] = 0; }
        }
    }
    static float safety(float x) noexcept {   // transparent below 0.9, smooth limit to +-1 above
        const float a = std::abs(x);
        if (a <= 0.9f) return x;
        const float o = 0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f);
        return std::copysign(o, x);
    }
    void publish_snapshot(LatticeCore& core) noexcept {
        Snapshot& s = snapshots.write_buffer();
        core.fill_snapshot(s);
        for (int k = 0; k < kMaxVoices; ++k) if (!voices.v[k].active) s.port[kPortVoice0 + k] = -1;   // stale voice ports
        if (brush_left_ <= 0.f) s.port[kPortBrush] = -1;
        s.mode = cv_.mode; s.frame = ++frame_; s.resets = resets_; s.peak_l = peak_l_; s.peak_r = peak_r_;
        peak_l_ = peak_r_ = 0;
        snapshots.publish();
    }

public:
    // output normalisation per mode (pickup velocity -> full scale), set from the Phase-3 offline renders
    inline static float kVoiceGain = 300.f;   // calibrated 18:35: bow MPE peak ~-9 dBFS at L16 (was ~-54 without it)
    inline static double kOutNorm[3] = {1.75, 4.0, 2.0e-3};   // FX edge recalibrated 18:42 on the downstream (hot) pickup: wet peak ~-3 dBFS on drums
    inline static double kSizeNorm[3][3] = {{1.0, 1, 1.4}, {0.42, 1, 2.4}, {1, 1, 1}};   // per lattice size 8/16/32

private:
    double fs_ = 48000.0;
    ControlValues cv_{};
    InGapBank bank_;
    int phase_ = 0, snap_counter_ = 0, resets_ = 0; std::uint32_t frame_ = 0;
    float brush_left_ = 0, brush_len_ = 24, brush_amp_ = 1, peak_l_ = 0, peak_r_ = 0;
    float fade_ = 1.f, fade_target_ = 1.f, fade_step_ = 0.f;
};

}  // namespace morpho_plugin
