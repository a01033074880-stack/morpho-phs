// SPDX-License-Identifier: AGPL-3.0-only
#include "Theme.h"
#include "../PluginProcessor.h"
#include "BinaryData_UiAssets.h"
#include <cmath>

namespace morpho_plugin::theme {

const Palette& pal() noexcept {
    static const Palette p = [] {
        Palette q;
        q.bulk0 = juce::Colour(0xff0b0d12); q.bulk1 = juce::Colour(0xff11141b); q.bulk2 = juce::Colour(0xff181c25);
        q.seam = juce::Colour(0xff262b37); q.seamHi = juce::Colour(0xff363d4d);
        q.ink = juce::Colour(0xffe9e6df); q.inkDim = juce::Colour(0xff9aa1b2); q.inkFaint = juce::Colour(0xff6b7385);
        q.accent = kIsFxVariant ? juce::Colour(0xffc8f05a) : juce::Colour(0xff59d8f0);
        q.accentDim = q.accent.withAlpha(0.22f);
        q.warn = juce::Colour(0xfff07a3a);   // = heat H3: CPU heat uses the energy ramp, not a new hue
        q.heat = {juce::Colour(0xff141826), juce::Colour(0xff3b2363), juce::Colour(0xffa3305a), juce::Colour(0xfff07a3a), juce::Colour(0xffffe7a8)};
        return q;
    }();
    return p;
}

juce::PixelARGB heat_px(float t) noexcept {
    static const std::array<juce::PixelARGB, 256> lut = [] {
        std::array<juce::PixelARGB, 256> a{};
        const auto& h = pal().heat;
        for (int i = 0; i < 256; ++i) {
            const float x = float(i) / 255.f * 4.f;
            const int k = std::min(3, int(x));
            const auto c = h[(size_t)k].interpolatedWith(h[(size_t)k + 1], x - float(k));
            a[(size_t)i] = juce::PixelARGB(255, c.getRed(), c.getGreen(), c.getBlue());
        }
        return a;
    }();
    const int i = juce::jlimit(0, 255, int(t * 255.f + 0.5f));
    return lut[(size_t)i];
}

namespace {
juce::Typeface::Ptr load(const char* data, int size) { return juce::Typeface::createSystemTypefaceFor(data, (size_t)size); }
struct Faces {
    juce::Typeface::Ptr sansMed, sansSemi, monoReg, monoMed;
    Faces() {
        sansMed = load(UiAssets::IBMPlexSansCondensedMedium_ttf, UiAssets::IBMPlexSansCondensedMedium_ttfSize);
        sansSemi = load(UiAssets::IBMPlexSansCondensedSemiBold_ttf, UiAssets::IBMPlexSansCondensedSemiBold_ttfSize);
        monoReg = load(UiAssets::IBMPlexMonoRegular_ttf, UiAssets::IBMPlexMonoRegular_ttfSize);
        monoMed = load(UiAssets::IBMPlexMonoMedium_ttf, UiAssets::IBMPlexMonoMedium_ttfSize);
    }
};
const Faces& faces() { static const Faces f; return f; }
juce::Font make(juce::Typeface::Ptr tf, float h) {
    if (tf == nullptr) return juce::Font(juce::FontOptions(h));
    return juce::Font(juce::FontOptions(tf).withHeight(h));
}
}  // namespace

juce::Font sans(float height, bool semibold) { return make(semibold ? faces().sansSemi : faces().sansMed, height); }
juce::Font mono(float height, bool medium) { return make(medium ? faces().monoMed : faces().monoReg, height); }
juce::Typeface::Ptr sans_typeface() { return faces().sansMed; }

void draw_caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j,
                  juce::Colour c, float height, float tracking) {
    auto f = sans(height, true).withExtraKerningFactor(tracking);
    g.setFont(f); g.setColour(c);
    g.drawText(text.toUpperCase(), r, j, false);
}

juce::Path hex_pill(juce::Rectangle<float> r, bool pl, bool pr) {
    const float h = r.getHeight(), c = std::min(r.getWidth() * 0.25f, 0.5f * h / std::tan(juce::degreesToRadians(60.f)) * 1.15f);
    juce::Path p;
    const float x0 = r.getX(), x1 = r.getRight(), y0 = r.getY(), y1 = r.getBottom(), ym = r.getCentreY();
    p.startNewSubPath(x0 + (pl ? c : 0.f), y0);
    p.lineTo(x1 - (pr ? c : 0.f), y0);
    if (pr) p.lineTo(x1, ym);
    p.lineTo(x1 - (pr ? c : 0.f), y1);
    p.lineTo(x0 + (pl ? c : 0.f), y1);
    if (pl) p.lineTo(x0, ym);
    p.closeSubPath();
    return p;
}

juce::Path hexagon(juce::Point<float> c, float rr, float rotation) {
    juce::Path p;
    for (int k = 0; k < 6; ++k) {
        const float a = rotation + juce::MathConstants<float>::pi * (0.5f + float(k) / 3.f);
        const juce::Point<float> v(c.x + rr * std::cos(a), c.y - rr * std::sin(a));
        if (k == 0) p.startNewSubPath(v); else p.lineTo(v);
    }
    p.closeSubPath();
    return p;
}

}  // namespace morpho_plugin::theme
