// SPDX-License-Identifier: AGPL-3.0-only
#include "PluginEditor.h"
#include "ui/Brand.h"
#include "ui/Theme.h"

namespace morpho_plugin {
using namespace theme;

namespace {
// design-canvas layout (8-px grid; see docs/phase6/art_direction.md section 4)
const juce::Rectangle<int> kHeader{0, 0, kDesignW, 56}, kTools{16, 64, 696, 28}, kHero{16, 100, 696, 388};
const juce::Rectangle<int> kMacros{728, 64, 408, 244}, kConfig{728, 316, 408, 64}, kMix{728, 392, 408, 96};
const juce::Rectangle<int> kPhysics{16, 504, 696, 120}, kMonitor{728, 504, 408, 120}, kRowC{16, 632, 1120, 56};
const juce::Rectangle<int> kDrawer{16, 504, 1120, 184}, kStatus{0, 696, kDesignW, 24};

void panel(juce::Graphics& g, juce::Rectangle<int> r, const juce::String& caption, bool accent_tick = false) {
    const auto& P = pal();
    g.setColour(P.bulk1); g.fillRect(r);
    g.setColour(P.seam); g.drawRect(r, 1);
    if (caption.isNotEmpty()) {
        float x = (float)r.getX() + 12.f;
        if (accent_tick) { g.setColour(P.accent); g.fillPath(hexagon({x + 3.f, (float)r.getY() + 14.f}, 3.5f)); x += 12.f; }
        draw_caption(g, caption, {x, (float)r.getY() + 6.f, 200.f, 16.f}, juce::Justification::centredLeft, P.inkDim, 10.5f, 0.14f);
    }
}
}  // namespace

// ================================================================================================ small components
void Backdrop::paint(juce::Graphics& g) {
    const auto& P = pal();
    g.fillAll(P.bulk0);
    g.setColour(P.bulk1); g.fillRect(kHeader);
    g.setColour(P.seam); g.fillRect(0, kHeader.getBottom() - 1, kDesignW, 1);
    brand::draw_glyph(g, {36.f, 28.f}, 15.f, P.accent, P.ink);
    brand::draw_wordmark(g, 62.f, 14.f, 13.f, P.ink, P.accent);
    g.setFont(mono(9.5f)); g.setColour(P.inkDim);
    g.drawText(sub_line, juce::Rectangle<float>(62.f, 32.f, 220.f, 14.f), juce::Justification::centredLeft, false);
    panel(g, kMacros, "Macros", true);
    panel(g, kConfig, {});
    panel(g, kMix, "Mix");
    panel(g, kPhysics, "Physics");
    panel(g, kMonitor, "Monitor");
    panel(g, kRowC, {});
    g.setColour(P.bulk1); g.fillRect(kStatus);
    g.setColour(P.seam); g.fillRect(0, kStatus.getY(), kDesignW, 1);
}

void TextLine::paint(juce::Graphics& g) {
    g.setFont(mono(height)); g.setColour(col_);
    g.drawText(text_, getLocalBounds(), just, true);
}

void Monitor::push(const Snapshot& s) {
    hist_[(size_t)head_] = std::log10(std::max(1e-12f, s.energy_total));
    head_ = (head_ + 1) % (int)hist_.size(); count_ = std::min(count_ + 1, (int)hist_.size());
    auto db = [](float p) { return juce::jlimit(0.f, 1.f, (20.f * std::log10(std::max(1e-6f, p)) + 60.f) / 60.f); };
    pl_ = std::max(db(s.peak_l), pl_ * 0.86f); pr_ = std::max(db(s.peak_r), pr_ * 0.86f);
    hold_l_ = std::max(pl_, hold_l_ - 0.01f); hold_r_ = std::max(pr_, hold_r_ - 0.01f);
    repaint();
}
void Monitor::paint(juce::Graphics& g) {
    const auto& P = pal();
    auto r = getLocalBounds().toFloat();
    auto meters = r.removeFromRight(44.f);
    r.removeFromRight(12.f);
    // H(t): log energy history, auto-ranged over the visible window
    g.setColour(P.bulk0); g.fillRect(r);
    g.setColour(P.seam); g.drawRect(r, 1.f);
    g.setFont(mono(9.5f)); g.setColour(P.inkDim);
    g.drawText("H(t)  LOG", r.reduced(6.f, 3.f), juce::Justification::topLeft, false);
    if (count_ > 1) {
        float lo = 1e9f, hi = -1e9f;
        for (int i = 0; i < count_; ++i) { lo = std::min(lo, hist_[(size_t)i]); hi = std::max(hi, hist_[(size_t)i]); }
        lo = std::min(lo, hi - 1.f);
        const auto pr = r.reduced(4.f, 6.f).withTrimmedTop(10.f);
        juce::Path p;
        const int n = (int)hist_.size();
        for (int k = 0; k < count_; ++k) {
            const int i = (head_ - count_ + k + n) % n;
            const float x = pr.getRight() - pr.getWidth() * (float)(count_ - 1 - k) / (float)(n - 1);
            const float y = pr.getBottom() - pr.getHeight() * (hist_[(size_t)i] - lo) / (hi - lo);
            if (k == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
        }
        g.setColour(P.ink.withAlpha(0.75f)); g.strokePath(p, juce::PathStrokeType(1.2f));
        const float yl = pr.getBottom() - pr.getHeight() * (hist_[(size_t)((head_ - 1 + n) % n)] - lo) / (hi - lo);
        g.setColour(heat(0.85f)); g.fillPath(hexagon({pr.getRight(), yl}, 3.f));
    }
    // output peaks: L / R bars coloured by the heat ramp (level is energy)
    for (int ch = 0; ch < 2; ++ch) {
        auto b = meters.removeFromLeft(18.f).withTrimmedTop(12.f).withTrimmedBottom(2.f);
        meters.removeFromLeft(8.f);
        const float v = ch == 0 ? pl_ : pr_, h = ch == 0 ? hold_l_ : hold_r_;
        g.setColour(P.bulk0); g.fillRect(b); g.setColour(P.seam); g.drawRect(b, 1.f);
        const int segs = 18;
        const float sh = (b.getHeight() - 4.f) / (float)segs;
        for (int k = 0; k < segs; ++k) {
            const float t = (float)(k + 1) / (float)segs;
            if (t > v + 1e-4f) break;
            g.setColour(heat(0.35f + 0.65f * t));
            g.fillRect(juce::Rectangle<float>(b.getX() + 3.f, b.getBottom() - 2.f - sh * (float)(k + 1) + 1.f, b.getWidth() - 6.f, sh - 1.5f));
        }
        g.setColour(P.ink); g.fillRect(juce::Rectangle<float>(b.getX() + 3.f, b.getBottom() - 2.f - (b.getHeight() - 4.f) * h, b.getWidth() - 6.f, 1.f));
        g.setFont(mono(9.5f)); g.setColour(P.inkDim);
        g.drawText(ch == 0 ? "L" : "R", juce::Rectangle<float>(b.getX(), b.getY() - 12.f, b.getWidth(), 11.f), juce::Justification::centred, false);
    }
}

void CpuChip::paint(juce::Graphics& g) {
    const auto& P = pal();
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const auto path = hex_pill(r);
    g.setColour(warn_ ? P.warn.withAlpha(0.12f) : P.bulk0); g.fillPath(path);
    g.setColour(warn_ ? P.warn : P.seam); g.strokePath(path, juce::PathStrokeType(1.f));
    auto t = r.reduced(14.f, 0.f);
    draw_caption(g, "CPU", t.removeFromLeft(30.f), juce::Justification::centredLeft, warn_ ? P.warn : P.inkDim, 10.5f, 0.12f);
    g.setFont(mono(11.f, true)); g.setColour(warn_ ? P.warn : P.ink);
    g.drawText(text_, t, juce::Justification::centredRight, false);
}

void Drawer::paint(juce::Graphics& g) {
    const auto& P = pal();
    g.fillAll(P.bulk1);
    g.setColour(P.seamHi); g.fillRect(0, 0, getWidth(), 1);
    g.setColour(P.seam); g.drawRect(getLocalBounds(), 1);
    g.setColour(P.accent); g.fillPath(hexagon({15.f, 14.f}, 3.5f));
    draw_caption(g, "Advanced  /  engine", {26.f, 6.f, 300.f, 16.f}, juce::Justification::centredLeft, P.inkDim, 10.5f, 0.14f);
    if (cpu_text.isNotEmpty()) {
        auto r = juce::Rectangle<float>(744.f, 88.f, (float)getWidth() - 756.f, 88.f);
        g.setColour(P.bulk0); g.fillRect(r); g.setColour(P.seam); g.drawRect(r, 1.f);
        juce::AttributedString s; s.append(cpu_text, mono(10.5f), P.inkDim); s.setLineSpacing(2.f);
        juce::TextLayout tl; tl.createLayout(s, r.getWidth() - 16.f);
        tl.draw(g, r.reduced(8.f, 6.f));
    }
}

void AboutPanel::paint(juce::Graphics& g) {
    const auto& P = pal();
    g.fillAll(P.bulk0.withAlpha(0.94f));
    const juce::Rectangle<float> card(296.f, 84.f, 560.f, 552.f);
    g.setColour(P.bulk1); g.fillRect(card);
    g.setColour(P.seamHi); g.drawRect(card, 1.f);
    g.setColour(P.accent); g.fillRect(card.withHeight(2.f));
    brand::draw_glyph(g, {card.getCentreX(), card.getY() + 78.f}, 44.f, P.accent, P.ink);
    {   // centred wordmark: measure first
        juce::Path a, b; float w = brand::wordmark(a, b); if (kIsFxVariant) { juce::Path c, d; w += brand::kWordGap + brand::wordmark_fx_suffix(c, d); }
        const float cap = 20.f, ww = w * cap / brand::kCapHeight;
        brand::draw_wordmark(g, card.getCentreX() - ww * 0.5f, card.getY() + 140.f, cap, P.ink, P.accent);
    }
    auto r = card.reduced(40.f, 0.f).withTrimmedTop(172.f);
    g.setFont(mono(11.f)); g.setColour(P.inkDim);
    g.drawText(juce::String("VERSION ") + JucePlugin_VersionString + (kIsFxVariant ? "  /  TOPOLOGICAL EDGE FX" : "  /  GYROSCOPIC LATTICE SYNTH"),
               r.removeFromTop(18.f), juce::Justification::centred, false);
    r.removeFromTop(14.f);
    juce::AttributedString s; s.setJustification(juce::Justification::topLeft); s.setLineSpacing(3.f);
    auto para = [&](const juce::String& head, const juce::String& body) {
        s.append(head.toUpperCase() + "\n", sans(10.5f, true).withExtraKerningFactor(0.12f), P.accent);
        s.append(body + "\n\n", sans(13.f), P.ink);
    };
    para("Instrument", "A port-Hamiltonian honeycomb of gyroscopes. Energy (light) flows through the lattice; "
                       "chirality opens a band gap whose topological edge channel carries sound around the boundary. "
                       "A Barkley excitable medium (hatching) grows defects that reshape the polyhedra.");
    para("Credits", "Design, DSP and visual identity: the MORPHO-PHS project. Brand marks, icons and every UI element are "
                    "drawn in code from the lattice geometry (scripts/brand/gen_brand.py).");
    para("Licences", "MORPHO-PHS: GNU AGPL v3.  JUCE 8: AGPLv3 option.  clap-juce-extensions: MIT.  "
                     "IBM Plex Sans Condensed / IBM Plex Mono: SIL Open Font License 1.1 (Reserved Font Name \"Plex\"), embedded unmodified.  "
                     "Source: see LICENSE and THIRD_PARTY_NOTICES.");
    juce::TextLayout tl; tl.createLayout(s, r.getWidth());
    tl.draw(g, r);
    g.setFont(mono(10.f)); g.setColour(P.inkDim);
    g.drawText("CLICK OR ESC TO CLOSE", card.withTrimmedTop(card.getHeight() - 28.f), juce::Justification::centred, false);
}

// ================================================================================================ MainPanel
ParamKnob* MainPanel::knob(int idx, const juce::String& caption, ParamKnob::Kind kind, bool bipolar) {
    auto* p = proc_.apvts.getParameter(kParams[(size_t)idx].id);
    jassert(p != nullptr);
    auto* k = knobs_.add(new ParamKnob(*p, idx, caption, kind, bipolar));
    k->setTooltip(juce::String(kParams[(size_t)idx].name) + "  -  drag (Shift = fine), double-click = default, wheel");
    return k;
}

MainPanel::MainPanel(MorphoProcessor& p)
    : proc_(p), view_(p), chip_(p), browser_(p), keys_(p.keyboard, juce::MidiKeyboardComponent::horizontalKeyboard) {
    setOpaque(true);
    backdrop_.sub_line = kIsFxVariant ? "TOPOLOGICAL EDGE FX" : "GYROSCOPIC LATTICE SYNTH";
    addAndMakeVisible(backdrop_);
    addAndMakeVisible(view_);
    addAndMakeVisible(monitor_);
    view_.on_frame = [this](const Snapshot& s) { monitor_.push(s); };

    // ---- header: presets + CPU + eco + about ----
    addAndMakeVisible(chip_);
    chip_.setTooltip("Open the preset browser (categories, tags, favourites).  Hexagram = favourite");
    chip_.on_open = [this] { show_browser(!browser_.isVisible()); };
    chip_.on_favourite = [this] { browser_.rebuild(); };
    addChildComponent(browser_);
    browser_.on_loaded = [this] { refresh_presets(); refresh_macro_names(); };
    browser_.on_close = [this] { show_browser(false); };
    prev_.show_label = next_.show_label = false;
    for (juce::Component* c : std::initializer_list<juce::Component*>{&prev_, &next_, &save_, &load_, &cpu_, &eco_, &info_}) addAndMakeVisible(c);
    prev_.setTooltip("Previous preset"); next_.setTooltip("Next preset");
    prev_.onClick = [this] { step_preset(-1); };
    next_.onClick = [this] { step_preset(+1); };
    save_.onClick = [this] { save_preset(); };
    load_.onClick = [this] { load_preset(); };
    save_.setTooltip("Save the current state as a .morpho preset"); load_.setTooltip("Load a .morpho preset");
    eco_.setClickingTogglesState(true);
    eco_.setTooltip("Low-motion visuals: 12 fps, no displacement needles, instant drawer (saved with the session)");
    eco_.onClick = [this] { set_eco(eco_.getToggleState()); };
    info_.onClick = [this] { show_about(true); };
    info_.setTooltip("About, credits and licences");

    // ---- lattice tools ----
    auto brush_btn = [this](GlyphButton& b, LatticeView::Brush br, const char* tip) {
        b.onClick = [this, br] { select_brush(br); }; b.setTooltip(tip); addAndMakeVisible(b); };
    brush_btn(b_pluck_, LatticeView::Brush::Pluck, "Pluck: click to strike the lattice at a site");
    brush_btn(b_morph_, LatticeView::Brush::Morphogen, "Morph: paint morphogen into the Barkley medium (drag)");
    brush_btn(b_chisel_, LatticeView::Brush::Chisel, "Chisel: add mass defects (drag)");
    brush_btn(b_erase_, LatticeView::Brush::Erase, "Erase: remove defects (also right-click with any brush)");
    brush_btn(b_spiral_, LatticeView::Brush::Spiral, "Spiral: seed a rotating Barkley spiral wave");
    for (auto* b : {&b_clear_, &b_reset_}) addAndMakeVisible(b);
    b_clear_.setTooltip("Clear all defects"); b_reset_.setTooltip("Reset the field and the medium");
    b_clear_.onClick = [this] { proc_.send_event({UiEvent::ClearDefects, 0, 0, 0}); };
    b_reset_.onClick = [this] { proc_.send_event({UiEvent::ResetField, 0, 0, 0}); };
    {
        int i = 0;
        const char* tips[] = {"Show energy H (light) and the Barkley medium u (hatching)", "Show energy H only", "Show the Barkley medium u only"};
        for (auto* b : {&layer_both_, &layer_field_, &layer_medium_}) {
            b->setRadioGroupId(9101, juce::dontSendNotification);
            b->setConnectedEdges((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
            b->setTooltip(tips[i]);
            b->onClick = [this, i] {
                view_.set_layer((LatticeView::Layer)i);
                layer_both_.setToggleState(i == 0, juce::dontSendNotification); layer_field_.setToggleState(i == 1, juce::dontSendNotification);
                layer_medium_.setToggleState(i == 2, juce::dontSendNotification);
            };
            addAndMakeVisible(b); ++i;
        }
        layer_both_.setToggleState(true, juce::dontSendNotification);
    }
    select_brush(LatticeView::Brush::Pluck);

    // ---- choices ----
    auto choice = [this](int idx) -> juce::RangedAudioParameter& { return *proc_.apvts.getParameter(kParams[(size_t)idx].id); };
    if (kIsFxVariant) mode_ = std::make_unique<ChoiceSeg>(choice(pMode), juce::StringArray{"Edge disperser", "Defect freezer"}, "Mode");
    else exciter_ = std::make_unique<ChoiceSeg>(choice(pExciter), juce::StringArray{"Bow", "Pluck"}, "Exciter");
    lattice_ = std::make_unique<ChoiceSeg>(choice(pLattice), juce::StringArray{"8", "16", "32"}, "Lattice");
    threads_ = std::make_unique<ChoiceSeg>(choice(pThreads), juce::StringArray{"Auto", "1", "2", "3", "4", "6", "8"}, "Threads");
    potential_ = std::make_unique<ChoiceSeg>(choice(pPotential), juce::StringArray{"Quartic", "HQ fast", "HQ exact"}, "Potential");
    interp_ = std::make_unique<ChoiceSeg>(choice(pInterp), juce::StringArray{"1", "8", "Off"}, "g/mu interp");
    for (auto* c : {mode_.get(), exciter_.get(), lattice_.get()}) if (c) addAndMakeVisible(c);
    lattice_->set_tip("Lattice size: 8x8 / 16x16 / 32x32 unit cells (32x32 = 2048 gyroscopes)");

    // ---- knobs ----
    for (int m = 0; m < 8; ++m) { macros_[(size_t)m] = knob(pMacro1 + m, "Macro " + juce::String(m + 1), ParamKnob::Kind::Macro); addAndMakeVisible(macros_[(size_t)m]); }
    const int phys[6] = {pTone, pChirality, pNonlinear, pDecay, kIsFxVariant ? pFreeze : pLife, pMorph};
    const char* physName[6] = {"Tone", "Chirality", "Nonlinear", "Decay", kIsFxVariant ? "Freeze" : "Life", "Morphogen"};
    for (int i = 0; i < 6; ++i) addAndMakeVisible(knob(phys[i], physName[i], ParamKnob::Kind::Main, phys[i] == pChirality));
    for (auto [idx, nm] : {std::pair{(int)pWet, "Lattice"}, {(int)pDry, "Dry"}, {(int)pInGain, "In"}, {(int)pOutGain, "Out"}})
        addAndMakeVisible(knob(idx, nm, ParamKnob::Kind::Small, idx == pInGain || idx == pOutGain));
    if (kIsFxVariant)
        for (auto [idx, nm] : {std::pair{(int)pEdgeWidth, "Edge width"}, {(int)pEdgeCrosstalk, "X-talk"}, {(int)pSpread, "Spread"}, {(int)pDefectDepth, "Defect"}})
            addAndMakeVisible(knob(idx, nm, ParamKnob::Kind::Bar));
    else
        addAndMakeVisible(keys_);

    // ---- advanced drawer (slides up over rows B and C) ----
    addChildComponent(drawer_);
    drawer_.setOpaque(true);
    for (auto* c : {threads_.get(), potential_.get(), interp_.get()}) drawer_.addAndMakeVisible(c);
    interp_->set_tip("g/mu parameter interpolation step (1 sample = smoothest; Off = per 64-sample block, cheapest)");
    std::vector<std::pair<int, const char*>> dk = {{pHQ, "HQ amount"}, {pTau, "Tau"}};
    if (kIsFxVariant) dk.push_back({pLife, "Life"});
    else { dk.push_back({pFreeze, "Freeze"}); dk.push_back({pSpread, "Spread"}); dk.push_back({pDefectDepth, "Defect"});
           dk.push_back({pEdgeWidth, "Edge width"}); dk.push_back({pEdgeCrosstalk, "X-talk"}); }
    for (auto [idx, nm] : dk) { auto* k = knob(idx, nm, ParamKnob::Kind::Small); drawer_knobs_.push_back(k); drawer_.addAndMakeVisible(k); }
    addAndMakeVisible(adv_);
    adv_.setTooltip("Engine settings: threads, potential, interpolation and the secondary physics controls");
    adv_.onClick = [this] { set_advanced(!advanced_, !view_.eco()); };

    // ---- keyboard / status ----
    keys_.setAvailableRange(36, 96); keys_.setLowestVisibleKey(36);
    keys_.setKeyWidth((float)kRowC.getWidth() / 36.f);
    keys_.setScrollButtonsVisible(false);
    keys_.setTitle("Keyboard");
    addAndMakeVisible(status_line_);
    version_line_.just = juce::Justification::centredRight;
    version_line_.set(juce::String("v") + JucePlugin_VersionString + "  /  AGPLv3", pal().inkDim);
    addAndMakeVisible(version_line_);
    addChildComponent(about_);

    view_.set_eco(proc_.eco_visuals);
    eco_.setToggleState(proc_.eco_visuals, juce::dontSendNotification);
    refresh_presets(); refresh_macro_names(); update_cpu_hints();
    setSize(kDesignW, kDesignH);   // last: resized() needs every child
    timerCallback();
    startTimerHz(4);
}

MainPanel::~MainPanel() { stopTimer(); }

void MainPanel::resized() {
    backdrop_.setBounds(getLocalBounds());
    // header
    prev_.setBounds(280, 14, 28, 28); chip_.setBounds(312, 14, 296, 28); next_.setBounds(612, 14, 28, 28);
    save_.setBounds(652, 14, 56, 28); load_.setBounds(712, 14, 56, 28);
    cpu_.setBounds(800, 14, 168, 28);
    eco_.setBounds(984, 14, 72, 28); info_.setBounds(1064, 14, 72, 28);
    // tools + hero
    int x = kTools.getX();
    for (auto* b : {&b_pluck_, &b_morph_, &b_chisel_, &b_erase_, &b_spiral_}) { b->setBounds(x, kTools.getY(), 82, kTools.getHeight()); x += 86; }
    x += 8;
    for (auto* b : {&b_clear_, &b_reset_}) { b->setBounds(x, kTools.getY(), 76, kTools.getHeight()); x += 80; }
    const int lw = (kTools.getRight() - (x + 8)) / 3;
    int lx = kTools.getRight() - 3 * lw;
    for (auto* b : {&layer_both_, &layer_field_, &layer_medium_}) { b->setBounds(lx, kTools.getY(), lw, kTools.getHeight()); lx += lw; }
    view_.setBounds(kHero);
    // right column
    for (int m = 0; m < 8; ++m) macros_[(size_t)m]->setBounds(kMacros.getX() + 8 + (m % 4) * 98, kMacros.getY() + 28 + (m / 4) * 106, 98, 104);
    if (mode_) { mode_->setBounds(kConfig.getX() + 12, kConfig.getY() + 8, 244, 46); lattice_->setBounds(kConfig.getX() + 272, kConfig.getY() + 8, 124, 46); }
    else { exciter_->setBounds(kConfig.getX() + 12, kConfig.getY() + 8, 184, 46); lattice_->setBounds(kConfig.getX() + 212, kConfig.getY() + 8, 184, 46); }
    // knobs by kind, in creation order: 8 macros, 6 physics, 4 mix, [4 bars], drawer knobs
    int phys = 0, mix = 0, bar = 0;
    for (auto* k : knobs_) {
        if (std::find(macros_.begin(), macros_.end(), k) != macros_.end()) continue;
        if (std::find(drawer_knobs_.begin(), drawer_knobs_.end(), k) != drawer_knobs_.end()) continue;
        if (phys < 6) { k->setBounds(kPhysics.getX() + 12 + phys * 112, kPhysics.getY() + 22, 112, 96); ++phys; }
        else if (mix < 4) { k->setBounds(kMix.getX() + 8 + mix * 98, kMix.getY() + 18, 98, 76); ++mix; }
        else { k->setBounds(kRowC.getX() + 16 + bar * 280, kRowC.getY() + 8, 244, 40); ++bar; }
    }
    monitor_.setBounds(kMonitor.getX() + 12, kMonitor.getY() + 24, kMonitor.getWidth() - 24, kMonitor.getHeight() - 32);
    keys_.setBounds(kRowC.reduced(1));
    // drawer (local layout)
    threads_->setBounds(12, 28, 420, 46);
    potential_->setBounds(448, 28, 330, 46);
    interp_->setBounds(794, 28, kDrawer.getWidth() - 806, 46);
    for (size_t i = 0; i < drawer_knobs_.size(); ++i) drawer_knobs_[i]->setBounds(8 + (int)i * 100, 86, 100, 92);
    drawer_.setBounds(advanced_ ? kDrawer : kDrawer.withY(kStatus.getY()));
    // status
    adv_.setBounds(16, kStatus.getY() + 3, 120, 19);
    status_line_.setBounds(152, kStatus.getY() + 1, 820, kStatus.getHeight() - 1);
    version_line_.setBounds(960, kStatus.getY() + 1, 176, kStatus.getHeight() - 1);
    about_.setBounds(getLocalBounds());
    browser_.setBounds(0, kHeader.getBottom(), kDesignW, kStatus.getY() - kHeader.getBottom());
}

void MainPanel::show_browser(bool on) {
    chip_.open = on; chip_.repaint();
    if (on) { browser_.rebuild(); browser_.setVisible(true); browser_.toFront(true); browser_.grabKeyboardFocus(); }
    else browser_.setVisible(false);
    about_.toFront(false);
}

// prev / next walk the bank in order (Init or a user file -> first / last preset)
void MainPanel::step_preset(int delta) {
    const int n = proc_.factory().size();
    if (n == 0) return;
    const int cur = proc_.current_program_index();
    proc_.setCurrentProgram(cur < 0 ? (delta > 0 ? 0 : n - 1) : (cur + delta + n) % n);
    refresh_presets(); refresh_macro_names();
}

void MainPanel::set_advanced(bool open, bool animate) {
    advanced_ = open;
    adv_.setToggleState(open, juce::dontSendNotification);
    adv_.flipped = open; adv_.repaint();
    auto& anim = juce::Desktop::getInstance().getAnimator();
    const auto target = open ? kDrawer : kDrawer.withY(kStatus.getY()).withHeight(kDrawer.getHeight());
    if (open) { drawer_.setVisible(true); drawer_.toFront(false); adv_.toFront(false); }
    if (animate) {
        if (open) drawer_.setBounds(kDrawer.withY(kStatus.getY()));
        anim.animateComponent(&drawer_, target, 1.f, 160, false, 0.0, 1.0);
    } else { anim.cancelAnimation(&drawer_, false); drawer_.setBounds(target); }
    if (!open && !animate) drawer_.setVisible(false);
    if (!open && animate) juce::Timer::callAfterDelay(200, [sp = juce::Component::SafePointer<MainPanel>(this)] { if (sp && !sp->advanced_) sp->drawer_.setVisible(false); });
    about_.toFront(false);
}

void MainPanel::set_eco(bool e) {
    proc_.eco_visuals = e;
    eco_.setToggleState(e, juce::dontSendNotification);
    view_.set_eco(e);
}

void MainPanel::select_brush(LatticeView::Brush b) {
    view_.set_brush(b);
    b_pluck_.setToggleState(b == LatticeView::Brush::Pluck, juce::dontSendNotification);
    b_morph_.setToggleState(b == LatticeView::Brush::Morphogen, juce::dontSendNotification);
    b_chisel_.setToggleState(b == LatticeView::Brush::Chisel, juce::dontSendNotification);
    b_erase_.setToggleState(b == LatticeView::Brush::Erase, juce::dontSendNotification);
    b_spiral_.setToggleState(b == LatticeView::Brush::Spiral, juce::dontSendNotification);
}

void MainPanel::refresh_presets() {
    chip_.repaint();
    if (browser_.isVisible()) browser_.sync_to_current();
}

void MainPanel::refresh_macro_names() {
    const auto& mm = proc_.macro_map();
    for (int k = 0; k < 8; ++k) {
        juce::String nm(mm.names[(size_t)k].data());
        bool mapped = false; juce::String targets;
        for (int t = 0; t < mm.count; ++t)
            if (mm.t[(size_t)t].macro == k) { mapped = true; if (mm.t[(size_t)t].param >= 0) targets << (targets.isEmpty() ? "" : ", ") << kParams[(size_t)mm.t[(size_t)t].param].name; }
        auto* m = macros_[(size_t)k];
        m->set_caption(nm.isEmpty() ? "Macro " + juce::String(k + 1) : nm);
        m->set_dimmed(!mapped);
        m->setTooltip(mapped ? "Macro " + juce::String(k + 1) + " -> " + targets : "Macro " + juce::String(k + 1) + " (not assigned by this preset)");
    }
}

void MainPanel::save_preset() {
    chooser_ = std::make_unique<juce::FileChooser>("Save .morpho preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.morpho");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {
        auto f = fc.getResult(); if (f == juce::File()) return;
        f = f.withFileExtension(".morpho");
        const auto pd = proc_.capture_preset(f.getFileNameWithoutExtension());
        set_status(f.replaceWithText(preset_to_string(pd)) ? "Saved " + f.getFileName() : "Could not write " + f.getFullPathName(), true);
    });
}

void MainPanel::load_preset() {
    chooser_ = std::make_unique<juce::FileChooser>("Load .morpho preset", juce::File(), "*.morpho");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult(); if (f == juce::File()) return;
        PresetData pd; PresetIssues is;
        if (preset_from_file(f, pd, is)) { proc_.load_preset(pd); refresh_macro_names(); set_status("Loaded " + pd.name, true); }
        else set_status("Preset error: " + is.errors.joinIntoString("; "), true);
    });
}

void MainPanel::set_status(const juce::String& s, bool sticky) {
    status_ = s; sticky_ticks_ = sticky ? 24 : 0;   // sticky messages stay ~6 s
    status_line_.set(s, sticky ? pal().ink : pal().inkDim);
}

// Phase 5c rules (README section 3.6), now surfaced in the header CPU chip:
//   * any HQ potential at 32x32 (and HQ exact at 16x16) -> "HIGH CPU" (heat colour) + tooltip
//   * HQ exact at 32x32 runs as HQ fast -> "HQ EXACT>FAST"
//   * Auto threads shows what it resolves to
void MainPanel::update_cpu_hints() {
    const int L = lattice_size_from_choice((int)std::lround(proc_.param_now(pLattice)));
    const int pot = (int)std::lround(proc_.param_now(pPotential));
    const int mode = (int)std::lround(proc_.param_now(pMode));
    const int tc = (int)std::lround(proc_.param_now(pThreads));
    const int autoT = threads_from_choice(0, L, mode, pot);
    const int T = tc == 0 ? autoT : threads_from_choice(tc, L, mode, pot);
    const bool limited = hq_exact_limited(pot, L), high = high_cpu_setting(pot, L);
    juce::String chip = (tc == 0 ? "AUTO " : "") + juce::String(T) + "T";
    if (limited) chip = "HQ>FAST " + chip; else if (high) chip = "HIGH " + chip;
    cpu_.set(chip, high);
    juce::String potTip = "Quartic: lowest CPU.  HQ (fast log-cosh): about 1.5-2x the CPU of Quartic at 32x32 (Auto uses 3 threads).  "
                          "HQ exact: 16x16 or smaller only; at 32x32 it runs as HQ fast.";
    if (limited) potTip = "HQ exact is limited to 16x16 or smaller (too slow for real time at 32x32). This lattice runs HQ fast. " + potTip;
    else if (high) potTip = "High CPU setting. " + potTip;
    potential_->set_caption(limited ? "Potential  -  HQ fast @32" : (high ? "Potential  -  high CPU" : "Potential"), high);
    potential_->set_tip(potTip);
    const juce::String thrTip = "Auto: 8x8/16x16 -> 1 (HQ exact 16x16 -> 2); 32x32 -> FX 2, Instrument 3, HQ 3 (capped by the P-cores). Now Auto = " + juce::String(autoT) + ".";
    threads_->set_caption(tc == 0 ? "Threads  -  auto = " + juce::String(autoT) : "Threads", false);
    threads_->set_tip(thrTip);
    cpu_.setTooltip((high ? juce::String("High CPU setting.  ") : juce::String()) + juce::String(L) + "x" + juce::String(L) + " lattice, " + juce::String(T)
                    + (T == 1 ? " thread.  " : " threads.  ") + thrTip);
    drawer_.cpu_text = juce::String(L) + "x" + juce::String(L) + " / " + (pot == 0 ? "QUARTIC" : (pot == 1 ? "HQ FAST" : (limited ? "HQ EXACT -> RUNS HQ FAST" : "HQ EXACT")))
                       + " / " + juce::String(T) + (tc == 0 ? " THREADS (AUTO)" : " THREADS") + (high ? "\nHIGH CPU: expect 1.5-2x the Quartic cost." : "\nWithin the real-time budget on 4+ cores.");
    drawer_.repaint(juce::Rectangle<int>(744, 88, kDrawer.getWidth() - 756, 88));
    cpu_note_ = limited ? "  /  HQ exact is 16x16-only: running HQ fast at 32x32 (high CPU)"
              : high    ? "  /  High CPU: HQ potential at " + juce::String(L) + "x" + juce::String(L)
                        : juce::String();
}

void MainPanel::timerCallback() {
    if (proc_.current_preset_name() != last_name_) { last_name_ = proc_.current_preset_name(); refresh_presets(); refresh_macro_names(); }
    update_cpu_hints();
    if (sticky_ticks_ > 0) { --sticky_ticks_; return; }
    const int mode = (int)proc_.param_now(pMode);
    const char* hint = mode == 0 ? "EDGE DISPERSER: in-gap band -> chiral edge channel, mixed with the dry full-band path"
                     : mode == 1 ? "DEFECT FREEZER: Freeze -> lossless lattice (mu = eta = 0) holds the field"
                                 : "INSTRUMENT: MPE voices drive one shared multi-port lattice; Life = gated self-oscillation";
    set_status(juce::String(hint) + cpu_note_, false);
}

// ================================================================================================ editor
MorphoEditor::MorphoEditor(MorphoProcessor& p) : juce::AudioProcessorEditor(p), proc_(p), panel_(p) {
    setLookAndFeel(&lnf_);
    addAndMakeVisible(panel_);
    setResizable(true, true);
    setResizeLimits(kMinW, kMinH, kMaxW, kMaxH);
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double)kDesignW / (double)kDesignH);
    const int w = p.ui_width >= kMinW && p.ui_width <= kMaxW ? p.ui_width : kDesignW;
    setSize(w, juce::roundToInt((double)w * kDesignH / kDesignW));
    setTitle(JucePlugin_Name);
}

MorphoEditor::~MorphoEditor() {
    proc_.host().engine().snapshot_hz.store(0, std::memory_order_relaxed);   // the visualiser stops with the editor
    setLookAndFeel(nullptr);
}

void MorphoEditor::paint(juce::Graphics& g) { g.fillAll(pal().bulk0); }

void MorphoEditor::resized() {
    const float s = (float)getWidth() / (float)kDesignW;
    panel_.setTransform(juce::AffineTransform::scale(s));
    panel_.setBounds(0, 0, kDesignW, kDesignH);
    proc_.ui_width = getWidth();
}

}  // namespace morpho_plugin
