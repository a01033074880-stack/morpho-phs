// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "engine/EngineHost.hpp"
#include "PresetIO.h"

#ifndef MORPHO_VARIANT_FX
#define MORPHO_VARIANT_FX 0
#endif

namespace morpho_plugin {

// Phase 4 (D2): one source tree, two plugins.  MORPHO_VARIANT_FX = 1 -> "MORPHO-PHS FX" (effect; modes FX Edge
// Disperser / FX Defect Freezer); 0 -> "MORPHO-PHS" (instrument / synth; mode fixed to Instrument).
inline constexpr bool kIsFxVariant = MORPHO_VARIANT_FX != 0;

class MorphoProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    MorphoProcessor();
    ~MorphoProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return JucePlugin_WantsMidiInput != 0; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return juce::jmax(1, factory_.size()); }
    int getCurrentProgram() override { return juce::jmax(0, current_program_); }   // -1 (Init / file) reported as 0 to hosts
    int current_program_index() const noexcept { return current_program_; }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // ---- used by the editor (message thread) ----
    juce::AudioProcessorValueTreeState apvts;
    juce::MidiKeyboardState keyboard;
    EngineHost& host() noexcept { return host_; }
    void send_event(const UiEvent& e);                 // also mirrors defects for state save / lattice rebuilds
    const juce::Array<PresetData>& factory() const { return factory_; }
    void load_preset(const PresetData& p);
    PresetData capture_preset(const juce::String& name) const;
    const MacroMap& macro_map() const { return macro_map_; }
    juce::String current_preset_name() const { return current_name_; }
    juce::String last_preset_error() const { return last_error_; }
    // Phase 6b browser: Init (all defaults, no macro targets, no defects) and per-variant favourites (by preset name),
    // stored in <user app data>/MORPHO-PHS/favourites_{inst|fx}.txt.  Message thread only.
    void load_init();
    bool is_favourite(const juce::String& name) const { return favourites_.contains(name); }
    void toggle_favourite(const juce::String& name);
    int find_factory(const juce::String& name) const;
    // Phase 6a: editor preferences saved with the session (not parameters): editor width (fixed aspect) + eco visuals
    int ui_width = 0;
    bool eco_visuals = false;

    static juce::AudioProcessorValueTreeState::ParameterLayout create_layout();
    // current plain value of parameter i with the variant's mode rule applied (instrument: always Instrument;
    // FX: Edge Disperser / Defect Freezer only)
    float param_now(int i) const noexcept;
    static float variant_mode(float preset_mode) noexcept { return kIsFxVariant ? (preset_mode >= 1.5f ? 0.f : preset_mode) : 2.f; }

private:
    void timerCallback() override;
    void push_lattice_state();

    EngineHost host_;
    std::array<std::atomic<float>*, kNumParams> raw_{};
    juce::Array<PresetData> factory_;
    int current_program_ = 0;
    MacroMap macro_map_ = default_macro_map();
    std::vector<PresetPoint> defects_;       // message-thread mirror (normalised coords)
    PresetData pending_state_;  bool pending_state_valid_ = false;
    juce::String current_name_ = "Init", last_error_;
    juce::StringArray favourites_;
    juce::File favourites_file() const;
    double fs_ = 48000.0;
    int seen_swaps_ = 0;
    bool prepared_ = false;
    juce::AudioBuffer<float> scratch_;
    juce::MidiBuffer kb_midi_;
};

}  // namespace morpho_plugin
