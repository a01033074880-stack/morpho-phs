// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS — .morpho preset format (JSON; spec: docs/morpho_preset_spec.md).  juce_core only, so the plugin and the
// CLI share it.  Parsing never touches the audio thread.
#pragma once
#include <juce_core/juce_core.h>
#include "engine/Params.hpp"
#include <array>
#include <vector>

namespace morpho_plugin {

struct PresetPoint { float x = 0.5f, y = 0.5f, amount = 1.f; };

struct PresetData {
    juce::String name, category, author, description;
    // Phase 6b (optional metadata, still format version 2; older readers ignore these keys):
    juce::String description_zh;          // Chinese description
    juce::StringArray tags;               // lower-case browser tags
    juce::String cpu_class;               // "eco" | "standard" | "high" (measured, docs/phase6/presets.md)
    int order = 1 << 20;                  // factory sort key (file name prefix "NNN_"), not serialised
    std::array<float, kNumParams> values{};
    std::array<bool, kNumParams> present{};
    MacroMap macros = default_macro_map();
    std::vector<PresetPoint> defects;     // normalised (x right, y up)
    std::vector<PresetPoint> morphogen;   // initial Barkley stimuli
    bool has_spiral = false; PresetPoint spiral;
    int version = 2;                      // file format version read (1 = Phase 3, 2 = Phase 4: edge_width / edge_crosstalk)

    PresetData() { for (int p = 0; p < kNumParams; ++p) values[(size_t)p] = kParams[(size_t)p].def; }
    float value_or_default(int p) const { return values[(size_t)p]; }
};

struct PresetIssues { juce::StringArray errors, warnings; bool ok() const { return errors.isEmpty(); } };

PresetData preset_from_json(const juce::var& root, PresetIssues& issues);
bool preset_from_file(const juce::File& f, PresetData& out, PresetIssues& issues);
bool preset_from_string(const juce::String& text, PresetData& out, PresetIssues& issues);
juce::var preset_to_json(const PresetData& p);
juce::String preset_to_string(const PresetData& p);

// choice parameters accept an index or one of the '|'-separated labels (case-insensitive)
juce::StringArray choice_labels(int param);

// Phase 6b: factory-bank rules on top of the format rules (all 8 macros named + assigned, metadata present, measured
// CPU class, 32x32 only with Quartic / HQ fast and Auto threads).  Appends to issues.errors.
void factory_checks(const PresetData& p, PresetIssues& issues);
// leading "NNN_" of a file name -> order (else 1 << 20)
int order_from_filename(const juce::String& fname);

}  // namespace morpho_plugin
