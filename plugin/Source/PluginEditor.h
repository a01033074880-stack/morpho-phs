// SPDX-License-Identifier: AGPL-3.0-only
// Phase 6a editor: "BULK / EDGE" (docs/phase6/art_direction.md).  The whole UI is laid out once on a 1152 x 720 design
// canvas (MainPanel) and scaled by an AffineTransform -> fixed 16:10 aspect, crisp vector rendering at any size/DPI.
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "LatticeView.h"
#include "ui/Controls.h"
#include "ui/PresetBrowser.h"

namespace morpho_plugin {

// static furniture (section frames, captions, brand) painted once and cached as an image
class Backdrop : public juce::Component {
public:
    Backdrop() { setInterceptsMouseClicks(false, false); setBufferedToImage(true); setOpaque(true); }
    void paint(juce::Graphics&) override;
    juce::String sub_line;
};

// energy history H(t) + output peak meters, fed from the hero's snapshots
class Monitor : public juce::Component {
public:
    Monitor() { setInterceptsMouseClicks(false, false); }
    void push(const Snapshot& s);
    void paint(juce::Graphics&) override;
private:
    std::array<float, 160> hist_{};
    int head_ = 0, count_ = 0;
    float pl_ = 0, pr_ = 0, hold_l_ = 0, hold_r_ = 0;
};

class CpuChip : public juce::Component, public juce::SettableTooltipClient {
public:
    void set(const juce::String& text, bool warn) { if (text != text_ || warn != warn_) { text_ = text; warn_ = warn; repaint(); } }
    void paint(juce::Graphics&) override;
private:
    juce::String text_; bool warn_ = false;
};

class AboutPanel : public juce::Component {
public:
    AboutPanel() { setWantsKeyboardFocus(true); setTitle("About"); }
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override { setVisible(false); }
    bool keyPressed(const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible(false); return true; } return false; }
};

class TextLine : public juce::Component {
public:
    TextLine() { setInterceptsMouseClicks(false, false); }
    void set(const juce::String& t, juce::Colour c) { if (t != text_ || c != col_) { text_ = t; col_ = c; repaint(); } }
    void paint(juce::Graphics&) override;
    juce::Justification just = juce::Justification::centredLeft;
    float height = 11.f;
private:
    juce::String text_; juce::Colour col_;
};

class Drawer : public juce::Component {
public:
    void paint(juce::Graphics&) override;
    juce::String cpu_text;
};

class MainPanel : public juce::Component, private juce::Timer {
public:
    explicit MainPanel(MorphoProcessor&);
    ~MainPanel() override;
    void resized() override;
    LatticeView& view() { return view_; }
    void set_advanced(bool open, bool animate);
    void set_eco(bool eco);
    void show_about(bool on) { about_.setVisible(on); if (on) { about_.toFront(true); about_.grabKeyboardFocus(); } }
    void show_browser(bool on);
    PresetBrowser& browser() { return browser_; }
    void refresh_now() { timerCallback(); }

private:
    void timerCallback() override;
    void refresh_presets();
    void step_preset(int delta);
    void refresh_macro_names();
    void select_brush(LatticeView::Brush b);
    void save_preset();
    void load_preset();
    void update_cpu_hints();
    void set_status(const juce::String& s, bool sticky);
    ParamKnob* knob(int param_index, const juce::String& caption, ParamKnob::Kind kind, bool bipolar = false);

    MorphoProcessor& proc_;
    Backdrop backdrop_;
    LatticeView view_;
    Monitor monitor_;
    CpuChip cpu_;
    PresetChip chip_;
    PresetBrowser browser_;
    GlyphButton prev_{"Previous preset", GlyphButton::Icon::Prev}, next_{"Next preset", GlyphButton::Icon::Next};
    juce::TextButton save_{"Save"}, load_{"Load"};
    GlyphButton eco_{"Eco", GlyphButton::Icon::Eco}, info_{"About", GlyphButton::Icon::Info};
    GlyphButton b_pluck_{"Pluck", GlyphButton::Icon::Pluck}, b_morph_{"Morph", GlyphButton::Icon::Morph},
        b_chisel_{"Chisel", GlyphButton::Icon::Chisel}, b_erase_{"Erase", GlyphButton::Icon::Erase},
        b_spiral_{"Spiral", GlyphButton::Icon::Spiral}, b_clear_{"Clear", GlyphButton::Icon::Clear}, b_reset_{"Reset", GlyphButton::Icon::Reset};
    juce::TextButton layer_both_{"H+u"}, layer_field_{"H"}, layer_medium_{"u"};
    std::unique_ptr<ChoiceSeg> mode_, lattice_, exciter_, threads_, potential_, interp_;
    juce::OwnedArray<ParamKnob> knobs_;
    std::array<ParamKnob*, 8> macros_{};
    std::vector<ParamKnob*> drawer_knobs_;
    Drawer drawer_;
    GlyphButton adv_{"Advanced", GlyphButton::Icon::Drawer};
    juce::MidiKeyboardComponent keys_;
    AboutPanel about_;
    juce::String status_, status_hint_, cpu_note_, last_name_;
    int sticky_ticks_ = 0;
    bool advanced_ = false;
    std::unique_ptr<juce::FileChooser> chooser_;
    TextLine status_line_, version_line_;
    juce::TooltipWindow tooltips_{this, 600};
};

class MorphoEditor : public juce::AudioProcessorEditor {
public:
    explicit MorphoEditor(MorphoProcessor&);
    ~MorphoEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    MainPanel& panel() { return panel_; }

private:
    MorphoProcessor& proc_;
    MorphoLnF lnf_;
    MainPanel panel_;
};

}  // namespace morpho_plugin
