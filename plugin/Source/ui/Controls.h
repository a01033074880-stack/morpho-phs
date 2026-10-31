// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS Phase 6a controls: LookAndFeel ("BULK / EDGE"), the gyro knob, segmented choices and glyph buttons.
// All drawing is vector (crisp at any scale); no bitmaps, no blur, no third-party artwork.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Theme.h"

namespace morpho_plugin {

class MorphoLnF : public juce::LookAndFeel_V4 {
public:
    MorphoLnF();
    juce::Typeface::Ptr getTypefaceForFont(const juce::Font&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    juce::Font getTextButtonFont(juce::TextButton&, int h) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    void positionComboBoxText(juce::ComboBox&, juce::Label&) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool sep, bool active, bool highlighted, bool ticked,
                           bool hasSub, const juce::String& text, const juce::String& shortcut, const juce::Drawable* icon,
                           const juce::Colour* textColour) override;
    void drawPopupMenuSectionHeader(juce::Graphics&, const juce::Rectangle<int>&, const juce::String&) override;
    juce::Font getPopupMenuFont() override;
    void drawTooltip(juce::Graphics&, const juce::String&, int w, int h) override;
    juce::Rectangle<int> getTooltipBounds(const juce::String&, juce::Point<int>, juce::Rectangle<int>) override;
    int getPopupMenuBorderSize() override { return 2; }
};

// value formatting shared by knobs and readouts (plain units)
juce::String format_param(int param_index, float plain);

// ---------------------------------------------------------------------------------------------------------------
// Gyro knob: one parameter, drawn as a honeycomb site whose three bonds rotate with the value.  Relative drag (no
// jumps), Shift = fine (x5), double-click / Alt-click = default, wheel, arrow keys; host gestures via
// juce::ParameterAttachment; accessible as a ranged value.
class ParamKnob : public juce::Component, public juce::SettableTooltipClient {
public:
    enum class Kind { Macro, Main, Small, Bar };
    ParamKnob(juce::RangedAudioParameter& p, int param_index, const juce::String& caption, Kind kind, bool bipolar = false);
    ~ParamKnob() override;
    void set_caption(const juce::String& c) { if (c != caption_) { caption_ = c; repaint(); } }
    void set_dimmed(bool d) { if (d != dimmed_) { dimmed_ = d; repaint(); } }
    void set_readout_override(const juce::String& s) { if (s != override_) { override_ = s; repaint(); } }
    float value01() const noexcept { return v01_; }

    void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    juce::RangedAudioParameter& param() noexcept { return p_; }
    void set_normalised(float v01, bool as_gesture_step);   // used by the accessibility interface / keys

private:
    juce::Rectangle<float> dial_bounds() const;
    juce::RangedAudioParameter& p_;
    int index_;
    juce::String caption_, override_;
    Kind kind_;
    bool bipolar_, hover_ = false, dragging_ = false, dimmed_ = false;
    float v01_ = 0.f;
    juce::Point<float> last_;
    std::unique_ptr<juce::ParameterAttachment> att_;
};

// ---------------------------------------------------------------------------------------------------------------
// Segmented choice for a Choice parameter (all options visible; radio buttons -> accessible for free)
class ChoiceSeg : public juce::Component {
public:
    ChoiceSeg(juce::RangedAudioParameter& p, const juce::StringArray& labels, const juce::String& caption);
    void set_caption(const juce::String& c, bool warn = false) { if (c != caption_ || warn != warn_) { caption_ = c; warn_ = warn; repaint(); } }
    void set_tip(const juce::String& t) { for (auto* b : buttons_) b->setTooltip(t); }
    void paint(juce::Graphics&) override;
    void resized() override;
    int caption_height = 14;

private:
    juce::RangedAudioParameter& p_;
    juce::String caption_;
    bool warn_ = false;
    juce::OwnedArray<juce::TextButton> buttons_;
    std::unique_ptr<juce::ParameterAttachment> att_;
    int current_ = 0;
};

// ---------------------------------------------------------------------------------------------------------------
// Glyph button (brushes / actions): a hex pill with a code-drawn icon and a short label
class GlyphButton : public juce::Button {
public:
    enum class Icon { Pluck, Morph, Chisel, Erase, Spiral, Clear, Reset, Info, Eco, Prev, Next, Drawer };
    GlyphButton(const juce::String& label, Icon icon);
    void paintButton(juce::Graphics&, bool over, bool down) override;
    bool show_label = true;
    static void draw_icon(juce::Graphics&, Icon, juce::Rectangle<float> r, juce::Colour c, bool flipped = false);
    bool flipped = false;   // drawer chevron direction
private:
    Icon icon_;
};

}  // namespace morpho_plugin
