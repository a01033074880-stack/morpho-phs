// SPDX-License-Identifier: AGPL-3.0-only
// Phase 6a hero visualiser: the live honeycomb lattice.
//   energy  -> heat-ramp light per site facet (log, auto-ranged)      displacement -> facet tilt shading + gyro needles
//   Barkley -> hatch texture (not a colour)                          edge channel -> accent outline, chirality arrows, energy dashes
// Performance contract (docs/phase6/art_direction.md section 7):
//   * the audio thread publishes snapshots only while this view is showing (MorphoEngine::snapshot_hz = fps, <= 30)
//   * static geometry (site map, bond mask, hatch mask, edge path) is cached per (size, scale, L) -- never rebuilt per frame
//   * one per-pixel pass into a cached Image at physical resolution (capped), then one 1:1 blit + a few vector overlays
//   * no blur, no gradients; per-pixel caches are rebuilt only on size / scale / L change (paint itself only builds a few small Paths and Strings on the message thread; the audio thread never allocates)
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include <array>
#include <vector>

namespace morpho_plugin {

class LatticeView : public juce::Component, private juce::Timer {
public:
    enum class Brush { Pluck, Morphogen, Chisel, Erase, Spiral };
    enum class Layer { Both, Field, Medium };
    explicit LatticeView(MorphoProcessor& p);
    ~LatticeView() override;
    void set_brush(Brush b) { brush_ = b; }
    Brush brush() const { return brush_; }
    void set_layer(Layer l) { layer_ = l; repaint(); }
    void set_eco(bool eco);
    bool eco() const noexcept { return eco_; }
    void paint(juce::Graphics&) override;
    void resized() override { geom_key_ = {}; }
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override { hover_site_ = -1; repaint(); }
    void visibilityChanged() override { subscribe(isShowing()); }
    void parentHierarchyChanged() override { subscribe(isShowing()); }

    std::function<void(const juce::String&)> on_status;
    std::function<void(const Snapshot&)> on_frame;   // fired on the message thread for each new snapshot

    // harness / diagnostics
    bool poll_now();                                  // pull the latest snapshot immediately (returns true if new)
    struct PaintStats { double last_us = 0, median_us = 0, p95_us = 0, rebuild_us = 0; int frames = 0, rebuilds = 0; int fps = 0; int render_w = 0, render_h = 0; };
    PaintStats paint_stats(float only_phys = 0.f) const;   // only_phys > 0: frames painted at that pixel scale
    void reset_paint_stats() { nstat_ = 0; nrebuild_ = 0; }
    int current_fps() const noexcept { return fps_; }

private:
    void timerCallback() override;
    void subscribe(bool on);
    void set_rate(int fps);
    struct GeomKey { int w = 0, h = 0, L = 0; float scale = 0; bool operator==(const GeomKey&) const = default; };
    void rebuild_geometry(int L, float scale);
    juce::Point<float> to_norm(juce::Point<float> p) const;
    void apply_brush(juce::Point<float> pos, bool drag);
    int site_at(juce::Point<float> p) const;

    MorphoProcessor& proc_;
    Snapshot snap_;
    bool have_snap_ = false, eco_ = false, subscribed_ = false;
    int fps_ = 0, quiet_frames_ = 0;
    std::uint32_t last_frame_ = 0;

    // geometry (component coordinates)
    GeomKey geom_key_;
    int L_ = 0, N_ = 0;
    juce::Rectangle<float> field_, frame_;
    double bx0_ = 0, bw_ = 1, by0_ = 0, bh_ = 1;
    float bond_px_ = 8.f;
    std::vector<juce::Point<float>> pts_;
    std::vector<std::array<int, 2>> bonds_;
    std::vector<std::uint8_t> is_edge_;
    std::vector<std::array<int, 2>> edge_bonds_;
    juce::Path edge_path_, arrows_path_;
    int arrows_sign_ = 0;
    // render cache (physical pixels)
    float rscale_ = 1.f;
    int rw_ = 0, rh_ = 0;
    juce::Image img_;
    std::vector<std::int16_t> sitemap_;
    std::vector<std::uint8_t> bondmask_, hatch_;
    std::vector<juce::PixelARGB> site_col_;
    std::vector<std::uint8_t> site_hatch_;

    float e_scale_ = 1e-6f, q_scale_ = 1e-9f;
    Brush brush_ = Brush::Pluck;
    Layer layer_ = Layer::Both;
    juce::Point<float> last_drag_{-1, -1};
    int hover_site_ = -1;

    std::array<double, 128> stat_{};
    std::array<float, 128> stat_phys_{};
    int nstat_ = 0;
    double last_us_ = 0, rebuild_us_ = 0;   // steady-state paint vs geometry rebuild (size / scale / L change only)
    int nrebuild_ = 0;
};

}  // namespace morpho_plugin
