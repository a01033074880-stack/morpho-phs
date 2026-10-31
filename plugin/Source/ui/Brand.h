// SPDX-License-Identifier: AGPL-3.0-only
// Phase 6a brand marks drawn in code from the generated geometry (scripts/brand/gen_brand.py -> BrandGeometry.h).
#pragma once
#include "BrandGeometry.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace morpho_plugin::brand {

// glyph centred at c, circumradius r (instrument: counter-clockwise edge arrow; FX: its mirror image)
inline void draw_glyph(juce::Graphics& g, juce::Point<float> c, float r, juce::Colour accent, juce::Colour ink) {
    juce::Path edge, bonds; float nr = 0.17f;
    if (kIsFxVariant) glyph_fx(edge, bonds, nr); else glyph_inst(edge, bonds, nr);
    const auto t = juce::AffineTransform::scale(r).translated(c);
    edge.applyTransform(t); bonds.applyTransform(t);
    g.setColour(accent); g.strokePath(edge, juce::PathStrokeType(0.13f * r, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    g.setColour(ink); g.strokePath(bonds, juce::PathStrokeType(0.12f * r, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
    g.fillEllipse(juce::Rectangle<float>(2 * nr * r, 2 * nr * r).withCentre(c));
}

// wordmark with its top-left at (x, y) and the given cap height in pixels; returns the drawn width
inline float draw_wordmark(juce::Graphics& g, float x, float y, float cap, juce::Colour ink, juce::Colour accent) {
    const float s = cap / kCapHeight;
    juce::Path st, dots;
    float w = wordmark(st, dots);
    const auto t = juce::AffineTransform::scale(s).translated(x, y);
    const juce::PathStrokeType stroke(kStroke * s, juce::PathStrokeType::mitered, juce::PathStrokeType::butt);
    st.applyTransform(t); dots.applyTransform(t);
    g.setColour(ink); g.strokePath(st, stroke); g.fillPath(dots);
    if (kIsFxVariant) {
        juce::Path fs, fd;
        const float fw = wordmark_fx_suffix(fs, fd);
        const auto tf = juce::AffineTransform::translation(w + kWordGap, 0.f).scaled(s).translated(x, y);
        fs.applyTransform(tf); fd.applyTransform(tf);
        g.setColour(accent); g.strokePath(fs, stroke); g.fillPath(fd);
        w += kWordGap + fw;
    }
    return w * s;
}

}  // namespace morpho_plugin::brand
