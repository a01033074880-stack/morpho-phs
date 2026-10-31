// SPDX-License-Identifier: AGPL-3.0-only
// Phase 6b preset browser ("BULK / EDGE", docs/phase6/art_direction.md): the header PresetChip (category, name,
// favourite hexagram, CPU class pips) opens a PresetBrowser overlay over the hero + right column.  Bank / Favourites /
// category column, tag chips, the preset list and a detail card (description, 8 macro names, measured CPU class).
// Single click auditions, double click / Enter loads and closes, Esc closes, F toggles the favourite, arrows step.
// Everything is code-drawn vector, message thread only.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "Theme.h"

namespace morpho_plugin {

// six-pointed star (two 60-degree triangles): the favourite mark
juce::Path hexagram(juce::Point<float> c, float r);
// eco / standard / high as 1-3 filled hexagon pips (high in the warn / heat colour)
void draw_cpu_pips(juce::Graphics& g, juce::Point<float> left_centre, const juce::String& cpu_class, float r = 3.2f);

class PresetChip : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit PresetChip(MorphoProcessor& p) : proc_(p) { setTitle("Preset browser"); setWantsKeyboardFocus(true); }
    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override { hover_ = true; repaint(); }
    void mouseExit(const juce::MouseEvent&) override { hover_ = false; repaint(); }
    bool keyPressed(const juce::KeyPress&) override;
    std::function<void()> on_open, on_favourite;
    bool open = false;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
private:
    juce::Rectangle<float> star_area() const { return getLocalBounds().toFloat().removeFromRight(30.f); }
    MorphoProcessor& proc_;
    bool hover_ = false;
};

class PresetBrowser : public juce::Component {
public:
    explicit PresetBrowser(MorphoProcessor& p);
    void rebuild();                 // after the factory list / favourites changed
    void sync_to_current();         // select the row of the current preset
    std::function<void()> on_loaded, on_close;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover_row_ = -1; hover_hit_ = {}; repaint(); }
    bool keyPressed(const juce::KeyPress&) override;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

    // filter state (also used by the UI harness)
    void set_filter(const juce::String& category_or_special, const juce::String& tag);
    static constexpr const char* kAll = "All", *kFav = "Favourites";

private:
    struct Hit { int kind = 0; int index = -1; juce::String text; };   // 1 cat, 2 tag, 3 row, 4 star, 5 load, 6 init, 7 close
    void refilter();
    void layout();
    Hit hit_test(juce::Point<float> p) const;
    void audition(int row);
    void ensure_visible();

    MorphoProcessor& proc_;
    juce::StringArray cats_;                 // All, Favourites, categories (bank order)
    juce::Array<int> cat_counts_;
    juce::StringArray tags_;                 // tags present under the current category filter (most frequent first, <= 14)
    juce::Array<int> rows_;                  // factory indices passing the filter
    juce::String cat_ = kAll, tag_;
    int sel_ = -1, scroll_ = 0, hover_row_ = -1;
    Hit hover_hit_;
    // layout (local coords)
    juce::Rectangle<float> card_, catCol_, tagRow_, list_, detail_, loadBtn_, initBtn_, closeBtn_;
    std::vector<std::pair<juce::Rectangle<float>, juce::String>> tagRects_;
    static constexpr float kRowH = 26.f;
};

}  // namespace morpho_plugin
