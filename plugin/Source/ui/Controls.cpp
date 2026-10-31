// SPDX-License-Identifier: AGPL-3.0-only
#include "Controls.h"
#include "../engine/Params.hpp"
#include <cmath>

namespace morpho_plugin {
using namespace theme;

namespace {
constexpr float kA0 = -2.35619449f, kA1 = 2.35619449f;   // -135 .. +135 degrees (clockwise from 12 o'clock)
juce::Point<float> polar(juce::Point<float> c, float r, float a) { return {c.x + r * std::sin(a), c.y - r * std::cos(a)}; }
}

// ================================================================================================ LookAndFeel
MorphoLnF::MorphoLnF() {
    const auto& p = pal();
    setColour(juce::ResizableWindow::backgroundColourId, p.bulk0);
    setColour(juce::PopupMenu::backgroundColourId, p.bulk1);
    setColour(juce::PopupMenu::textColourId, p.ink);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, p.accent.withAlpha(0.16f));
    setColour(juce::PopupMenu::highlightedTextColourId, p.ink);
    setColour(juce::PopupMenu::headerTextColourId, p.inkDim);
    setColour(juce::ComboBox::textColourId, p.ink);
    setColour(juce::ComboBox::backgroundColourId, p.bulk1);
    setColour(juce::ComboBox::outlineColourId, p.seam);
    setColour(juce::ComboBox::arrowColourId, p.inkDim);
    setColour(juce::Label::textColourId, p.ink);
    setColour(juce::TooltipWindow::backgroundColourId, p.bulk2);
    setColour(juce::TooltipWindow::textColourId, p.ink);
    setColour(juce::TooltipWindow::outlineColourId, p.seamHi);
    setColour(juce::TextButton::buttonColourId, p.bulk1);
    setColour(juce::TextButton::textColourOffId, p.inkDim);
    setColour(juce::TextButton::textColourOnId, p.accent);
    setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff9aa3ae));   // muted: the hero owns the brightness
    setColour(juce::MidiKeyboardComponent::blackNoteColourId, p.bulk1);
    setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, p.bulk0.withAlpha(0.6f));
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, p.accent.withAlpha(0.18f));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, p.accent.withAlpha(0.65f));
    setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
    setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, p.bulk1);
    setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, p.inkDim);
    setColour(juce::MidiKeyboardComponent::textLabelColourId, p.bulk0.withAlpha(0.85f));
}

juce::Typeface::Ptr MorphoLnF::getTypefaceForFont(const juce::Font& f) {
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) if (auto tf = sans_typeface()) return tf;
    return juce::LookAndFeel_V4::getTypefaceForFont(f);
}

void MorphoLnF::drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) {
    const auto& p = pal();
    auto r = b.getLocalBounds().toFloat().reduced(0.5f);
    const auto path = hex_pill(r, !b.isConnectedOnLeft(), !b.isConnectedOnRight());
    const bool on = b.getToggleState();
    g.setColour(on ? p.accent.withAlpha(0.14f) : (down ? p.bulk2.brighter(0.08f) : (over ? p.bulk2 : p.bulk1)));
    g.fillPath(path);
    g.setColour(on ? p.accent : (over ? p.seamHi : p.seam));
    g.strokePath(path, juce::PathStrokeType(on ? 1.2f : 1.f));
}

juce::Font MorphoLnF::getTextButtonFont(juce::TextButton&, int h) { return sans(juce::jmin(12.f, h * 0.5f), true).withExtraKerningFactor(0.06f); }

void MorphoLnF::drawButtonText(juce::Graphics& g, juce::TextButton& b, bool over, bool) {
    const auto& p = pal();
    g.setFont(getTextButtonFont(b, b.getHeight()));
    g.setColour(!b.isEnabled() ? p.inkFaint : (b.getToggleState() ? p.accent : (over ? p.ink : p.inkDim)));
    g.drawText(b.getButtonText().toUpperCase(), b.getLocalBounds().reduced(4, 0), juce::Justification::centred, false);
}

void MorphoLnF::drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) {
    const auto& p = pal();
    auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(0.5f);
    const auto path = hex_pill(r, false, false);
    const bool over = box.isMouseOver(true);
    g.setColour(over ? p.bulk2 : p.bulk1); g.fillPath(path);
    g.setColour(box.hasKeyboardFocus(true) ? p.accent : (over ? p.seamHi : p.seam)); g.strokePath(path, juce::PathStrokeType(1.f));
    // caret: a small 60-degree chevron
    const float cx = (float)w - 12.f, cy = (float)h * 0.5f;
    juce::Path c; c.startNewSubPath(cx - 4.f, cy - 2.f); c.lineTo(cx, cy + 2.5f); c.lineTo(cx + 4.f, cy - 2.f);
    g.setColour(over ? p.ink : p.inkDim); g.strokePath(c, juce::PathStrokeType(1.3f));
}
juce::Font MorphoLnF::getComboBoxFont(juce::ComboBox&) { return sans(14.f, true); }
void MorphoLnF::positionComboBoxText(juce::ComboBox& box, juce::Label& l) {
    l.setBounds(10, 0, box.getWidth() - 30, box.getHeight());
    l.setFont(getComboBoxFont(box));
    l.setJustificationType(juce::Justification::centredLeft);
}

void MorphoLnF::drawPopupMenuBackground(juce::Graphics& g, int w, int h) {
    const auto& p = pal();
    g.fillAll(p.bulk1);
    g.setColour(p.seamHi); g.drawRect(0, 0, w, h, 1);
}
juce::Font MorphoLnF::getPopupMenuFont() { return sans(14.f); }
void MorphoLnF::drawPopupMenuSectionHeader(juce::Graphics& g, const juce::Rectangle<int>& area, const juce::String& t) {
    draw_caption(g, t, area.toFloat().reduced(12.f, 0).withTrimmedTop(4.f), juce::Justification::centredLeft, pal().inkDim, 10.5f, 0.12f);
}
void MorphoLnF::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area, bool sep, bool active, bool hi, bool ticked,
                                  bool hasSub, const juce::String& text, const juce::String& sc, const juce::Drawable* icon,
                                  const juce::Colour* tc) {
    const auto& p = pal();
    if (sep) { g.setColour(p.seam); g.fillRect(area.reduced(8, 0).withHeight(1).withY(area.getCentreY())); return; }
    auto r = area.reduced(2, 0);
    if (hi && active) { g.setColour(p.accent.withAlpha(0.14f)); g.fillRect(r); }
    if (ticked) { g.setColour(p.accent); g.fillPath(hexagon({(float)r.getX() + 10.f, (float)r.getCentreY()}, 3.5f)); }
    g.setColour(!active ? p.inkFaint : (tc ? *tc : (ticked ? p.accent : p.ink)));
    g.setFont(getPopupMenuFont());
    g.drawText(text, r.withTrimmedLeft(22).withTrimmedRight(8), juce::Justification::centredLeft, true);
    juce::ignoreUnused(hasSub, sc, icon);
}

void MorphoLnF::drawTooltip(juce::Graphics& g, const juce::String& text, int w, int h) {
    const auto& p = pal();
    g.fillAll(p.bulk2);
    g.setColour(p.seamHi); g.drawRect(0, 0, w, h, 1);
    juce::AttributedString s; s.setJustification(juce::Justification::centredLeft);
    s.append(text, sans(13.f), p.ink);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths(s, (float)w - 16.f);
    tl.draw(g, juce::Rectangle<float>(8.f, 6.f, (float)w - 16.f, (float)h - 12.f));
}
juce::Rectangle<int> MorphoLnF::getTooltipBounds(const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) {
    juce::AttributedString s; s.append(text, sans(13.f), pal().ink);
    juce::TextLayout tl; tl.createLayoutWithBalancedLineLengths(s, 320.f);
    const int w = (int)std::ceil(tl.getWidth()) + 18, h = (int)std::ceil(tl.getHeight()) + 14;
    return juce::Rectangle<int>(pos.x > parent.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                pos.y > parent.getCentreY() ? pos.y - (h + 8) : pos.y + 18, w, h).constrainedWithin(parent);
}

// ================================================================================================ formatting
juce::String format_param(int i, float v) {
    if (i < 0 || i >= kNumParams) return juce::String(v, 2);
    const auto& d = kParams[(size_t)i];
    const juce::String unit(d.unit);
    if (d.kind == PKind::Choice) return juce::String(v, 0);
    if (unit == "Hz") return v >= 1000.f ? juce::String(v / 1000.f, 2) + " kHz" : juce::String(juce::roundToInt(v)) + " Hz";
    if (unit == "s") return v < 1.f ? juce::String(juce::roundToInt(v * 1000.f)) + " ms" : juce::String(v, 2) + " s";
    if (unit == "dB") return (v >= 0.f ? "+" : "") + juce::String(v, 1) + " dB";
    if (d.min < 0.f) return (v >= 0.f ? "+" : "") + juce::String(v, 2);
    if (d.min == 0.f && d.max == 1.f) return juce::String(juce::roundToInt(v * 100.f)) + "%";
    return juce::String(v, 2);
}

// ================================================================================================ ParamKnob
ParamKnob::ParamKnob(juce::RangedAudioParameter& p, int idx, const juce::String& caption, Kind kind, bool bipolar)
    : p_(p), index_(idx), caption_(caption), kind_(kind), bipolar_(bipolar) {
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownLeftRightResizeCursor);
    att_ = std::make_unique<juce::ParameterAttachment>(p_, [this](float plain) {
        const float v = p_.convertTo0to1(plain);
        if (std::abs(v - v01_) > 1e-6f) { v01_ = v; repaint(); }
    });
    att_->sendInitialUpdate();
    setTitle(caption);
}
ParamKnob::~ParamKnob() = default;

juce::Rectangle<float> ParamKnob::dial_bounds() const {
    const float lab = kind_ == Kind::Small ? 26.f : 30.f;
    auto r = getLocalBounds().toFloat();
    const float d = std::min(r.getWidth() - 8.f, r.getHeight() - lab - 4.f);
    return juce::Rectangle<float>(d, d).withCentre({r.getCentreX(), r.getY() + 2.f + d * 0.5f});
}

void ParamKnob::set_normalised(float v, bool step) {
    v = juce::jlimit(0.f, 1.f, v);
    const float plain = p_.convertFrom0to1(v);
    if (step) att_->setValueAsPartOfGesture(plain); else att_->setValueAsCompleteGesture(plain);
}

void ParamKnob::paint(juce::Graphics& g) {
    const auto& P = pal();
    if (kind_ == Kind::Bar) {
        if (dimmed_) g.beginTransparencyLayer(0.5f);   // horizontal fader: caption | hex-pill track | mono readout
        const bool live = hover_ || dragging_;
        auto r = getLocalBounds().toFloat();
        draw_caption(g, caption_, r.removeFromLeft(84.f), juce::Justification::centredLeft, live ? P.ink : P.inkDim, 10.5f, 0.07f);
        g.setFont(mono(12.f, dragging_)); g.setColour(dragging_ ? P.accent : P.ink.withAlpha(0.92f));
        g.drawText(override_.isNotEmpty() ? override_ : format_param(index_, p_.convertFrom0to1(v01_)), r.removeFromRight(54.f), juce::Justification::centredRight, false);
        const auto tr = r.reduced(6.f, 0.f).withSizeKeepingCentre(r.getWidth() - 12.f, 10.f);
        const auto track = hex_pill(tr);
        g.setColour(P.bulk2); g.fillPath(track);
        g.setColour(live ? P.seamHi : P.seam); g.strokePath(track, juce::PathStrokeType(1.f));
        const float x = tr.getX() + tr.getWidth() * v01_;
        g.saveState(); g.reduceClipRegion(tr.withRight(x).toNearestInt().expanded(0, 2)); 
        g.setColour(P.ink.withAlpha(live ? 0.75f : 0.55f)); g.fillPath(hex_pill(tr.reduced(2.f, 2.5f)));
        g.restoreState();
        g.setColour(P.ink); g.fillRect(juce::Rectangle<float>(2.f, tr.getHeight() + 6.f).withCentre({x, tr.getCentreY()}));
        if (hasKeyboardFocus(false)) { g.setColour(P.accent.withAlpha(0.5f)); g.strokePath(track, juce::PathStrokeType(1.f)); }
        if (dimmed_) g.endTransparencyLayer();
        return;
    }
    const auto db = dial_bounds();
    const auto c = db.getCentre();
    const float R = db.getWidth() * 0.5f;
    const bool live = hover_ || dragging_;
    const bool macro = kind_ == Kind::Macro;
    if (dimmed_) g.beginTransparencyLayer(0.42f);
    const float arcR = R - 2.f, arcW = macro ? 3.2f : (kind_ == Kind::Main ? 2.6f : 2.2f);
    const float a = kA0 + (kA1 - kA0) * v01_, aRef = bipolar_ ? 0.f : kA0;
    // track + end ticks
    juce::Path track; track.addCentredArc(c.x, c.y, arcR, arcR, 0.f, kA0, kA1, true);
    g.setColour(P.seam); g.strokePath(track, juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.setColour(P.inkFaint);
    for (float t : {kA0, kA1}) g.drawLine(juce::Line<float>(polar(c, R + 1.5f, t), polar(c, R - 4.5f - arcW, t)), 1.f);
    if (bipolar_) g.drawLine(juce::Line<float>(polar(c, R + 1.5f, 0.f), polar(c, R - 4.5f - arcW, 0.f)), 1.f);
    // value arc
    const auto arcCol = macro ? P.accent : (live ? P.ink : P.ink.withAlpha(0.86f));
    if (std::abs(a - aRef) > 1e-4f) {
        juce::Path v; v.addCentredArc(c.x, c.y, arcR, arcR, 0.f, std::min(a, aRef), std::max(a, aRef), true);
        g.setColour(arcCol); g.strokePath(v, juce::PathStrokeType(arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    }
    // well
    const float wr = R * 0.60f;
    g.setColour(P.bulk2); g.fillEllipse(juce::Rectangle<float>(2 * wr, 2 * wr).withCentre(c));
    g.setColour(live ? P.seamHi : P.seam); g.drawEllipse(juce::Rectangle<float>(2 * wr, 2 * wr).withCentre(c), 1.f);
    // gyro site: pointer bond + two trailing bonds at +-120 degrees
    const float arm = wr * 0.86f;
    g.setColour(P.inkDim.withAlpha(0.55f));
    for (float off : {2.0943951f, -2.0943951f}) g.drawLine(juce::Line<float>(c, polar(c, arm * 0.48f, a + off)), 1.4f);
    g.setColour(macro ? P.accent : P.ink);
    g.drawLine(juce::Line<float>(c, polar(c, arm, a)), macro ? 2.2f : 1.9f);
    g.fillEllipse(juce::Rectangle<float>(4.4f, 4.4f).withCentre(c));
    // tip: a small honeycomb site on the arc
    g.setColour(arcCol); g.fillPath(hexagon(polar(c, arcR, a), arcW * 0.5f + 2.2f));
    if (dimmed_) g.endTransparencyLayer();
    // caption + readout (kept legible when unassigned: caption at inkDim, value replaced by a dash)
    const float capH = kind_ == Kind::Small ? 10.f : 10.5f, roH = kind_ == Kind::Small ? 11.f : 12.f;
    auto tr = getLocalBounds().toFloat().withTop(db.getBottom() + 3.f);
    draw_caption(g, caption_, tr.removeFromTop(capH + 3.f), juce::Justification::centredTop, live ? P.ink : P.inkDim, capH, 0.07f);
    g.setFont(mono(roH, dragging_));
    g.setColour(dragging_ ? P.accent : (dimmed_ ? P.inkDim : P.ink.withAlpha(0.92f)));
    g.drawText(override_.isNotEmpty() ? override_ : (dimmed_ && !dragging_ ? juce::String(juce::CharPointer_UTF8("\xe2\x80\x94")) : format_param(index_, p_.convertFrom0to1(v01_))),
               tr.removeFromTop(roH + 3.f), juce::Justification::centredTop, false);
    if (hasKeyboardFocus(false)) { g.setColour(P.accent.withAlpha(0.5f)); g.drawEllipse(db.reduced(-1.f), 1.f); }
}

void ParamKnob::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) return;
    if (e.mods.isAltDown() || e.mods.isCommandDown()) { set_normalised(p_.getDefaultValue(), false); return; }
    dragging_ = true; last_ = e.position;
    att_->beginGesture();
    e.source.enableUnboundedMouseMovement(true, false);
    repaint();
}
void ParamKnob::mouseDrag(const juce::MouseEvent& e) {
    if (!dragging_) return;
    const auto d = e.position - last_; last_ = e.position;
    const float px = (d.x - d.y);                                   // right / up = increase
    const float full = e.mods.isShiftDown() ? 1100.f : 220.f;       // design pixels for the full range (Shift = fine x5)
    const float nv = juce::jlimit(0.f, 1.f, v01_ + px / full);
    if (nv != v01_) { v01_ = nv; att_->setValueAsPartOfGesture(p_.convertFrom0to1(nv)); repaint(); }
}
void ParamKnob::mouseUp(const juce::MouseEvent& e) {
    if (!dragging_) return;
    dragging_ = false;
    e.source.enableUnboundedMouseMovement(false, true);
    att_->endGesture();
    repaint();
}
void ParamKnob::mouseDoubleClick(const juce::MouseEvent&) { set_normalised(p_.getDefaultValue(), false); }
void ParamKnob::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) {
    const float dv = (std::abs(w.deltaY) > std::abs(w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.f : 1.f);
    if (dv == 0.f) return;
    set_normalised(v01_ + dv * (e.mods.isShiftDown() ? 0.02f : 0.1f), false);
}
bool ParamKnob::keyPressed(const juce::KeyPress& k) {
    const float step = k.getModifiers().isShiftDown() ? 0.002f : 0.01f;
    if (k.isKeyCode(juce::KeyPress::upKey) || k.isKeyCode(juce::KeyPress::rightKey)) { set_normalised(v01_ + step, false); return true; }
    if (k.isKeyCode(juce::KeyPress::downKey) || k.isKeyCode(juce::KeyPress::leftKey)) { set_normalised(v01_ - step, false); return true; }
    if (k.isKeyCode(juce::KeyPress::homeKey) || k.isKeyCode(juce::KeyPress::deleteKey)) { set_normalised(p_.getDefaultValue(), false); return true; }
    return false;
}

namespace {
class KnobValue : public juce::AccessibilityRangedNumericValueInterface {
public:
    explicit KnobValue(ParamKnob& k) : k_(k) {}
    bool isReadOnly() const override { return false; }
    double getCurrentValue() const override { return k_.param().convertFrom0to1(k_.value01()); }
    void setValue(double v) override { k_.set_normalised(k_.param().convertTo0to1((float)v), false); }
    AccessibleValueRange getRange() const override {
        const auto& r = k_.param().getNormalisableRange();
        return AccessibleValueRange({(double)r.start, (double)r.end}, (double)std::max(r.interval, (r.end - r.start) / 100.f));
    }
private:
    ParamKnob& k_;
};
}  // namespace

std::unique_ptr<juce::AccessibilityHandler> ParamKnob::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                        juce::AccessibilityHandler::Interfaces{std::make_unique<KnobValue>(*this)});
}

// ================================================================================================ ChoiceSeg
ChoiceSeg::ChoiceSeg(juce::RangedAudioParameter& p, const juce::StringArray& labels, const juce::String& caption) : p_(p), caption_(caption) {
    static int group = 7300;
    const int gid = ++group;
    for (int i = 0; i < labels.size(); ++i) {
        auto* b = buttons_.add(new juce::TextButton(labels[i]));
        b->setRadioGroupId(gid, juce::dontSendNotification);
        b->setClickingTogglesState(false);
        int flags = 0;
        if (i > 0) flags |= juce::Button::ConnectedOnLeft;
        if (i + 1 < labels.size()) flags |= juce::Button::ConnectedOnRight;
        b->setConnectedEdges(flags);
        b->onClick = [this, i] { if (att_) att_->setValueAsCompleteGesture((float)i); };
        b->setTitle(caption + ": " + labels[i]);
        addAndMakeVisible(b);
    }
    att_ = std::make_unique<juce::ParameterAttachment>(p_, [this](float plain) {
        current_ = juce::roundToInt(plain);
        if (current_ < 0 || current_ >= buttons_.size()) current_ = 0;   // FX mode: preset value 'Instrument' runs as Edge Disperser
        for (int i = 0; i < buttons_.size(); ++i) buttons_[i]->setToggleState(i == current_, juce::dontSendNotification);
    });
    att_->sendInitialUpdate();
}
void ChoiceSeg::paint(juce::Graphics& g) {
    if (caption_height > 0)
        draw_caption(g, caption_, getLocalBounds().toFloat().removeFromTop((float)caption_height), juce::Justification::centredLeft,
                     warn_ ? pal().warn : pal().inkDim, 10.5f, 0.08f);
}
void ChoiceSeg::resized() {
    auto r = getLocalBounds(); r.removeFromTop(caption_height + (caption_height > 0 ? 4 : 0));
    const int n = buttons_.size(); if (n == 0) return;
    const float w = (float)r.getWidth() / (float)n;
    for (int i = 0; i < n; ++i)
        buttons_[i]->setBounds(r.getX() + juce::roundToInt(w * (float)i), r.getY(), juce::roundToInt(w * float(i + 1)) - juce::roundToInt(w * (float)i) + (i + 1 < n ? -1 : 0), r.getHeight());
}

// ================================================================================================ GlyphButton
GlyphButton::GlyphButton(const juce::String& label, Icon icon) : juce::Button(label), icon_(icon) { setTitle(label); }

void GlyphButton::draw_icon(juce::Graphics& g, Icon ic, juce::Rectangle<float> r, juce::Colour col, bool flipped) {
    const auto c = r.getCentre();
    const float s = std::min(r.getWidth(), r.getHeight()) * 0.5f;
    const juce::PathStrokeType st(1.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
    g.setColour(col);
    juce::Path p;
    switch (ic) {
        case Icon::Pluck:
            g.fillPath(hexagon(c, s * 0.34f));
            for (int k = 0; k < 3; ++k) { const float a = 2.0943951f * (float)k; p.startNewSubPath(polar(c, s * 0.58f, a)); p.lineTo(polar(c, s * 0.98f, a)); }
            g.strokePath(p, st); break;
        case Icon::Morph:
            g.fillEllipse(juce::Rectangle<float>(s * 0.5f, s * 0.5f).withCentre(c));
            p.addCentredArc(c.x, c.y, s * 0.62f, s * 0.62f, 0.f, -2.2f, 0.9f, true);
            p.addCentredArc(c.x, c.y, s * 0.95f, s * 0.95f, 0.f, -1.9f, 0.6f, true);
            g.strokePath(p, juce::PathStrokeType(1.4f)); break;
        case Icon::Chisel:
            g.strokePath(hexagon(c, s * 0.95f), st); g.fillPath(hexagon(c, s * 0.42f)); break;
        case Icon::Erase: {
            juce::Path h = hexagon(c, s * 0.95f), d; const float dash[] = {2.2f, 2.0f};
            juce::PathStrokeType(1.4f).createDashedStroke(d, h, dash, 2); g.fillPath(d); break; }
        case Icon::Spiral:
            for (int i = 0; i <= 48; ++i) {
                const float t = (float)i / 48.f, a = t * 11.f, rr = s * (0.08f + 0.9f * t);
                const juce::Point<float> q(c.x + rr * std::cos(a), c.y + rr * std::sin(a));
                if (i == 0) p.startNewSubPath(q); else p.lineTo(q);
            }
            g.strokePath(p, juce::PathStrokeType(1.4f)); break;
        case Icon::Clear:
            g.strokePath(hexagon(c, s * 0.95f), st);
            p.startNewSubPath(c.x - s * 0.36f, c.y - s * 0.36f); p.lineTo(c.x + s * 0.36f, c.y + s * 0.36f);
            p.startNewSubPath(c.x + s * 0.36f, c.y - s * 0.36f); p.lineTo(c.x - s * 0.36f, c.y + s * 0.36f);
            g.strokePath(p, st); break;
        case Icon::Reset: {
            p.addCentredArc(c.x, c.y, s * 0.78f, s * 0.78f, 0.f, 0.6f, 5.6f, true);
            g.strokePath(p, juce::PathStrokeType(1.4f));
            const auto e = polar(c, s * 0.78f, 0.6f);
            juce::Path h; h.addTriangle(e.x - s * 0.34f, e.y - s * 0.12f, e.x + s * 0.12f, e.y - s * 0.40f, e.x + s * 0.10f, e.y + s * 0.16f);
            g.fillPath(h); break; }
        case Icon::Info:
            g.drawEllipse(juce::Rectangle<float>(2 * s * 0.92f, 2 * s * 0.92f).withCentre(c), 1.3f);
            g.fillRect(juce::Rectangle<float>(1.6f, s * 0.75f).withCentre({c.x, c.y + s * 0.18f}));
            g.fillEllipse(juce::Rectangle<float>(2.2f, 2.2f).withCentre({c.x, c.y - s * 0.42f})); break;
        case Icon::Eco:   // a low, slow wave: low-motion visuals
            for (int i = 0; i <= 24; ++i) {
                const float t = (float)i / 24.f; const juce::Point<float> q(c.x - s + 2 * s * t, c.y + s * 0.28f * std::sin(t * 6.2831853f));
                if (i == 0) p.startNewSubPath(q); else p.lineTo(q);
            }
            g.strokePath(p, juce::PathStrokeType(1.4f)); break;
        case Icon::Prev: case Icon::Next: {
            const float d = ic == Icon::Prev ? -1.f : 1.f;
            p.startNewSubPath(c.x - d * s * 0.3f, c.y - s * 0.55f); p.lineTo(c.x + d * s * 0.3f, c.y); p.lineTo(c.x - d * s * 0.3f, c.y + s * 0.55f);
            g.strokePath(p, st); break; }
        case Icon::Drawer: {
            const float d = flipped ? 1.f : -1.f;
            p.startNewSubPath(c.x - s * 0.55f, c.y - d * s * 0.25f); p.lineTo(c.x, c.y + d * s * 0.3f); p.lineTo(c.x + s * 0.55f, c.y - d * s * 0.25f);
            g.strokePath(p, st); break; }
    }
}

void GlyphButton::paintButton(juce::Graphics& g, bool over, bool down) {
    const auto& P = pal();
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const bool on = getToggleState();
    const auto path = hex_pill(r);
    g.setColour(on ? P.accent.withAlpha(0.14f) : (down ? P.bulk2.brighter(0.08f) : (over ? P.bulk2 : P.bulk1)));
    g.fillPath(path);
    g.setColour(on ? P.accent : (over ? P.seamHi : P.seam));
    g.strokePath(path, juce::PathStrokeType(on ? 1.2f : 1.f));
    const auto col = !isEnabled() ? P.inkFaint : (on ? P.accent : (over ? P.ink : P.inkDim));
    const float ih = std::min(14.f, r.getHeight() - 10.f);
    if (show_label && getButtonText().isNotEmpty()) {
        auto f = sans(11.f, true).withExtraKerningFactor(0.07f);
        const auto txt = getButtonText().toUpperCase();
        const float tw = juce::GlyphArrangement::getStringWidth(f, txt);
        const float total = ih + 6.f + tw, x0 = r.getCentreX() - total * 0.5f;
        draw_icon(g, icon_, {x0, r.getCentreY() - ih * 0.5f, ih, ih}, col, flipped);
        g.setFont(f); g.setColour(col);
        g.drawText(txt, juce::Rectangle<float>(x0 + ih + 6.f, r.getY(), tw + 2.f, r.getHeight()), juce::Justification::centredLeft, false);
    } else {
        draw_icon(g, icon_, juce::Rectangle<float>(ih, ih).withCentre(r.getCentre()), col, flipped);
    }
}

}  // namespace morpho_plugin
