// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS plugin — engine host (no JUCE).  Parameter smoothing, macro overrides, MIDI/MPE routing and the
// off-audio-thread lattice swap.  Used identically by the JUCE processor and the CLI renderer / benchmark.
//
// Thread contract
//   * set_param() / set_macro_map()            : any thread (atomics / triple buffer)
//   * request_build() / collect_garbage()       : message thread (allocates; constructs LatticeCore + its workers)
//   * process(), note_on()/note_off()/...        : audio thread only (no allocation, no locks, no syscalls except the
//                                                  futex wake of idle worker threads inside EsavLFMT)
#pragma once
#include "MorphoEngine.hpp"
#include <memory>

namespace morpho_plugin {

class EngineHost {
public:
    EngineHost() {
        for (int p = 0; p < kNumParams; ++p) raw_[p].store(kParams[p].def, std::memory_order_relaxed);
        macro_buf_.write_buffer() = default_macro_map(); macro_buf_.publish();
        macro_buf_.write_buffer() = default_macro_map();
    }
    ~EngineHost() {
        delete pending_.exchange(nullptr); delete retired_.exchange(nullptr); active_.reset();
    }

    // ---------------- parameters ----------------
    void set_param(int p, float plain) noexcept { raw_[p].store(plain, std::memory_order_relaxed); }
    float get_param(int p) const noexcept { return raw_[p].load(std::memory_order_relaxed); }
    void set_macro_map(const MacroMap& m) noexcept { macro_buf_.write_buffer() = m; macro_buf_.publish(); }   // single writer: message thread
    // effective value after macro override (what the engine is targeting); valid on the audio thread
    float effective(int p) const noexcept { return target_[p]; }

    // ---------------- lattice lifecycle (message thread) ----------------
    BuildSettings desired_settings(double fs) const noexcept {
        BuildSettings s; s.fs = fs;
        s.L = lattice_size_from_choice(int(get_param(pLattice) + 0.5f));
        s.potential = int(get_param(pPotential) + 0.5f);
        s.allow_hq_exact_any_L = allow_hq_exact_any_L_;
        // Auto threads (Phase 5c) depend on mode and potential: switching e.g. Quartic -> HQ at 32x32 rebuilds the
        // lattice with 3 threads (normal 256-sample crossfade).  Uses the raw parameters, not macro overrides.
        s.threads = threads_from_choice(int(get_param(pThreads) + 0.5f), s.L, int(get_param(pMode) + 0.5f), s.potential);
        s.interp_choice = int(get_param(pInterp) + 0.5f);
        s.spin_iters = spin_iters_;
        s.rt = rt_;
        if (thread_override_ > 0) s.threads = thread_override_;
        return s;
    }
    void set_spin_iters(long n) noexcept { spin_iters_ = n; }
    // Phase 4: threading options and an explicit thread count (CLI / benchmarks; 0 = use the Threads parameter)
    void set_rt_options(const RtOptions& o) noexcept { rt_ = o; }
    const RtOptions& rt_options() const noexcept { return rt_; }
    void set_thread_override(int t) noexcept { thread_override_ = t; }
    // Phase 5c: benchmarks may run HQ exact at 32x32 (the plugin never sets this)
    void set_allow_hq_exact_any_L(bool b) noexcept { allow_hq_exact_any_L_ = b; }
    // Build a new lattice if size / threads / fs changed.  Returns true if a build was queued.
    bool request_build(double fs, bool force = false) {
        const BuildSettings s = desired_settings(fs);
        if (!force && built_valid_ && s.L == built_.L && s.threads == built_.threads && s.fs == built_.fs && s.rt == built_.rt
            && s.allow_hq_exact_any_L == built_.allow_hq_exact_any_L) return false;
        auto* core = new LatticeCore(s);
        built_ = s; built_valid_ = true;
        delete pending_.exchange(core, std::memory_order_acq_rel);   // an older, never-installed pending core
        return true;
    }
    // Offline use (CLI / prepareToPlay while the audio thread is stopped): install immediately.
    void install_now(double fs) {
        prepare(fs);
        request_build(fs, true);
        active_.reset(pending_.exchange(nullptr));
        engine_.set_fade(1.f, 1);
        smoothing_reset_ = true;
    }
    void collect_garbage() { delete retired_.exchange(nullptr, std::memory_order_acq_rel); }
    void prepare(double fs) noexcept { fs_ = fs; engine_.prepare(fs); smoothing_reset_ = true; }

    // ---------------- MIDI / MPE (audio thread) ----------------
    void note_on(int ch, int note, float vel) noexcept { engine_.voices.note_on(ch, note, vel); }
    void note_off(int ch, int note) noexcept { engine_.voices.note_off(ch, note); }
    void pitch_bend(int ch, int v14) noexcept { engine_.voices.pitch_bend(ch, v14); }
    void channel_pressure(int ch, float p) noexcept { engine_.voices.pressure(ch, p); }
    void timbre(int ch, float t) noexcept { engine_.voices.timbre(ch, t); }
    void all_notes_off() noexcept { engine_.voices.all_off(); }

    MorphoEngine& engine() noexcept { return engine_; }
    LatticeCore* active_core() noexcept { return active_.get(); }
    int swaps() const noexcept { return swaps_; }

    // ---------------- audio ----------------
    void process(const float* inL, const float* inR, float* outL, float* outR, int n) noexcept {
        // 1) lattice swap protocol: fade out 256 samples -> swap pointer -> fade in
        if (pending_.load(std::memory_order_acquire) != nullptr) {
            if (!active_) { install_pending(); engine_.set_fade(1.f, 256); }
            else if (engine_.fade() <= 0.f) { if (retired_.load(std::memory_order_acquire) == nullptr) { install_pending(); engine_.set_fade(1.f, 256); } }
            else if (!fading_out_) { engine_.set_fade(0.f, 256); fading_out_ = true; }
        }
        if (!active_) { for (int i = 0; i < n; ++i) { outL[i] = 0; outR[i] = 0; } return; }
        // 2) control values: macro override + smoothing (per call; the engine also ramps g/mu inside the block)
        update_targets();
        smooth(n);
        engine_.process(*active_, cv_, inL, inR, outL, outR, n);
    }

private:
    void install_pending() noexcept {
        LatticeCore* nc = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (!nc) return;
        LatticeCore* old = active_.release();
        active_.reset(nc);
        if (old) retired_.store(old, std::memory_order_release);   // deleted by collect_garbage() on the message thread
        for (auto& v : engine_.voices.v) v.site = -1;              // re-place voice ports on the new lattice
        fading_out_ = false; ++swaps_;
    }
    void update_targets() noexcept {
        macro_buf_.update();
        const MacroMap& mm = macro_buf_.read_buffer();
        for (int p = 0; p < kNumParams; ++p) target_[p] = raw_[p].load(std::memory_order_relaxed);
        for (int k = 0; k < mm.count; ++k) {
            const auto& t = mm.t[k];
            if (t.macro < 0 || t.macro > 7 || !is_engine_param(t.param)) continue;
            const float x = std::clamp(target_[pMacro1 + t.macro], 0.f, 1.f);
            const auto& d = kParams[t.param];
            float v;
            if (d.log && t.lo > 0 && t.hi > 0) v = t.lo * std::pow(t.hi / t.lo, x);
            else v = t.lo + (t.hi - t.lo) * x;
            if (d.kind == PKind::Choice) v = std::round(v);
            target_[t.param] = std::clamp(v, d.min, d.max);
        }
    }
    void smooth(int n) noexcept {
        // one-pole, 20 ms; log-domain for tone and tau
        const float a = smoothing_reset_ ? 1.f : 1.f - std::exp(-float(n) / (0.020f * float(fs_)));
        smoothing_reset_ = false;
        auto sm = [&](float& s, float t) { s += a * (t - s); };
        auto smlog = [&](float& s, float t) { s = std::exp(std::log(s) + a * (std::log(t) - std::log(s))); };
        cv_.mode = int(target_[pMode] + 0.5f);
        cv_.potential = int(target_[pPotential] + 0.5f);
        cv_.interp_choice = int(target_[pInterp] + 0.5f);
        cv_.exciter = int(target_[pExciter] + 0.5f);
        smlog(cv_.tone, target_[pTone]); smlog(cv_.tau, target_[pTau]);
        sm(cv_.chirality, target_[pChirality]); sm(cv_.nonlinear, target_[pNonlinear]); sm(cv_.hq, target_[pHQ]);
        sm(cv_.decay, target_[pDecay]); sm(cv_.life, target_[pLife]); sm(cv_.morph, target_[pMorph]);
        sm(cv_.freeze, target_[pFreeze]); sm(cv_.wet, target_[pWet]); sm(cv_.dry, target_[pDry]);
        sm(cv_.spread, target_[pSpread]); sm(cv_.defect_depth, target_[pDefectDepth]);
        sm(cv_.edge_width, target_[pEdgeWidth]); sm(cv_.edge_crosstalk, target_[pEdgeCrosstalk]);
        sm(cv_.in_gain, std::pow(10.f, target_[pInGain] / 20.f)); sm(cv_.out_gain, std::pow(10.f, target_[pOutGain] / 20.f));
        // snap tiny residues so ENPU-C sees "unchanged" m / alpha / beta and keeps its fast path
        auto snap = [](float& s, float t, float eps) { if (std::abs(s - t) <= eps) s = t; };
        snap(cv_.tone, target_[pTone], 1e-3f); snap(cv_.nonlinear, target_[pNonlinear], 1e-5f); snap(cv_.hq, target_[pHQ], 1e-5f);
        snap(cv_.defect_depth, target_[pDefectDepth], 1e-5f);
        snap(cv_.edge_width, target_[pEdgeWidth], 1e-5f); snap(cv_.edge_crosstalk, target_[pEdgeCrosstalk], 1e-5f);
    }

    double fs_ = 48000.0;
    std::array<std::atomic<float>, kNumParams> raw_{};
    std::array<float, kNumParams> target_{};
    TripleBuffer<MacroMap> macro_buf_;
    ControlValues cv_{};
    bool smoothing_reset_ = true, fading_out_ = false, built_valid_ = false;
    BuildSettings built_{};
    long spin_iters_ = 20000;
    RtOptions rt_{};
    int thread_override_ = 0;
    bool allow_hq_exact_any_L_ = false;
    MorphoEngine engine_;
    std::unique_ptr<LatticeCore> active_;
    std::atomic<LatticeCore*> pending_{nullptr}, retired_{nullptr};
    int swaps_ = 0;
};

}  // namespace morpho_plugin
