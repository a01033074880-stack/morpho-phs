// SPDX-License-Identifier: AGPL-3.0-only
// MORPHO-PHS Phase 6a visual system "BULK / EDGE" (docs/phase6/art_direction.md).
//   * the bulk is dark and insulating: matte graphite, no gradients, no glow
//   * the edge conducts: interactive things are delineated by hairline edges; the accent marks only active edges
//   * energy is light: the heat ramp (port-Hamiltonian energy H) is the only luminous colour family
//   * excitable = texture: the Barkley medium is drawn as hatching, never as an extra hue
//   * geometry from 60 degrees: hex-pill buttons (60-degree chamfers), the honeycomb site's three bonds as pointers
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace morpho_plugin::theme {

// --- design canvas (all layout in these units; the editor scales the whole canvas with a transform) ---
inline constexpr int kDesignW = 1152, kDesignH = 720;     // 16:10, 8-px grid
inline constexpr int kMinW = 960, kMinH = 600, kMaxW = 1728, kMaxH = 1080;
inline constexpr int kGrid = 8;

struct Palette {
    juce::Colour bulk0, bulk1, bulk2, seam, seamHi, ink, inkDim, inkFaint, accent, accentDim, warn;
    std::array<juce::Colour, 5> heat;   // H0 (cold) .. H4 (white-hot)
};

// variant-aware palette: the instrument's accent is A-site cyan, the FX's is B-site lime
const Palette& pal() noexcept;

// heat ramp, t in [0, 1] (256-entry LUT, premultiplied-safe opaque colours)
juce::PixelARGB heat_px(float t) noexcept;
inline juce::Colour heat(float t) noexcept { const auto p = heat_px(t); return juce::Colour(p.getRed(), p.getGreen(), p.getBlue()); }

// embedded OFL fonts (IBM Plex Sans Condensed Medium / SemiBold, IBM Plex Mono Regular / Medium)
juce::Font sans(float height, bool semibold = false);
juce::Font mono(float height, bool medium = false);
juce::Typeface::Ptr sans_typeface();

// caption text: upper case with tracking
void draw_caption(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Justification j,
                  juce::Colour c, float height = 10.5f, float tracking = 0.08f);

// hex pill: a rectangle whose short ends are 60-degree points (chamfer = h / (2 tan 60) * k)
juce::Path hex_pill(juce::Rectangle<float> r, bool point_left = true, bool point_right = true);
// small pointy-top hexagon centred at c with circumradius rr
juce::Path hexagon(juce::Point<float> c, float rr, float rotation = 0.f);

}  // namespace morpho_plugin::theme
