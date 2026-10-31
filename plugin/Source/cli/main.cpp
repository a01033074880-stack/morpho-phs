// SPDX-License-Identifier: AGPL-3.0-only
// morpho_cli — headless renderer, benchmark and preset validator for MORPHO-PHS.  Uses exactly the plugin's engine
// (EngineHost / MorphoEngine / LatticeCore) and preset loader; no GUI, no audio device.
//
//   morpho_cli render-modes  <out_dir> [--lattice 0|1|2] [--seconds S] [--potential 0|1|2]
//   morpho_cli render-presets <presets_dir> <out_dir> [--seconds S]
//   morpho_cli render <preset.morpho> <out.wav> [--seconds S] [--input drums|noise|silence|phrase]
//   morpho_cli bench <out_basename> [--blocks 64,128,512] [--sizes 8,16,32] [--threads 1,2,3,4,6,8]
//                    [--seconds S] [--mode 0|1|2] [--potential 0|1|2] [--interp 0|1|2] [--fs 48000]
//                    Phase 4: [--pin none|pcores|pset] [--mmcss 0|1] [--pool auto|caller|dedicated] [--load N]
//                             [--barrier-spin-us U] [--idle-spin-us U] [--adaptive 0|1] [--caller-spin 0|1] [--paced 0|1] [--tag NAME]
//                    Phase 5: [--pin cpuset] [--kernel stencil|csr] [--ersav 0|1] [--inblock-os-wait 0|1] [--ecoqos 0|1]
//                             [--caller-cpuset 0|1] [--caller-mmcss 0|1] [--variant NAME] (NAME is only a label for the rows)
//                    Defaults since Phase 5 = the plugin defaults: --pin cpuset --mmcss 1 --pool caller --kernel stencil
//                    --ersav 0 --inblock-os-wait 0 --ecoqos 0 --idle-spin-us 3500 --adaptive 0.
//                    Every row also logs start time, an in-process clock estimate (clk_ghz, dependent FP-add chain on the
//                    caller thread right before the row) and the barrier OS-wait counters.
//                    Phase 6a: [--snapshot-hz H] (0 = editor closed, the default; H > 0 = editor open: the engine publishes
//                             the visualiser snapshot at min(H, 30) Hz and a reader thread consumes it like the UI does;
//                             the publish cost is timed and reported as pub_us / pub_max_us / pub_pct of the block budget)
//                    Phase 5c: [--hq-exact-any-L 0|1] (1 = really run HQ exact at 32x32; the plugin maps it to HQ fast)
//                    --threads takes any count (1..64; 0 = Auto: the plugin's mode/potential-aware default); --load N runs N busy-loop
//                    threads during the measurement (contention test).  Pass criterion (32x32, block 128):
//                    0 overruns and p99/budget < 0.7 -> PASS; <= 0.1 % overruns and p99/budget < 0.7 -> NEAR.
//   morpho_cli validate <presets_dir_or_file> [--factory 1]   (Phase 6b: --factory 1 adds the factory-bank rules)
//   Phase 6b:
//   morpho_cli qa <presets_dir_or_file> <out_dir> [--play S] [--tail S] [--cost S] [--bits 16|24|32] [--fade-demo S]
//                    Renders the standard demo (instrument: DemoPhrase; FX: DemoInput = drums + arpeggio) for `play`
//                    seconds, then `tail` seconds of silence, to <out_dir>/<stem>.wav; logs per-block energy against the
//                    energy gate (E_cap), output-limiter hits, NaN resets; then (if --cost > 0) times the preset paced
//                    like a host (block 128, Auto threads) for `cost` seconds.  One JSON row per preset in qa.json.
//                    Phase 6b2: [--only "name or file part,..."] [--seed N] [--noise portable|std] [--no-wav 1]
//                    (qa default: --noise portable --seed 12345, i.e. the same FX demo input on every platform; --noise std
//                    reproduces the Phase 6b input, whose noise differs between libstdc++ and the MSVC STL)
//   morpho_cli check-presets <presets_dir>   load rules (HQ-exact fallback, Auto threads, round trip, finite render)
//   bench ... [--preset file.morpho]   (Phase 6b: runs the preset's own parameters / macros / lattice state; L, mode and
//                    potential come from the preset; --threads 0 = Auto)
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../engine/EngineHost.hpp"
#include "../PresetIO.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned int __stdcall timeBeginPeriod(unsigned int);   // winmm (avoid <windows.h> here)
#if defined(_MSC_VER)
#pragma comment(lib, "winmm.lib")
#endif
#endif
#include <random>
#ifndef M_PI  // MORPHO_M_PI_GUARD: MSVC/MinGW do not define M_PI without _USE_MATH_DEFINES
#define M_PI 3.14159265358979323846
#endif

using namespace morpho_plugin;

namespace {

struct Args {
    juce::StringArray pos; juce::StringPairArray opt;
    Args(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            juce::String a(argv[i]);
            if (a.startsWith("--") && i + 1 < argc) { opt.set(a.substring(2), argv[++i]); }
            else pos.add(a);
        }
    }
    juce::String get(const char* k, const char* d) const { return opt.containsKey(k) ? opt[k] : juce::String(d); }
    std::vector<int> ints(const char* k, const char* d) const {
        juce::StringArray t; t.addTokens(get(k, d), ",", ""); std::vector<int> v; for (auto& s : t) v.push_back(s.getIntValue()); return v;
    }
};

// ---------------------------------------------------------------- deterministic test material
struct TestInput {
    enum Kind { Drums, Noise, Silence };
    // Phase 6b2: std::normal_distribution is implementation-defined (libstdc++ and the MSVC STL turn the same mt19937
    // stream into different Gaussian sequences), so the FX demo input differed between the box and the i9.  qa takes
    // --seed N (noise seed) and --noise portable (Box-Muller on the raw mt19937 words: same sequence on every platform,
    // up to libm ULPs).  qa defaults to portable since 6b2; render and the other commands keep the std noise.
    inline static unsigned kSeed = 12345; inline static bool kPortable = false;
    Kind kind = Drums; double fs = 48000; std::mt19937 rng{kSeed}; std::normal_distribution<float> nd_std{0.f, 1.f};
    bool have_spare = false; double spare = 0;
    float nd(std::mt19937& g) {
        if (!kPortable) return nd_std(g);
        if (have_spare) { have_spare = false; return float(spare); }
        const double u1 = (double(g()) + 1.0) / 4294967297.0, u2 = double(g()) / 4294967296.0;   // u1 in (0, 1]
        const double r = std::sqrt(-2.0 * std::log(u1)), th = 2.0 * M_PI * u2;
        spare = r * std::sin(th); have_spare = true; return float(r * std::cos(th));
    }
    long n = 0; float hp = 0, bp1 = 0, bp2 = 0;
    float next() {
        const double t = double(n++) / fs;
        if (kind == Silence) return 0.f;
        if (kind == Noise) return (std::fmod(t, 2.0) < 0.4) ? 0.25f * nd(rng) : 0.f;
        // 120 BPM: kick on beats, snare on 2/4, closed hat on eighths, chord stab every bar
        const double beat = t * 2.0, ph = beat - std::floor(beat), bt = ph / 2.0;
        const int b = int(std::floor(beat));
        float x = 0;
        { const double f = 45.0 + 75.0 * std::exp(-bt * 30.0); x += 0.55f * float(std::sin(2 * M_PI * f * bt) * std::exp(-bt * 9.0)); }   // kick
        if (b % 2 == 1) { const float w = nd(rng); bp1 += 0.25f * (w - bp1); x += float(0.35 * std::exp(-bt * 22.0)) * (bp1 * 1.6f + 0.4f * float(std::sin(2 * M_PI * 185 * bt))); }
        { const double e = beat * 2.0, eph = (e - std::floor(e)) / 4.0; const float w = nd(rng); const float h = w - hp; hp = w; x += float(0.12 * std::exp(-eph * 120.0)) * h; }
        { const double bar = t / 2.0, bph = (bar - std::floor(bar)) * 2.0;   // stab: minor chord, 0.4 s
          if (bph < 0.4) { float s = 0; for (double f : {220.0, 261.63, 329.63, 440.0}) s += float(2.0 * (std::fmod(f * t, 1.0)) - 1.0);
                           bp2 += 0.08f * (s - bp2); x += 0.10f * bp2 * float(std::exp(-bph * 4.0)); } }
        return x;
    }
};

// MPE phrase on member channels 2..4 (lower zone), with glides, pressure swells and CC74 sweeps
struct Phrase {
    struct Ev { double t; int type, ch, a; float f; };   // type 0 on 1 off 2 bend(a=14bit) 3 pressure(f) 4 timbre(f)
    std::vector<Ev> ev; size_t k = 0;
    explicit Phrase(double secs) {
        const int notes[] = {48, 55, 60, 63, 58, 53, 60, 67};
        for (int i = 0; i * 1.0 < secs - 1.0; ++i) {
            const double t0 = i * 1.0; const int ch = 2 + i % 3, nt = notes[i % 8];
            ev.push_back({t0, 0, ch, nt, 0.8f});
            for (int s = 1; s <= 20; ++s) {
                const double tt = t0 + s * 0.04;
                ev.push_back({tt, 3, ch, 0, 0.3f + 0.7f * float(std::sin(M_PI * s / 20.0))});
                ev.push_back({tt, 4, ch, 0, float(s) / 20.f});
                if (i % 2 == 1) ev.push_back({tt, 2, ch, 8192 + int(8191.0 * (2.0 / 48.0) * s / 20.0), 0});   // glide +2 st
            }
            ev.push_back({t0 + 0.85, 1, ch, nt, 0});
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    }
    void dispatch(EngineHost& h, double t_end) {
        while (k < ev.size() && ev[k].t < t_end) {
            const auto& e = ev[k++];
            switch (e.type) {
                case 0: h.note_on(e.ch, e.a, e.f); break;
                case 1: h.note_off(e.ch, e.a); break;
                case 2: h.pitch_bend(e.ch, e.a); break;
                case 3: h.channel_pressure(e.ch, e.f); break;
                default: h.timbre(e.ch, e.f); break;
            }
        }
    }
};


// Phase 6b: standard demo material (deterministic; synthetic, no samples).
// FX demo input: 0-4 s drums (TestInput pattern), 4-8 s plucked minor-7 arpeggio + low sustained dyad, then silence.
// Scaled so the 8 s programme measures about -18 LUFS-I (python/qa: calibrated once, kDemoInputGain).
struct DemoInput {
    double fs = 48000, play = 8.0; long n = 0; TestInput drums;
    std::array<float, 6> ph{}; float lp = 0, env = 0, padph1 = 0, padph2 = 0, padlp = 0; int last_step = -1, note_idx = 0;
    inline static float kDemoInputGain = 0.7f;   // calibrated: dry demo input = -20.1 LUFS-I, peak -3.1 dBFS (Phase 6b)
    float next() {
        const double t = double(n++) / fs;
        if (t >= play) return 0.f;
        if (t < 4.0) return kDemoInputGain * drums.next();
        const double tt = t - 4.0; const int step = int(tt * 4.0);    // 1/8 notes at 120 BPM
        static const float notes[8] = {57, 60, 64, 67, 69, 67, 64, 60};
        if (step != last_step) { last_step = step; note_idx = step % 8; env = 1.f; }
        const float f = 440.f * std::pow(2.f, (notes[note_idx] - 69.f) / 12.f);
        ph[0] += f / float(fs); if (ph[0] >= 1.f) ph[0] -= 1.f;
        env *= std::exp(-1.f / (0.18f * float(fs)));
        const float saw = 2.f * ph[0] - 1.f;
        lp += (0.08f + 0.25f * env) * (saw - lp);
        padph1 += 110.f / float(fs); if (padph1 >= 1.f) padph1 -= 1.f;
        padph2 += 164.81f / float(fs); if (padph2 >= 1.f) padph2 -= 1.f;
        const float pad = (2.f * padph1 - 1.f) + (2.f * padph2 - 1.f);
        padlp += 0.02f * (pad - padlp);
        const float fadeout = float(std::clamp((play - t) / 0.3, 0.0, 1.0));
        return kDemoInputGain * fadeout * (0.32f * env * lp + 0.10f * padlp);
    }
};

// Instrument demo phrase (MPE, member channels 2..7): a short line, then a held three-note chord with a pressure swell
// and a slow timbre sweep; everything released at `play` - 0.0 s.  Same for every preset (loudness comparability).
struct DemoPhrase {
    struct Ev { double t; int type, ch, a; float f; };
    std::vector<Ev> ev; size_t k = 0;
    explicit DemoPhrase(double play) {
        const double line_end = std::min(4.0, play * 0.5);
        const int line[8] = {48, 51, 55, 58, 60, 55, 53, 51};
        const double dt = line_end / 8.0;
        for (int i = 0; i < 8; ++i) {
            const double t0 = i * dt; const int ch = 2 + i % 3;
            ev.push_back({t0, 0, ch, line[i], i % 4 == 0 ? 0.9f : 0.7f});   // pressure / timbre only reach active voices
            ev.push_back({t0 + 1e-4, 4, ch, 0, 0.35f + 0.05f * float(i)});
            ev.push_back({t0 + 1e-4, 3, ch, 0, 0.7f});
            ev.push_back({t0 + 0.8 * dt, 1, ch, line[i], 0});
        }
        const int chord[3] = {48, 55, 63};
        const double c0 = line_end, c1 = std::max(c0 + 0.5, play - 0.05);
        for (int v = 0; v < 3; ++v) {
            const int ch = 5 + v;
            ev.push_back({c0 + 0.03 * v, 0, ch, chord[v], 0.75f});
            ev.push_back({c0 + 0.03 * v + 1e-4, 4, ch, 0, 0.3f});
            ev.push_back({c0 + 0.03 * v + 1e-4, 3, ch, 0, 0.4f});
            for (int s = 1; s <= 40; ++s) {
                const double tt = c0 + (c1 - c0) * s / 41.0, x = double(s) / 40.0;
                ev.push_back({tt, 3, ch, 0, float(0.4 + 0.5 * std::sin(M_PI * x))});
                ev.push_back({tt, 4, ch, 0, float(0.3 + 0.5 * x)});
                if (v == 2) ev.push_back({tt, 2, ch, 8192 + int(8191.0 * (0.3 / 48.0) * std::sin(2 * M_PI * 5.0 * (tt - c0)) * x), 0});
            }
            ev.push_back({c1, 1, ch, chord[v], 0});
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    }
    void dispatch(EngineHost& h, double t_end) {
        while (k < ev.size() && ev[k].t < t_end) {
            const auto& e = ev[k++];
            switch (e.type) {
                case 0: h.note_on(e.ch, e.a, e.f); break;
                case 1: h.note_off(e.ch, e.a); break;
                case 2: h.pitch_bend(e.ch, e.a); break;
                case 3: h.channel_pressure(e.ch, e.f); break;
                default: h.timbre(e.ch, e.f); break;
            }
        }
    }
};

void apply_preset(EngineHost& h, const PresetData& p) {
    for (int i = 0; i < kNumParams; ++i) h.set_param(i, p.values[(size_t)i]);
    h.set_macro_map(p.macros);
}
void push_preset_state(EngineHost& h, const PresetData& p) {
    for (auto& d : p.defects) h.engine().ui_events.push({UiEvent::ChiselAdd, d.x, d.y, 1.f});
    for (auto& m : p.morphogen) h.engine().ui_events.push({UiEvent::Morphogen, m.x, m.y, m.amount});
    if (p.has_spiral) h.engine().ui_events.push({UiEvent::SeedSpiral, p.spiral.x, p.spiral.y, 1.f});
}

struct RenderStats { double peak = 0, rms = 0, secs = 0, cpu_s = 0, energy_end = 0; int resets = 0, L = 0, threads = 0; bool finite = true; };

RenderStats render(EngineHost& h, const juce::File& out, double secs, TestInput::Kind input, bool phrase, int bits = 24, double fs = 48000) {
    h.install_now(fs);
    TestInput in; in.kind = input; in.fs = fs;
    Phrase ph(secs);
    const int B = 128; const long total = long(secs * fs);
    juce::AudioBuffer<float> buf(2, (int)total);
    std::vector<float> il(B), ir(B);
    RenderStats st; double ss = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long pos = 0; pos < total; pos += B) {
        const int n = (int)std::min<long>(B, total - pos);
        if (phrase) ph.dispatch(h, double(pos + n) / fs);
        for (int i = 0; i < n; ++i) { il[(size_t)i] = in.next(); ir[(size_t)i] = il[(size_t)i]; }
        h.process(il.data(), ir.data(), buf.getWritePointer(0, (int)pos), buf.getWritePointer(1, (int)pos), n);
    }
    st.cpu_s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    for (int c = 0; c < 2; ++c) for (long i = 0; i < total; ++i) {
        const float v = buf.getSample(c, (int)i); st.finite &= std::isfinite(v);
        st.peak = std::max(st.peak, (double)std::abs(v)); ss += double(v) * v;
    }
    st.rms = std::sqrt(ss / double(2 * total)); st.secs = secs; st.resets = h.engine().resets();
    if (auto* c = h.active_core()) { st.L = c->L(); st.threads = c->engine().threads(); st.energy_end = c->engine().tracked_energy(); }
    out.getParentDirectory().createDirectory();
    out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(out.createOutputStream().release());
    if (auto* w = wav.createWriterFor(os.get(), fs, 2, bits, {}, 0)) {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer(w);
        writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    }
    return st;
}

juce::var stats_json(const juce::String& name, const juce::String& file, const RenderStats& s) {
    auto* o = new juce::DynamicObject();
    o->setProperty("name", name); o->setProperty("file", file);
    o->setProperty("peak_dbfs", s.peak > 0 ? 20.0 * std::log10(s.peak) : -999.0);
    o->setProperty("rms_dbfs", s.rms > 0 ? 20.0 * std::log10(s.rms) : -999.0);
    o->setProperty("seconds", s.secs); o->setProperty("render_cpu_seconds", s.cpu_s);
    o->setProperty("realtime_factor", s.cpu_s / s.secs);
    o->setProperty("lattice", s.L); o->setProperty("threads", s.threads);
    o->setProperty("nan_resets", s.resets); o->setProperty("finite", s.finite);
    o->setProperty("tracked_energy_end", s.energy_end);
    return juce::var(o);
}

int cmd_render_modes(const Args& a) {
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[1]);
    const double secs = a.get("seconds", "8").getDoubleValue();
    const int lat = a.get("lattice", "1").getIntValue(), pot = a.get("potential", "0").getIntValue();
    if (a.opt.containsKey("edge-norm-scale"))   // calibration only: scales the FX-edge output normalisation
        MorphoEngine::kOutNorm[0] *= a.get("edge-norm-scale", "1").getDoubleValue();
    struct Job { const char* name; int mode; TestInput::Kind in; bool phrase; std::vector<std::pair<int, float>> set; bool spiral; };
    const std::vector<Job> jobs = {
        {"fx_edge_disperser", 0, TestInput::Drums, false, {}, false},
        {"fx_edge_disperser_chir_neg", 0, TestInput::Drums, false, {{pChirality, -0.6f}}, false},
        {"fx_edge_disperser_chir_zero", 0, TestInput::Drums, false, {{pChirality, 0.f}}, false},
        {"fx_defect_freezer", 1, TestInput::Drums, false, {{pFreeze, 0.f}}, false},
        {"fx_defect_freezer_frozen", 1, TestInput::Drums, false, {{pFreeze, 0.85f}, {pDecay, 0.9f}}, false},
        {"instrument_bow_mpe", 2, TestInput::Silence, true, {{pExciter, 0}, {pDry, 0.f}}, false},
        {"instrument_pluck_mpe", 2, TestInput::Silence, true, {{pExciter, 1}, {pDry, 0.f}, {pDecay, 0.8f}}, false},
        {"instrument_life_spiral", 2, TestInput::Silence, true, {{pExciter, 1}, {pDry, 0.f}, {pLife, 0.35f}, {pMorph, 0.6f}}, true},
    };
    juce::Array<juce::var> summary;
    for (auto& j : jobs) {
        EngineHost h;
        h.set_param(pMode, (float)j.mode); h.set_param(pLattice, (float)lat); h.set_param(pPotential, (float)pot);
        for (auto& kv : j.set) h.set_param(kv.first, kv.second);
        if (j.mode == 1 && j.set.size() > 0 && j.set[0].second > 0.5f) {   // freezer: chisel a defect ring first
            for (int k = 0; k < 10; ++k) h.engine().ui_events.push({UiEvent::ChiselAdd, 0.5f + 0.25f * std::cos(0.628f * k), 0.5f + 0.25f * std::sin(0.628f * k), 1.f});
        }
        if (j.spiral) h.engine().ui_events.push({UiEvent::SeedSpiral, 0.5f, 0.5f, 1.f});
        const auto f = dir.getChildFile(juce::String(j.name) + "_L" + juce::String(lattice_size_from_choice(lat)) + ".wav");
        const auto st = render(h, f, secs, j.in, j.phrase);
        std::printf("%-34s L%-2d peak %6.1f dBFS  rms %6.1f dBFS  rt %.3f  resets %d  %s\n", j.name, st.L,
                    20 * std::log10(st.peak + 1e-12), 20 * std::log10(st.rms + 1e-12), st.cpu_s / st.secs, st.resets, st.finite ? "finite" : "NON-FINITE");
        summary.add(stats_json(j.name, f.getFileName(), st));
    }
    // dry reference input
    {
        TestInput in; const long total = long(secs * 48000); juce::AudioBuffer<float> b(2, (int)total);
        for (long i = 0; i < total; ++i) { const float v = in.next(); b.setSample(0, (int)i, v); b.setSample(1, (int)i, v); }
        const auto f = dir.getChildFile("input_drums_reference.wav"); f.deleteFile();
        juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os(f.createOutputStream().release());
        if (auto* w = wav.createWriterFor(os.get(), 48000, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    }
    dir.getChildFile("render_modes_L" + juce::String(lattice_size_from_choice(lat)) + ".json").replaceWithText(juce::JSON::toString(summary));
    return 0;
}

juce::Array<juce::File> preset_files(const juce::File& f) {
    juce::Array<juce::File> v;
    if (f.isDirectory()) v = f.findChildFiles(juce::File::findFiles, false, "*.morpho");   // not recursive: matches the BinaryData glob (presets/retired/ is not shipped)
    else v.add(f);
    v.sort();
    return v;
}

int cmd_render_presets(const Args& a) {
    const auto files = preset_files(juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[1]));
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[2]);
    const double secs = a.get("seconds", "6").getDoubleValue();
    juce::Array<juce::var> summary; int fails = 0;
    for (auto& pf : files) {
        PresetData p; PresetIssues is;
        if (!preset_from_file(pf, p, is)) { std::printf("FAIL %s: %s\n", pf.getFileName().toRawUTF8(), is.errors.joinIntoString("; ").toRawUTF8()); ++fails; continue; }
        EngineHost h; apply_preset(h, p); push_preset_state(h, p);
        const int mode = (int)p.values[pMode];
        const auto out = dir.getChildFile(pf.getFileNameWithoutExtension() + ".wav");
        const auto st = render(h, out, secs, mode == 2 ? TestInput::Silence : TestInput::Drums, mode == 2, 16);
        std::printf("%-30s %-12s L%-2d peak %6.1f  rms %6.1f dBFS  rt %.3f  resets %d %s\n", p.name.toRawUTF8(), p.category.toRawUTF8(), st.L,
                    20 * std::log10(st.peak + 1e-12), 20 * std::log10(st.rms + 1e-12), st.cpu_s / st.secs, st.resets, st.finite ? "" : "NON-FINITE");
        auto v = stats_json(p.name, out.getFileName(), st); v.getDynamicObject()->setProperty("category", p.category);
        summary.add(v);
        if (!st.finite || st.resets) ++fails;
    }
    dir.getChildFile("render_presets.json").replaceWithText(juce::JSON::toString(summary));
    return fails ? 1 : 0;
}

int cmd_render(const Args& a) {
    PresetData p; PresetIssues is;
    if (!preset_from_file(juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[1]), p, is)) { std::printf("%s\n", is.errors.joinIntoString("\n").toRawUTF8()); return 2; }
    EngineHost h; apply_preset(h, p); push_preset_state(h, p);
    const juce::String inp = a.get("input", (int)p.values[pMode] == 2 ? "phrase" : "drums");
    const auto kind = inp == "noise" ? TestInput::Noise : (inp == "drums" ? TestInput::Drums : TestInput::Silence);
    const auto st = render(h, juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[2]), a.get("seconds", "8").getDoubleValue(), kind, inp == "phrase");
    std::printf("%s\n", juce::JSON::toString(stats_json(p.name, a.pos[2], st)).toRawUTF8());
    return st.finite ? 0 : 1;
}

int cmd_validate(const Args& a) {
    const auto files = preset_files(juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[1]));
    int bad = 0; juce::StringArray names;
    for (auto& f : files) {
        PresetData p; PresetIssues is;
        const bool ok = preset_from_file(f, p, is);
        if (names.contains(p.name)) is.errors.add("duplicate preset name \"" + p.name + "\"");
        if (ok && a.get("factory", "0").getIntValue() != 0) factory_checks(p, is);
        names.add(p.name);
        std::printf("%s %-34s %-12s %s%s\n", ok && is.ok() ? "OK  " : "FAIL", f.getFileName().toRawUTF8(), p.category.toRawUTF8(),
                    is.errors.joinIntoString("; ").toRawUTF8(), is.warnings.isEmpty() ? "" : (" [warn] " + is.warnings.joinIntoString("; ")).toRawUTF8());
        bad += !(ok && is.ok());
    }
    std::printf("%d preset(s), %d failed\n", files.size(), bad);
    return bad ? 1 : 0;
}


// ---------------------------------------------------------------- Phase 6b: preset QA
struct QaStats {
    double peak = 0, energy_max_frac = 0, gate_frac = 0, gate_frac_tail = 0, energy_end_frac = 0;
    std::vector<double> e_trace; long limiter_samples = 0, blocks = 0; int resets = 0, L = 0, threads = 0, pot_eff = 0; bool finite = true;
};

void write_wav(const juce::File& out, const juce::AudioBuffer<float>& buf, int bits, double fs) {
    out.getParentDirectory().createDirectory(); out.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> os(out.createOutputStream().release());
    if (auto* w = wav.createWriterFor(os.get(), fs, 2, bits, {}, 0)) {
        os.release();
        std::unique_ptr<juce::AudioFormatWriter> writer(w);
        writer->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
    }
}

QaStats qa_render(EngineHost& h, const PresetData& p, const juce::File& out, double play, double tail, int bits, double fs = 48000) {
    h.install_now(fs);
    const bool inst = (int)p.values[pMode] == 2;
    DemoInput in; in.fs = fs; in.play = play; in.drums.fs = fs;
    DemoPhrase ph(play);
    const int B = 128; const long total = long((play + tail) * fs), play_n = long(play * fs);
    juce::AudioBuffer<float> buf(2, (int)total);
    std::vector<float> il(B), ir(B);
    QaStats st; long gate_blocks = 0, gate_tail = 0, tail_blocks = 0;
    auto* core = h.active_core();
    const double ecap = core ? core->engine().config().E_cap : 1.0;
    for (long pos = 0; pos < total; pos += B) {
        const int n = (int)std::min<long>(B, total - pos);
        if (inst) ph.dispatch(h, double(pos + n) / fs);
        for (int i = 0; i < n; ++i) { il[(size_t)i] = inst ? 0.f : in.next(); ir[(size_t)i] = il[(size_t)i]; }
        h.process(il.data(), ir.data(), buf.getWritePointer(0, (int)pos), buf.getWritePointer(1, (int)pos), n);
        if ((core = h.active_core()) != nullptr) {
            const double e = core->engine().tracked_energy() / ecap;
            st.energy_max_frac = std::max(st.energy_max_frac, e);
            const bool g = e >= 1.0;
            gate_blocks += g; ++st.blocks;
            if (pos >= play_n) { gate_tail += g; ++tail_blocks; }
            st.energy_end_frac = e;
            if ((pos / B) % 37 == 0) st.e_trace.push_back(e);   // ~0.1 s
        }
    }
    st.gate_frac = st.blocks ? double(gate_blocks) / double(st.blocks) : 0;
    st.gate_frac_tail = tail_blocks ? double(gate_tail) / double(tail_blocks) : 0;
    for (int c = 0; c < 2; ++c) for (long i = 0; i < total; ++i) {
        const float v = buf.getSample(c, (int)i); st.finite &= std::isfinite(v);
        st.peak = std::max(st.peak, (double)std::abs(v)); st.limiter_samples += std::abs(v) > 0.9f;
    }
    st.resets = h.engine().resets();
    if (auto* c = h.active_core()) { st.L = c->L(); st.threads = c->engine().threads(); }
    st.pot_eff = effective_potential_choice((int)p.values[pPotential], st.L);
    write_wav(out, buf, bits, fs);
    return st;
}

struct CostStats { double med_uss = 0, p99_us = 0, p99_bud = 0, max_us = 0; long over = 0, blocks = 0; int threads = 0; };

// Times the preset paced like a host (block 128, one block per period; same pacing as `bench`).
CostStats qa_cost(const PresetData& p, double secs, double fs = 48000, int thread_override = 0) {
    EngineHost h; apply_preset(h, p); push_preset_state(h, p);
    if (thread_override > 0) h.set_thread_override(thread_override);
    h.install_now(fs);
    const bool inst = (int)p.values[pMode] == 2;
    const int B = 128;
    DemoInput in; in.fs = fs; in.play = 1e9; in.drums.fs = fs;
    std::unique_ptr<DemoPhrase> ph;
    double ph_t0 = 0;
    std::vector<float> il((size_t)B), ir((size_t)B), ol((size_t)B), orr((size_t)B);
    const long warm = long(0.5 * fs / B), nb = std::max<long>(20, long(secs * fs / B));
    std::vector<double> t; t.reserve((size_t)nb);
    const auto period = std::chrono::duration<double>(double(B) / fs);
    auto next = std::chrono::steady_clock::now();
    double clock = 0;
    for (long k = 0; k < warm + nb; ++k) {
        if (inst) {   // loop the 8 s demo phrase
            if (!ph || clock - ph_t0 >= 8.0) { if (ph) h.all_notes_off(); ph = std::make_unique<DemoPhrase>(7.5); ph_t0 = clock; }
            ph->dispatch(h, clock - ph_t0 + double(B) / fs);
        }
        clock += double(B) / fs;
        for (int i = 0; i < B; ++i) { il[(size_t)i] = inst ? 0.f : in.next(); ir[(size_t)i] = il[(size_t)i]; }
        if (std::fmod(clock, 8.0) < double(B) / fs) in.n = 0;   // loop the FX demo input too
        next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
        const auto now = std::chrono::steady_clock::now();
        if (next - now > std::chrono::milliseconds(2)) std::this_thread::sleep_for(next - now - std::chrono::microseconds(1500));
        while (std::chrono::steady_clock::now() < next) {}
        const auto t0 = std::chrono::steady_clock::now();
        h.process(il.data(), ir.data(), ol.data(), orr.data(), B);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        if (k >= warm) t.push_back(us);
        if (std::chrono::steady_clock::now() > next + 4 * period) next = std::chrono::steady_clock::now();
    }
    std::vector<double> s = t; std::sort(s.begin(), s.end());
    CostStats c; const double budget = 1e6 * double(B) / fs;
    c.med_uss = s[s.size() / 2] / B; c.p99_us = s[std::min(s.size() - 1, size_t(double(s.size()) * 0.99))]; c.p99_bud = c.p99_us / budget;
    c.max_us = s.back(); for (double x : t) c.over += x > budget; c.blocks = (long)t.size();
    c.threads = h.active_core() ? h.active_core()->engine().threads() : 0;
    return c;
}

int cmd_qa(const Args& a) {
    const auto files = preset_files(juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[1]));
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[2]);
    const double play = a.get("play", "8").getDoubleValue(), tail = a.get("tail", "6").getDoubleValue(), cost = a.get("cost", "0").getDoubleValue();
    const int bits = a.get("bits", "32").getIntValue();
    if (a.opt.containsKey("input-gain")) DemoInput::kDemoInputGain = (float)a.get("input-gain", "1").getDoubleValue();
    TestInput::kSeed = (unsigned)a.get("seed", "12345").getLargeIntValue();
    TestInput::kPortable = a.get("noise", "portable") != "std";   // 6b2: qa defaults to the portable noise
    // Phase 6b2: --only "Big Spiral 32,125_chiral" keeps presets whose name or file name contains one of the comma-separated
    // items (case-insensitive), e.g. to re-run only the changed presets on the i9.
    juce::StringArray only; only.addTokens(a.get("only", ""), ",", "\""); only.trim(); only.removeEmptyStrings();
    const bool no_wav = a.get("no-wav", "0").getIntValue() != 0;
    juce::Array<juce::var> rows; int fails = 0;
    for (auto& pf : files) {
        PresetData p; PresetIssues is;
        if (!preset_from_file(pf, p, is)) { std::printf("FAIL %s: %s\n", pf.getFileName().toRawUTF8(), is.errors.joinIntoString("; ").toRawUTF8()); ++fails; continue; }
        if (!only.isEmpty()) {
            bool hit = false;
            for (auto& o : only) hit |= p.name.containsIgnoreCase(o) || pf.getFileName().containsIgnoreCase(o);
            if (!hit) continue;
        }
        EngineHost h; apply_preset(h, p); push_preset_state(h, p);
        const auto out = dir.getChildFile(pf.getFileNameWithoutExtension() + ".wav");
        const auto st = qa_render(h, p, out, play, tail, bits);
        if (no_wav) out.deleteFile();
        CostStats c; if (cost > 0) c = qa_cost(p, cost);
        std::printf("%-26s L%-2d T%d pot%d peak %6.1f dBFS lim %5ld Emax %.3f gate %.3f/%.3f Eend %.4f res %d %s | cost %.3f us/smp p99/bud %.3f over %ld/%ld T%d\n",
                    p.name.toRawUTF8(), st.L, st.threads, st.pot_eff, 20 * std::log10(st.peak + 1e-12), st.limiter_samples, st.energy_max_frac, st.gate_frac,
                    st.gate_frac_tail, st.energy_end_frac, st.resets, st.finite ? "" : "NON-FINITE", c.med_uss, c.p99_bud, c.over, c.blocks, c.threads);
        std::fflush(stdout);
        auto* o = new juce::DynamicObject();
        o->setProperty("file", pf.getFileName()); o->setProperty("wav", out.getFileName()); o->setProperty("name", p.name); o->setProperty("category", p.category);
        o->setProperty("mode", (int)p.values[pMode]); o->setProperty("lattice", st.L); o->setProperty("threads", st.threads);
        o->setProperty("potential", (int)p.values[pPotential]); o->setProperty("potential_effective", st.pot_eff);
        o->setProperty("play_s", play); o->setProperty("tail_s", tail);
        o->setProperty("noise_seed", (int)TestInput::kSeed); o->setProperty("noise", TestInput::kPortable ? "portable" : "std");
        o->setProperty("finite", st.finite); o->setProperty("nan_resets", st.resets); o->setProperty("peak", st.peak);
        o->setProperty("limiter_samples", (double)st.limiter_samples); o->setProperty("energy_max_over_cap", st.energy_max_frac);
        o->setProperty("gate_frac", st.gate_frac); o->setProperty("gate_frac_tail", st.gate_frac_tail); o->setProperty("energy_end_over_cap", st.energy_end_frac);
        o->setProperty("out_gain_db", p.values[pOutGain]);
        { juce::Array<juce::var> tr; for (double e : st.e_trace) tr.add(e); o->setProperty("energy_trace", tr); }
        o->setProperty("cost_seconds", cost);
        if (cost > 0) {
            o->setProperty("cost_us_per_sample", c.med_uss); o->setProperty("cost_p99_us", c.p99_us); o->setProperty("cost_p99_over_budget", c.p99_bud);
            o->setProperty("cost_max_us", c.max_us); o->setProperty("cost_overruns", (double)c.over); o->setProperty("cost_blocks", (double)c.blocks);
            o->setProperty("cost_threads", c.threads);
        }
        rows.add(juce::var(o));
        if (!st.finite || st.resets) ++fails;
        dir.getChildFile("qa.json").replaceWithText(juce::JSON::toString(rows));
    }
    return fails ? 1 : 0;
}

// Phase 6b: load rules for every preset in a directory (and synthetic edge cases)
int cmd_check_presets(const Args& a) {
    const auto files = preset_files(juce::File::getCurrentWorkingDirectory().getChildFile(a.pos[1]));
    int bad = 0, n = 0;
    auto fail = [&](const juce::String& what) { std::printf("FAIL %s\n", what.toRawUTF8()); ++bad; };
    auto check_one = [&](const PresetData& p, const juce::String& label, bool expect_warn_hq) {
        ++n;
        // round trip (metadata included)
        PresetData q; PresetIssues iq;
        if (!preset_from_string(preset_to_string(p), q, iq)) fail(label + ": round trip parse");
        else {
            if (q.name != p.name || q.tags != p.tags || q.description_zh != p.description_zh || q.cpu_class != p.cpu_class || q.macros.count != p.macros.count)
                fail(label + ": round trip metadata / macros changed");
            for (int i = 0; i < kNumParams; ++i) if (std::abs(q.values[(size_t)i] - p.values[(size_t)i]) > 1e-4f * std::max(1.f, std::abs(p.values[(size_t)i]))) { fail(label + ": round trip value " + kParams[(size_t)i].id); break; }
        }
        // the engine: effective potential / threads
        EngineHost h; apply_preset(h, p); push_preset_state(h, p);
        const auto ds = h.desired_settings(48000.0);
        const int pot_eff = effective_potential_choice(ds.potential, ds.L);
        if (ds.L > kHqExactMaxL && pot_eff == 2) fail(label + ": HQ exact effective above 16x16");
        if ((int)p.values[pThreads] == 0 && ds.threads != threads_from_choice(0, ds.L, (int)p.values[pMode], ds.potential)) fail(label + ": Auto threads mismatch");
        if (expect_warn_hq != hq_exact_limited(ds.potential, ds.L)) fail(label + ": hq_exact_limited flag");
        // 0.5 s render through the plugin's exact host path: finite, no resets
        h.install_now(48000.0);
        if (auto* c = h.active_core()) {
            const auto& cfg = c->engine().config();
            const auto want = potential_from_choice(pot_eff);
            if (cfg.potential != want) fail(label + ": engine potential is not the effective potential");
        } else fail(label + ": no lattice");
        std::vector<float> z(128, 0.f), l(128), r(128); bool fin = true;
        DemoPhrase ph(0.5); double clock = 0;
        for (int k = 0; k < 188; ++k) { if ((int)p.values[pMode] == 2) ph.dispatch(h, clock + 128.0 / 48000.0); clock += 128.0 / 48000.0;
            for (int i = 0; i < 128; ++i) z[(size_t)i] = (int)p.values[pMode] == 2 ? 0.f : 0.1f * std::sin(0.05f * float(k * 128 + i));
            h.process(z.data(), z.data(), l.data(), r.data(), 128); for (int i = 0; i < 128; ++i) fin &= std::isfinite(l[(size_t)i]) && std::isfinite(r[(size_t)i]); }
        if (!fin || h.engine().resets()) fail(label + ": non-finite or reset in 0.5 s render");
        std::printf("ok   %-34s L%-2d pot %d->%d threads %d%s\n", label.toRawUTF8(), ds.L, ds.potential, pot_eff, ds.threads, expect_warn_hq ? "  (HQ exact -> HQ fast, warned)" : "");
    };
    for (auto& f : files) {
        PresetData p; PresetIssues is;
        if (!preset_from_file(f, p, is)) { fail(f.getFileName() + ": " + is.errors.joinIntoString("; ")); continue; }
        const bool warn = is.warnings.joinIntoString(" ").contains("HQ exact");
        check_one(p, f.getFileName(), warn);
        // the same preset forced to 32x32 + HQ exact must still load and play as HQ fast (Phase 5c rule)
        PresetData x = p; x.values[pLattice] = 2.f; x.values[pPotential] = 2.f; x.present[pLattice] = x.present[pPotential] = true;
        PresetIssues ix; PresetData y;
        if (!preset_from_string(preset_to_string(x), y, ix)) fail(f.getFileName() + " [32x32 HQ exact]: does not load");
        else if (!ix.warnings.joinIntoString(" ").contains("HQ exact")) fail(f.getFileName() + " [32x32 HQ exact]: no fallback warning");
    }
    // synthetic fallback renders: one per mode
    for (int mode = 0; mode < 3; ++mode) {
        PresetData x; x.name = "synthetic L32 HQ exact mode " + juce::String(mode);
        x.values[pMode] = (float)mode; x.values[pLattice] = 2.f; x.values[pPotential] = 2.f;
        for (int i = 0; i < kNumParams; ++i) x.present[(size_t)i] = true;
        PresetIssues ix; PresetData y;
        if (!preset_from_string(preset_to_string(x), y, ix)) { fail(x.name + ": does not load"); continue; }
        check_one(y, x.name, true);
        // 16x16 + HQ exact stays HQ exact
        PresetData z = y; z.values[pLattice] = 1.f; z.name = "synthetic L16 HQ exact mode " + juce::String(mode);
        check_one(z, z.name, false);
    }
    std::printf("%d check(s) over %d preset file(s), %d failure(s)\n", n, files.size(), bad);
    return bad ? 1 : 0;
}

// Clock estimate on the calling thread: a dependent chain of double adds (no -ffast-math, so it cannot be
// reassociated) runs at one add per FADD latency.  Assumes 2 cycles/add (Golden Cove / Raptor Cove P-cores;
// Gracemont E-cores and older cores have 3-4, so an E-core reads ~30-50 % low).  ~20 ms per call.
// Purpose: detect throttling / turbo differences between bench rows, not an absolute frequency meter.
double clock_ghz_estimate() {
    volatile double seed = 1e-9;
    double x = 1.0; const double y = seed;
    long n = 0; const auto t0 = std::chrono::steady_clock::now(); double el = 0;
    do {
        for (int k = 0; k < 100000; ++k) { x = x + y; x = x + y; x = x + y; x = x + y; }
        n += 400000; el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    } while (el < 0.02);
    volatile double sink = x; (void)sink;
    return 2.0 * double(n) / el * 1e-9;
}

int cmd_bench(const Args& a) {
    const juce::String base = a.pos[1];
    auto blocks = a.ints("blocks", "64,128,512"), sizes = a.ints("sizes", "8,16,32"), threads = a.ints("threads", "1,2,3,4,6,8");
    const double secs = a.get("seconds", "3").getDoubleValue(), fs = a.get("fs", "48000").getDoubleValue();
    int mode = a.get("mode", "0").getIntValue(), pot = a.get("potential", "0").getIntValue(), interp = a.get("interp", "0").getIntValue();
    // Phase 6b: --preset runs a preset's own parameters (L / mode / potential / interp from the preset)
    PresetData bench_preset; bool use_preset = false;
    if (a.opt.containsKey("preset")) {
        PresetIssues is;
        if (!preset_from_file(juce::File::getCurrentWorkingDirectory().getChildFile(a.get("preset", "")), bench_preset, is)) { std::printf("preset: %s\n", is.errors.joinIntoString("; ").toRawUTF8()); return 2; }
        use_preset = true;
        mode = (int)bench_preset.values[pMode]; pot = (int)bench_preset.values[pPotential]; interp = (int)bench_preset.values[pInterp];
        sizes = { lattice_size_from_choice((int)bench_preset.values[pLattice]) };
    }
    RtOptions rt;
    const juce::String pin = a.get("pin", "cpuset"), pool = a.get("pool", "caller");
    rt.pin = pin == "pcores" || pin == "1" ? 1 : (pin == "pset" || pin == "2" ? 2 : (pin == "cpuset" || pin == "3" ? 3 : 0));
    rt.mmcss = a.get("mmcss", "1").getIntValue() != 0;
    rt.caller_computes = pool == "caller" ? 1 : (pool == "dedicated" ? 0 : -1);
    rt.kernel = a.get("kernel", "stencil") == "csr" ? 0 : 1;
    rt.ersav = a.get("ersav", "0").getIntValue() != 0;
    rt.inblock_os_wait = a.get("inblock-os-wait", "0").getIntValue() != 0;
    rt.no_power_throttling = a.get("ecoqos", "0").getIntValue() == 0;
    const bool caller_cpuset = a.get("caller-cpuset", "0").getIntValue() != 0, caller_mmcss = a.get("caller-mmcss", "0").getIntValue() != 0;
    const juce::String variant = a.get("variant", "");
    // Phase 5c: the plugin runs HQ exact (potential 2) as HQ fast above 16x16; --hq-exact-any-L 1 measures the real thing
    const bool hq_exact_any_L = a.get("hq-exact-any-L", "0").getIntValue() != 0;
    if (a.opt.containsKey("barrier-spin-us")) rt.barrier_spin_ns = long(1000.0 * a.get("barrier-spin-us", "50").getDoubleValue());
    if (a.opt.containsKey("idle-spin-us")) rt.idle_spin_ns = long(1000.0 * a.get("idle-spin-us", "200").getDoubleValue());
    rt.barrier_yield_ns = std::max(rt.barrier_yield_ns, rt.barrier_spin_ns); rt.idle_yield_ns = std::max(rt.idle_yield_ns, rt.idle_spin_ns);
    rt.adaptive_spin = a.get("adaptive", "0").getIntValue() != 0;
    rt.caller_pool_spin = a.get("caller-spin", "0").getIntValue() != 0;
    const int load = a.get("load", "0").getIntValue();
    const int snap_hz = a.get("snapshot-hz", "0").getIntValue();
    const juce::String tag = a.get("tag", (juce::String(morpho_phs::rt::pin_mode_name(morpho_phs::rt::PinMode(rt.pin))) + (rt.mmcss ? "+mmcss" : "")).toRawUTF8());
    const auto& topo = machine_topology();
    // optional: restrict the bench's host thread (= compute thread 0 in caller mode) to the clean P-core CPU set and/or
    // register it with MMCSS, like a DAW audio thread would be (both off by default: a host decides this, not the plugin)
    bool caller_cpuset_ok = false;
    if (caller_cpuset) caller_cpuset_ok = morpho_phs::rt::set_thread_cpuset(morpho_phs::rt::clean_perf_cpus(topo));
    morpho_phs::rt::MmcssScope caller_mm(caller_mmcss);
    juce::Array<juce::var> rows;
    juce::String txt;
    txt << "MORPHO-PHS morpho_cli bench  host=" << juce::SystemStats::getComputerName() << "  cpu=" << juce::SystemStats::getCpuModel()
        << "  logical_cores=" << juce::SystemStats::getNumCpus() << "  fs=" << fs << "  mode=" << mode << "  potential=" << pot << "  interp=" << interp << "\n"
        << "topology: source=" << topo.source << " logical=" << (int)topo.cpus.size() << " physical=" << topo.physical_cores
        << " perf_physical=" << topo.perf_physical_cores << " hybrid=" << (int)topo.hybrid << "  auto_threads(L32): fx=" << morpho_phs::rt::default_threads(32, topo)
        << " inst=" << threads_from_choice(0, 32, 2, 0) << " hq_fast=" << threads_from_choice(0, 32, 0, 1)
        << "  this_run(L32)=" << threads_from_choice(0, 32, mode, pot) << "  hq_exact_any_L=" << (int)hq_exact_any_L
        << "  effective_potential(L32)=" << effective_potential_choice(pot, 32, hq_exact_any_L) << "\n"
        << "tag=" << tag << "  pin=" << pin << "  mmcss=" << (int)rt.mmcss << "  pool=" << pool << "  load_threads=" << load
        << "  barrier_spin_us=" << rt.barrier_spin_ns / 1000.0 << "  idle_spin_us=" << rt.idle_spin_ns / 1000.0
        << "  adaptive_spin=" << (int)rt.adaptive_spin << "  caller_spin=" << (int)rt.caller_pool_spin << "  paced=" << a.get("paced", "1") << "\n"
        << "variant=" << variant << "  kernel=" << (rt.kernel ? "stencil" : "csr") << "  ersav=" << (int)rt.ersav << "  inblock_os_wait=" << (int)rt.inblock_os_wait
        << "  ecoqos_off=" << (int)rt.no_power_throttling << "  caller_cpuset=" << (int)caller_cpuset << "/" << (int)caller_cpuset_ok
        << "  caller_mmcss=" << (int)caller_mmcss << "/" << (int)caller_mm.active() << "  snapshot_hz=" << snap_hz << "\n"
        << "time=" << juce::Time::getCurrentTime().toISO8601(true) << "  seconds/config=" << secs << "\n"
        << "tag            block  L  thr  us/smp(med)  med_us   p99_us    p999_us    max_us   budget  p99/bud  overruns     verdict  clkGHz bOSw iOSw  rt\n";
    std::printf("%s", txt.toRawUTF8());
    // Phase 5b: results are (re)written after EVERY row, so a run killed by the bench script's timeout keeps the rows
    // that finished ("complete": false until the last row).
    auto write_out = [&](bool complete) {
        std::fflush(stdout);   // live progress when stdout is redirected to a file by bench_windows.ps1
        auto* root = new juce::DynamicObject();
        root->setProperty("tool", "morpho_cli bench"); root->setProperty("host", juce::SystemStats::getComputerName());
        root->setProperty("cpu", juce::SystemStats::getCpuModel()); root->setProperty("logical_cores", juce::SystemStats::getNumCpus());
        root->setProperty("os", juce::SystemStats::getOperatingSystemName());
        root->setProperty("topology_source", juce::String(topo.source)); root->setProperty("physical_cores", topo.physical_cores);
        root->setProperty("perf_physical_cores", topo.perf_physical_cores); root->setProperty("hybrid", topo.hybrid);
        root->setProperty("time", juce::Time::getCurrentTime().toISO8601(true));
        root->setProperty("fs", fs); root->setProperty("mode", mode); root->setProperty("potential", pot); root->setProperty("interp", interp);
        root->setProperty("variant", variant);
        root->setProperty("seconds_per_config", secs); root->setProperty("paced", a.get("paced", "1").getIntValue() != 0); root->setProperty("rows", rows); root->setProperty("complete", complete);
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(base + ".json")).replaceWithText(juce::JSON::toString(juce::var(root)));
        juce::File(juce::File::getCurrentWorkingDirectory().getChildFile(base + ".txt")).replaceWithText(txt);
    };
    std::atomic<bool> stop_load{false};
    std::vector<std::thread> hogs;
    for (int k = 0; k < load; ++k) hogs.emplace_back([&] { volatile unsigned long x = 0; while (!stop_load.load(std::memory_order_relaxed)) x = x + 1; });
    for (int B : blocks) for (int L : sizes) for (int T0 : threads) {
        const int T = T0 > 0 ? std::clamp(T0, 1, 64)
                             : morpho_phs::rt::default_threads(L, topo, auto_load(mode, pot, L, hq_exact_any_L));
        EngineHost h;
        if (use_preset) { apply_preset(h, bench_preset); push_preset_state(h, bench_preset); }
        else {
            h.set_param(pMode, (float)mode); h.set_param(pPotential, (float)pot); h.set_param(pInterp, (float)interp);
            h.set_param(pLattice, L <= 8 ? 0.f : (L <= 16 ? 1.f : 2.f));
            if (mode == 2) h.set_param(pLife, 0.3f);
        }
        h.set_thread_override(T); h.set_rt_options(rt); h.set_allow_hq_exact_any_L(hq_exact_any_L);
        h.install_now(fs);
        // Phase 6a: editor-open simulation (subscription + a UI-like reader thread)
        h.engine().snapshot_hz.store(snap_hz); h.engine().time_publish = snap_hz > 0;
        std::atomic<bool> stop_reader{false};
        std::atomic<long> frames_seen{0};
        std::thread reader;
        if (snap_hz > 0) reader = std::thread([&] {
            double acc = 0; std::uint32_t last = 0;
            while (!stop_reader.load(std::memory_order_relaxed)) {
                auto& tb = h.engine().snapshots;
                if (tb.update()) {
                    const Snapshot& s = tb.read_buffer();
                    if (s.frame != last) { last = s.frame; frames_seen.fetch_add(1, std::memory_order_relaxed); }
                    for (int i = 0; i < s.N; ++i) acc += s.energy[(size_t)i] + s.qx[(size_t)i] + s.medium[(size_t)i];
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(std::max(1, 1000 / std::min(snap_hz, kMaxSnapshotHz))));
            }
            volatile double sink = acc; (void)sink;
        });
        const juce::String rtrep = h.active_core() ? juce::String(h.active_core()->engine().pin_report()) : juce::String("?");
        const juce::String row_start = juce::Time::getCurrentTime().toISO8601(true);
        const double clk = clock_ghz_estimate();
        TestInput in; in.kind = mode == 2 ? TestInput::Silence : TestInput::Drums; in.fs = fs;
        Phrase ph(secs + 2.0);   // covers warm-up + measured blocks
        std::vector<float> il((size_t)B), ir((size_t)B), ol((size_t)B), orr((size_t)B);
        const long warm = long(0.5 * fs / B), nb = std::max<long>(20, long(secs * fs / B));
        std::vector<double> t; t.reserve((size_t)nb);
        double clock = 0;
        // real-time pacing: one block per block period (like a host), so idle-wait / wake-up behaviour is realistic
        const auto period = std::chrono::duration<double>(double(B) / fs);
        auto next = std::chrono::steady_clock::now();
        const bool paced = a.get("paced", "1").getIntValue() != 0;
        long bosw0 = 0, iosw0 = 0;
        for (long k = 0; k < warm + nb; ++k) {
            if (k == warm && h.active_core()) { bosw0 = (long)h.active_core()->engine().barrier_blocked_waits(); iosw0 = (long)h.active_core()->engine().idle_os_waits(); }
            if (mode == 2) ph.dispatch(h, clock + double(B) / fs);
            clock += double(B) / fs;
            for (int i = 0; i < B; ++i) { il[(size_t)i] = in.next(); ir[(size_t)i] = il[(size_t)i]; }
            if (paced) {   // sleep most of the gap (like a host waiting for the device), spin the last ~1.5 ms
                next += std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
                const auto now = std::chrono::steady_clock::now();
                if (next - now > std::chrono::milliseconds(2)) std::this_thread::sleep_for(next - now - std::chrono::microseconds(1500));
                while (std::chrono::steady_clock::now() < next) {}
            }
            const auto t0 = std::chrono::steady_clock::now();
            h.process(il.data(), ir.data(), ol.data(), orr.data(), B);
            const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
            if (k >= warm) t.push_back(us);
            if (paced && std::chrono::steady_clock::now() > next + 4 * period) next = std::chrono::steady_clock::now();   // resync after a long overrun
        }
        stop_reader = true; if (reader.joinable()) reader.join();
        const auto& E = h.engine();
        const double pub_us = E.publish_count ? 1e-3 * double(E.publish_ns_sum) / double(E.publish_count) : 0.0, pub_max_us = 1e-3 * double(E.publish_ns_max);
        const double pubs_per_block = snap_hz > 0 ? std::max(1.0, fs / double(std::min(snap_hz, kMaxSnapshotHz)) / double(B)) : 0.0;   // >= 1 block between publishes
        const double pub_pct = pubs_per_block > 0 ? 100.0 * pub_us / pubs_per_block / (1e6 * double(B) / fs) : 0.0;
        std::vector<double> s = t; std::sort(s.begin(), s.end());
        auto pct = [&](double q) { return s[std::min(s.size() - 1, size_t(double(s.size()) * q))]; };
        const double med = s[s.size() / 2], p99 = pct(0.99), p999 = pct(0.999), mx = s.back();
        const double budget = 1e6 * double(B) / fs; long over = 0; for (double x : t) over += x > budget;
        const double uss = med / B;
        const bool crit = (L == 32 && B == 128);
        const char* verdict = (over == 0 && p99 / budget < 0.7) ? "PASS" : ((double(over) <= 0.001 * double(t.size()) && p99 / budget < 0.7) ? "NEAR" : "FAIL");
        // OS-wait counters over the MEASURED blocks only (warm-up / first wake-ups excluded); totals in the JSON
        const long bosw_tot = h.active_core() ? (long)h.active_core()->engine().barrier_blocked_waits() : -1;
        const long iosw_tot = h.active_core() ? (long)h.active_core()->engine().idle_os_waits() : -1;
        const long bosw = bosw_tot < 0 ? -1 : bosw_tot - bosw0, iosw = iosw_tot < 0 ? -1 : iosw_tot - iosw0;
        char line[768];
        std::snprintf(line, sizeof line, "%-13s %5d %3d %4d   %9.3f %8.1f %8.1f %10.1f %9.1f %8.1f %8.3f %5ld/%-6zu %6s%s %6.2f %4ld %5ld  %s\n", tag.toRawUTF8(), B, L, T, uss, med, p99, p999, mx, budget,
                      p99 / budget, over, t.size(), verdict, crit ? "*" : " ", clk, bosw, iosw, rtrep.toRawUTF8());
        if (snap_hz > 0) {   // Phase 6a: append the publish cost to the row's line (old parsers read the fixed columns only)
            line[std::strlen(line) - 1] = 0;
            char extra[200];
            std::snprintf(extra, sizeof extra, "  snap_hz=%d pubs=%llu pub_us=%.2f pub_max_us=%.2f pub_pct_budget=%.4f frames_read=%ld\n", snap_hz,
                          (unsigned long long)E.publish_count, pub_us, pub_max_us, pub_pct, frames_seen.load());
            std::strncat(line, extra, sizeof line - std::strlen(line) - 1);
        }
        std::printf("%s", line); std::fflush(stdout); txt << line;
        auto* o = new juce::DynamicObject();
        o->setProperty("tag", tag); o->setProperty("pin", rt.pin); o->setProperty("mmcss", rt.mmcss); o->setProperty("caller_computes", rt.caller_computes);
        o->setProperty("load_threads", load); o->setProperty("rt_report", rtrep); o->setProperty("adaptive_spin", rt.adaptive_spin);
        o->setProperty("block", B); o->setProperty("L", L); o->setProperty("threads", T); o->setProperty("us_per_sample_median", uss);
        o->setProperty("block_median_us", med); o->setProperty("load_pct_median", 100.0 * med / budget); o->setProperty("block_p99_us", p99);
        o->setProperty("block_p999_us", p999); o->setProperty("block_max_us", mx);
        o->setProperty("budget_us", budget); o->setProperty("p99_over_budget", p99 / budget); o->setProperty("overruns", (int)over);
        o->setProperty("blocks_measured", (int)t.size()); o->setProperty("nan_resets", h.engine().resets()); o->setProperty("verdict", verdict);
        o->setProperty("barrier_os_waits", (double)bosw); o->setProperty("idle_os_waits", (double)iosw);
        o->setProperty("barrier_os_waits_incl_warmup", (double)bosw_tot); o->setProperty("idle_os_waits_incl_warmup", (double)iosw_tot);
        o->setProperty("variant", variant); o->setProperty("row_start", row_start); o->setProperty("clk_ghz_est", clk);
        o->setProperty("kernel", rt.kernel ? "stencil" : "csr"); o->setProperty("ersav", rt.ersav); o->setProperty("inblock_os_wait", rt.inblock_os_wait);
        o->setProperty("ecoqos_off", rt.no_power_throttling); o->setProperty("caller_cpuset", caller_cpuset_ok); o->setProperty("caller_mmcss", caller_mm.active());
        o->setProperty("snapshot_hz", snap_hz); o->setProperty("publish_count", (double)E.publish_count);
        o->setProperty("publish_mean_us", pub_us); o->setProperty("publish_max_us", pub_max_us); o->setProperty("publish_pct_budget", pub_pct);
        o->setProperty("frames_read", (double)frames_seen.load());
        rows.add(juce::var(o));
        write_out(false);
    }
    stop_load = true; for (auto& x : hogs) x.join();
    txt << "verdict: PASS = 0 overruns and p99/budget < 0.7; NEAR = <= 0.1 % overruns and p99/budget < 0.7; '*' = Phase-4 criterion row (32x32, block 128)\n";
    std::printf("verdict: PASS = 0 overruns and p99/budget < 0.7; NEAR = <= 0.1 %% overruns and p99/budget < 0.7; '*' = criterion row (32x32, block 128)\n");
    write_out(true);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    morpho_phs::enable_ftz_daz();
#if defined(_WIN32)
    timeBeginPeriod(1);   // 1 ms sleep granularity for the paced benchmark
#endif   // same FP mode as the plugin's audio thread (ScopedNoDenormals) and workers
    Args a(argc, argv);
    if (a.pos.isEmpty()) {
        std::printf("usage: morpho_cli render-modes|render-presets|render|bench|validate|qa|check-presets ... (see source header)\n");
        return 2;
    }
    const auto cmd = a.pos[0];
    if (cmd == "render-modes" && a.pos.size() >= 2) return cmd_render_modes(a);
    if (cmd == "render-presets" && a.pos.size() >= 3) return cmd_render_presets(a);
    if (cmd == "render" && a.pos.size() >= 3) return cmd_render(a);
    if (cmd == "bench" && a.pos.size() >= 2) return cmd_bench(a);
    if (cmd == "validate" && a.pos.size() >= 2) return cmd_validate(a);
    if (cmd == "qa" && a.pos.size() >= 3) return cmd_qa(a);
    if (cmd == "check-presets" && a.pos.size() >= 2) return cmd_check_presets(a);
    std::printf("bad arguments\n");
    return 2;
}
