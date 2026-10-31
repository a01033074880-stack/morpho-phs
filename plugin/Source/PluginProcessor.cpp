// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace morpho_plugin {

juce::AudioProcessorValueTreeState::ParameterLayout MorphoProcessor::create_layout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i = 0; i < kNumParams; ++i) {
        const auto& d = kParams[(size_t)i];
        const juce::ParameterID pid { d.id, 1 };
        if (i == pMode) {   // D2: the instrument has no mode parameter; the FX lists only the two FX modes (same indices)
            if (kIsFxVariant) layout.add(std::make_unique<juce::AudioParameterChoice>(pid, d.name, juce::StringArray { "FX Edge Disperser", "FX Defect Freezer" }, 0));
            continue;
        }
        if (d.kind == PKind::Choice) {
            layout.add(std::make_unique<juce::AudioParameterChoice>(pid, d.name, choice_labels(i), (int)d.def));
        } else {
            juce::NormalisableRange<float> r;
            if (d.log) {
                const float lo = d.min, hi = d.max;
                r = juce::NormalisableRange<float>(lo, hi,
                    [](float s, float e, float t) { return s * std::pow(e / s, t); },
                    [](float s, float e, float v) { return std::log(v / s) / std::log(e / s); });
            } else r = juce::NormalisableRange<float>(d.min, d.max);
            layout.add(std::make_unique<juce::AudioParameterFloat>(pid, d.name, r, d.def,
                juce::AudioParameterFloatAttributes().withLabel(d.unit)));
        }
    }
    return layout;
}

MorphoProcessor::MorphoProcessor()
    : juce::AudioProcessor(kIsFxVariant ? BusesProperties()
                                              .withInput("Input", juce::AudioChannelSet::stereo(), true)
                                              .withOutput("Output", juce::AudioChannelSet::stereo(), true)
                                        : BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MORPHO", create_layout()) {
    for (int i = 0; i < kNumParams; ++i) raw_[(size_t)i] = apvts.getRawParameterValue(kParams[(size_t)i].id);   // instrument: raw_[pMode] == nullptr
    // Phase 5 real-time threading (defaults = RtOptions{}): host thread computes + 2 workers on the clean P-core
    // CPU set, pure-spin in-block barrier, EcoQoS off, MMCSS "Pro Audio" (Windows).  Environment overrides for testing
    // in a DAW: MORPHO_PHS_PIN = none|pcores|pset|cpuset, MORPHO_PHS_MMCSS = 0|1, MORPHO_PHS_POOL = auto|caller|dedicated,
    // MORPHO_PHS_KERNEL = stencil|csr, MORPHO_PHS_ERSAV = 0|1, MORPHO_PHS_INBLOCK_WAIT = 0|1, MORPHO_PHS_ECOQOS = 0|1.
    {
        RtOptions o;
        const auto pin = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_PIN", "cpuset");
        o.pin = pin == "none" ? 0 : (pin == "pcores" ? 1 : (pin == "pset" ? 2 : 3));
        o.mmcss = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_MMCSS", "1") != "0";
        const auto pool = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_POOL", "caller");
        o.caller_computes = pool == "dedicated" ? 0 : (pool == "auto" ? -1 : 1);
        o.kernel = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_KERNEL", "stencil") == "csr" ? 0 : 1;
        o.ersav = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_ERSAV", "0") == "1";
        o.inblock_os_wait = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_INBLOCK_WAIT", "0") == "1";
        o.no_power_throttling = juce::SystemStats::getEnvironmentVariable("MORPHO_PHS_ECOQOS", "0") != "1";
        host_.set_rt_options(o);
    }
    host_.set_param(pMode, variant_mode(kIsFxVariant ? 0.f : 2.f));
    // factory presets embedded as BinaryData (*.morpho), in bank order: the file-name prefix "NNN_" (instrument 001-099,
    // FX 101-199; scripts/presets/bank6b.py), then name
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) {
        const juce::String fname = BinaryData::originalFilenames[i];
        if (!fname.endsWithIgnoreCase(".morpho")) continue;
        int sz = 0;
        if (const char* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], sz)) {
            PresetData p; PresetIssues is;
            // D2: each plugin lists only its own presets (instrument: mode == Instrument; FX: the two FX modes)
            if (preset_from_string(juce::String::fromUTF8(data, sz), p, is) && ((p.values[(size_t)pMode] >= 1.5f) != kIsFxVariant)) {
                p.order = order_from_filename(fname);
                factory_.add(p);
            }
        }
    }
    std::sort(factory_.begin(), factory_.end(), [](const PresetData& a, const PresetData& b) {
        return a.order != b.order ? a.order < b.order : a.name < b.name; });
    {   // favourites (missing file = none)
        juce::StringArray lines; lines.addLines(favourites_file().loadFileAsString());
        for (auto& l : lines) if (l.trim().isNotEmpty()) favourites_.addIfNotAlreadyThere(l.trim());
    }
    startTimerHz(15);
}

MorphoProcessor::~MorphoProcessor() { stopTimer(); }

float MorphoProcessor::param_now(int i) const noexcept {
    if (i == pMode) return variant_mode(raw_[(size_t)pMode] ? raw_[(size_t)pMode]->load(std::memory_order_relaxed) : 2.f);
    return raw_[(size_t)i]->load(std::memory_order_relaxed);
}

bool MorphoProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    const auto in = layouts.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::stereo() || in == juce::AudioChannelSet::mono();
}

void MorphoProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    fs_ = sampleRate;
    for (int i = 0; i < kNumParams; ++i) host_.set_param(i, param_now(i));
    host_.set_macro_map(macro_map_);
    host_.install_now(sampleRate);         // audio is stopped during prepareToPlay
    host_.collect_garbage();
    seen_swaps_ = host_.swaps();
    scratch_.setSize(2, juce::jmax(1, samplesPerBlock));
    keyboard.reset();
    prepared_ = true;
    push_lattice_state();
}

void MorphoProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;   // matches the FTZ/DAZ mode of the engine's worker threads
    const int n = buffer.getNumSamples();
    for (int i = 0; i < kNumParams; ++i) host_.set_param(i, param_now(i));
    keyboard.processNextMidiBuffer(midi, 0, n, true);
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        const int ch = m.getChannel();
        if (m.isNoteOn()) host_.note_on(ch, m.getNoteNumber(), m.getFloatVelocity());
        else if (m.isNoteOff()) host_.note_off(ch, m.getNoteNumber());
        else if (m.isPitchWheel()) host_.pitch_bend(ch, m.getPitchWheelValue());
        else if (m.isChannelPressure()) host_.channel_pressure(ch, (float)m.getChannelPressureValue() / 127.f);
        else if (m.isAftertouch()) host_.channel_pressure(ch, (float)m.getAfterTouchValue() / 127.f);
        else if (m.isController() && m.getControllerNumber() == 74) host_.timbre(ch, (float)m.getControllerValue() / 127.f);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) host_.all_notes_off();
    }
    const int nin = getTotalNumInputChannels();
    const float* inL = nin > 0 ? buffer.getReadPointer(0) : nullptr;
    const float* inR = nin > 1 ? buffer.getReadPointer(1) : inL;
    // engine reads input sample n before writing output n, but L/R outputs would clobber R input -> copy input
    if (scratch_.getNumSamples() < n) { buffer.clear(); return; }   // host violated maximum block size: fail silent
    if (inL) scratch_.copyFrom(0, 0, inL, n); else scratch_.clear(0, 0, n);
    if (inR) scratch_.copyFrom(1, 0, inR, n); else scratch_.copyFrom(1, 0, scratch_, 0, 0, n);
    float* outL = buffer.getWritePointer(0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    if (outR) host_.process(scratch_.getReadPointer(0), scratch_.getReadPointer(1), outL, outR, n);
    else {   // mono out: render stereo into scratch then mix
        host_.process(scratch_.getReadPointer(0), scratch_.getReadPointer(1), scratch_.getWritePointer(0), scratch_.getWritePointer(1), n);
        for (int i = 0; i < n; ++i) outL[i] = 0.5f * (scratch_.getSample(0, i) + scratch_.getSample(1, i));
    }
    for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear(c, 0, n);
}

void MorphoProcessor::timerCallback() {
    if (!prepared_) return;
    host_.request_build(fs_);      // lattice size / thread count changed -> build off the audio thread
    host_.collect_garbage();
    if (host_.swaps() != seen_swaps_) { seen_swaps_ = host_.swaps(); push_lattice_state(); }
    if (pending_state_valid_) { pending_state_valid_ = false; load_preset(pending_state_); }
}

void MorphoProcessor::push_lattice_state() {
    host_.engine().ui_events.push({UiEvent::ClearDefects, 0, 0, 0});
    for (auto& d : defects_) host_.engine().ui_events.push({UiEvent::ChiselAdd, d.x, d.y, 1.f});
}

void MorphoProcessor::send_event(const UiEvent& e) {
    if (e.type == UiEvent::ChiselAdd) defects_.push_back({e.x, e.y, 1.f});
    else if (e.type == UiEvent::ChiselRemove) {
        // remove mirrored points close to the erase position (same tolerance as one honeycomb cell at 16x16)
        defects_.erase(std::remove_if(defects_.begin(), defects_.end(), [&](const PresetPoint& p) {
            return std::hypot(p.x - e.x, p.y - e.y) < 0.06f; }), defects_.end());
    } else if (e.type == UiEvent::ClearDefects) defects_.clear();
    host_.engine().ui_events.push(e);
}

void MorphoProcessor::load_preset(const PresetData& p) {
    for (int i = 0; i < kNumParams; ++i) {
        if (auto* prm = apvts.getParameter(kParams[(size_t)i].id)) {
            const bool is_macro = is_macro_param(i);
            float v = p.present[(size_t)i] || !is_macro ? p.values[(size_t)i] : kParams[(size_t)i].def;
            if (i == pMode) v = variant_mode(v);   // FX: an Instrument preset loads as FX Edge Disperser
            prm->setValueNotifyingHost(prm->convertTo0to1(v));
        }
    }
    macro_map_ = p.macros;
    host_.set_macro_map(macro_map_);
    defects_ = p.defects;
    push_lattice_state();
    host_.engine().ui_events.push({UiEvent::ResetField, 0, 0, 0});
    for (auto& m : p.morphogen) host_.engine().ui_events.push({UiEvent::Morphogen, m.x, m.y, m.amount});
    if (p.has_spiral) host_.engine().ui_events.push({UiEvent::SeedSpiral, p.spiral.x, p.spiral.y, 1.f});
    current_name_ = p.name;
    current_program_ = find_factory(p.name);
}

PresetData MorphoProcessor::capture_preset(const juce::String& name) const {
    PresetData p; p.name = name; p.category = "User"; p.author = "user";
    // a state saved from a factory preset keeps its browser metadata (category, tags, descriptions)
    if (const int fi = find_factory(current_name_); fi >= 0) {
        const auto& f = factory_.getReference(fi);
        p.category = f.category; p.tags = f.tags; p.description = f.description; p.description_zh = f.description_zh;
        if (name == f.name) p.author = f.author;
    }
    for (int i = 0; i < kNumParams; ++i) { p.values[(size_t)i] = param_now(i); p.present[(size_t)i] = true; }
    p.macros = macro_map_; p.defects = defects_;
    return p;
}

int MorphoProcessor::find_factory(const juce::String& name) const {
    for (int i = 0; i < factory_.size(); ++i) if (factory_.getReference(i).name == name) return i;
    return -1;
}

void MorphoProcessor::load_init() {
    PresetData init; init.name = "Init"; init.category = "Init";
    for (int i = 0; i < kNumParams; ++i) init.present[(size_t)i] = true;   // every parameter at its default
    init.macros = default_macro_map();
    load_preset(init);
    current_program_ = -1;
}

juce::File MorphoProcessor::favourites_file() const {
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("MORPHO-PHS")
        .getChildFile(kIsFxVariant ? "favourites_fx.txt" : "favourites_inst.txt");
}

void MorphoProcessor::toggle_favourite(const juce::String& name) {
    if (name.isEmpty()) return;
    if (favourites_.contains(name)) favourites_.removeString(name); else favourites_.add(name);
    const auto f = favourites_file();
    if (f.getParentDirectory().createDirectory()) f.replaceWithText(favourites_.isEmpty() ? juce::String() : favourites_.joinIntoString("\n") + "\n");
}

void MorphoProcessor::setCurrentProgram(int index) {
    if (!juce::isPositiveAndBelow(index, factory_.size())) return;
    current_program_ = index;
    load_preset(factory_.getReference(index));
}

const juce::String MorphoProcessor::getProgramName(int index) {
    return juce::isPositiveAndBelow(index, factory_.size()) ? factory_.getReference(index).category + " / " + factory_.getReference(index).name : juce::String("Init");
}

void MorphoProcessor::getStateInformation(juce::MemoryBlock& destData) {
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    auto* extra = xml->createNewChildElement("MorphoExtra");
    extra->setAttribute("preset", preset_to_string(capture_preset(current_name_)));
    extra->setAttribute("program", current_program_);
    if (ui_width > 0) extra->setAttribute("ui_width", ui_width);
    extra->setAttribute("eco", eco_visuals ? 1 : 0);
    copyXmlToBinary(*xml, destData);
}

void MorphoProcessor::setStateInformation(const void* data, int sizeInBytes) {
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));
    if (!xml || !xml->hasTagName(apvts.state.getType())) return;
    juce::String extra_json; int program = 0;
    if (auto* extra = xml->getChildByName("MorphoExtra")) {
        extra_json = extra->getStringAttribute("preset"); program = extra->getIntAttribute("program");
        ui_width = extra->getIntAttribute("ui_width", ui_width); eco_visuals = extra->getIntAttribute("eco", eco_visuals ? 1 : 0) != 0;
        xml->removeChildElement(extra, true);
    }
    apvts.replaceState(juce::ValueTree::fromXml(*xml));
    current_program_ = program;
    if (extra_json.isNotEmpty()) {
        PresetData p; PresetIssues is;
        if (preset_from_string(extra_json, p, is)) {
            macro_map_ = p.macros; host_.set_macro_map(macro_map_);
            defects_ = p.defects; current_name_ = p.name;
            // Phase 6b renumbered the bank: resolve the program by name (sessions saved before 6b store an old index)
            current_program_ = find_factory(p.name);
            if (prepared_) push_lattice_state();
        } else last_error_ = is.errors.joinIntoString("; ");
    }
}

juce::AudioProcessorEditor* MorphoProcessor::createEditor() { return new MorphoEditor(*this); }

}  // namespace morpho_plugin

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new morpho_plugin::MorphoProcessor(); }
