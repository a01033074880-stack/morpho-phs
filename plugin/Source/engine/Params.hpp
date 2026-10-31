// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS plugin — parameter table shared by the JUCE processor, the .morpho preset loader and the CLI.
// The order of this table is the stable parameter index; ids are the stable automation / preset keys.
#pragma once
#include <array>
#include <cstring>
#include <string_view>

namespace morpho_plugin {

enum class PKind { Float, Choice };

struct ParamDef {
    const char* id; const char* name; PKind kind;
    float min, max, def;
    bool log;              // log-scaled float (min > 0)
    const char* unit;
    const char* choices;   // '|'-separated for Choice
};

enum P : int {
    pMode, pLattice, pThreads, pPotential, pInterp,
    pTone, pChirality, pNonlinear, pHQ, pDecay, pLife, pMorph, pTau, pFreeze,
    pWet, pDry, pInGain, pOutGain, pSpread, pExciter, pDefectDepth,
    pMacro1, pMacro2, pMacro3, pMacro4, pMacro5, pMacro6, pMacro7, pMacro8,
    pEdgeWidth, pEdgeCrosstalk,   // Phase 4 (D4): appended so existing indices / automation stay stable
    kNumParams
};

inline constexpr std::array<ParamDef, kNumParams> kParams = {{
    {"mode", "Mode", PKind::Choice, 0, 2, 0, false, "", "FX Edge Disperser|FX Defect Freezer|Instrument"},
    {"lattice", "Lattice", PKind::Choice, 0, 2, 1, false, "", "8x8|16x16|32x32"},
    {"threads", "Threads", PKind::Choice, 0, 6, 0, false, "", "Auto|1|2|3|4|6|8"},
    {"potential", "Potential", PKind::Choice, 0, 2, 0, false, "", "Quartic|HQ (fast log-cosh)|HQ exact"},
    {"interp", "g/mu Interp", PKind::Choice, 0, 2, 0, false, "", "1 sample|8 samples|Off (64)"},
    {"tone", "Tone", PKind::Float, 150.f, 3000.f, 1500.f, true, "Hz", nullptr},
    {"chirality", "Chirality", PKind::Float, -1.f, 1.f, 0.6f, false, "", nullptr},
    {"nonlinear", "Nonlinear", PKind::Float, 0.f, 1.f, 0.25f, false, "", nullptr},
    {"hq", "HQ Amount", PKind::Float, 0.f, 1.f, 0.3f, false, "", nullptr},
    {"decay", "Decay", PKind::Float, 0.f, 1.f, 0.6f, false, "", nullptr},
    {"life", "Life", PKind::Float, 0.f, 1.f, 0.f, false, "", nullptr},
    {"morph", "Morphogen", PKind::Float, 0.f, 1.f, 0.3f, false, "", nullptr},
    {"tau", "Tau", PKind::Float, 0.01f, 2.f, 0.0833f, true, "s", nullptr},
    {"freeze", "Freeze", PKind::Float, 0.f, 1.f, 0.f, false, "", nullptr},
    {"wet", "Lattice Level", PKind::Float, 0.f, 1.f, 0.7f, false, "", nullptr},
    {"dry", "Dry / Full-band", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"in_gain", "Input Gain", PKind::Float, -24.f, 24.f, 0.f, false, "dB", nullptr},
    {"out_gain", "Output Gain", PKind::Float, -24.f, 24.f, 0.f, false, "dB", nullptr},
    {"spread", "Pickup Spread", PKind::Float, 0.05f, 0.45f, 0.25f, false, "", nullptr},
    {"exciter", "Exciter", PKind::Choice, 0, 1, 0, false, "", "Bow (saw)|Pluck"},
    {"defect_depth", "Defect Depth", PKind::Float, 0.f, 1.f, 0.6f, false, "", nullptr},
    {"macro1", "Macro 1", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro2", "Macro 2", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro3", "Macro 3", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro4", "Macro 4", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro5", "Macro 5", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro6", "Macro 6", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro7", "Macro 7", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"macro8", "Macro 8", PKind::Float, 0.f, 1.f, 0.5f, false, "", nullptr},
    {"edge_width", "Edge Width", PKind::Float, 0.f, 2.f, 1.f, false, "", nullptr},
    {"edge_crosstalk", "Edge Crosstalk", PKind::Float, 0.f, 1.f, 0.f, false, "", nullptr},
}};

inline int param_index(std::string_view id) noexcept {
    for (int i = 0; i < kNumParams; ++i) if (id == kParams[i].id) return i;
    return -1;
}
inline int num_choices(int p) noexcept {
    int n = 1; for (const char* c = kParams[p].choices; c && *c; ++c) n += (*c == '|'); return n;
}
inline bool is_macro_param(int p) noexcept { return p >= pMacro1 && p <= pMacro8; }
inline bool is_engine_param(int p) noexcept { return p >= 0 && p < kNumParams && !is_macro_param(p); }   // macros may target any non-macro parameter

// Macro mapping: macro k drives parameter `param` over [lo, hi] (plain units; log curve for log params).
// Semantics (documented in docs/morpho_preset_spec.md): a mapped parameter is OVERRIDDEN by its macro.
// D3 (Phase 4, decided): this includes Tone -- a macro mapped to "tone" replaces the Tone knob value while mapped.
struct MacroTarget { int macro = -1; int param = -1; float lo = 0, hi = 1; };
struct MacroMap {
    static constexpr int kMax = 32;
    int count = 0;
    std::array<MacroTarget, kMax> t{};
    std::array<std::array<char, 24>, 8> names{};
};

inline MacroMap default_macro_map() noexcept {
    MacroMap m;
    const struct { int p; float lo, hi; const char* nm; } d[8] = {
        {pTone, 300.f, 3000.f, "Tone"}, {pChirality, -1.f, 1.f, "Chirality"}, {pNonlinear, 0.f, 1.f, "Nonlinear"},
        {pDecay, 0.f, 1.f, "Decay"}, {pMorph, 0.f, 1.f, "Morphogen"}, {pTau, 0.02f, 1.f, "Tau"},
        {pLife, 0.f, 1.f, "Life"}, {pWet, 0.f, 1.f, "Lattice Lvl"}};
    for (int k = 0; k < 8; ++k) {
        m.t[k] = {k, d[k].p, d[k].lo, d[k].hi};
        std::strncpy(m.names[k].data(), d[k].nm, 23);
    }
    m.count = 0;   // default: macros are unassigned (names only) -> knobs act directly; presets assign targets
    return m;
}

}  // namespace morpho_plugin
