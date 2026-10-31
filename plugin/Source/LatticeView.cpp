// SPDX-License-Identifier: AGPL-3.0-only
#include "LatticeView.h"
#include "ui/Theme.h"
#include "morpho_phs/honeycomb.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace morpho_plugin {
using namespace theme;

namespace {
constexpr int kBusyFps = 30, kEcoFps = 12, kQuietFps = 8, kHiddenPollHz = 4;
constexpr double kMaxRenderPixels = 2.6e6;    // cap of the per-pixel pass: 1:1 up to the max size at 2x (resampled blits cost more than the pass)
inline std::uint8_t lerp8(int a, int b, int t) { return (std::uint8_t)(a + (((b - a) * t + 127) >> 8)); }
inline juce::PixelARGB mix(juce::PixelARGB a, juce::PixelARGB b, int t) {   // t in 0..256
    return juce::PixelARGB(255, lerp8(a.getRed(), b.getRed(), t), lerp8(a.getGreen(), b.getGreen(), t), lerp8(a.getBlue(), b.getBlue(), t));
}
juce::PixelARGB px(juce::Colour c) { return juce::PixelARGB(255, c.getRed(), c.getGreen(), c.getBlue()); }
int lattice_from_param(float v) { return v < 0.5f ? 8 : (v < 1.5f ? 16 : 32); }
}  // namespace

LatticeView::LatticeView(MorphoProcessor& p) : proc_(p) {
    setOpaque(true);
    setTitle("Lattice");
    setDescription("Live honeycomb lattice: click to excite with the selected brush, right-click to erase defects");
    set_rate(kHiddenPollHz);
}
LatticeView::~LatticeView() { stopTimer(); subscribe(false); }

void LatticeView::set_eco(bool e) {
    eco_ = e;
    if (subscribed_) { set_rate(eco_ ? kEcoFps : kBusyFps); subscribe(true); }
    repaint();
}
void LatticeView::set_rate(int fps) { if (fps != fps_) { fps_ = fps; startTimerHz(fps); } }
void LatticeView::subscribe(bool on) {
    subscribed_ = on;
    if (!on) set_rate(kHiddenPollHz);
    else if (fps_ == kHiddenPollHz) { quiet_frames_ = 0; set_rate(eco_ ? kEcoFps : kBusyFps); }
    // the audio thread publishes at the view's frame rate (<= kMaxSnapshotHz) and not at all while hidden / closed
    proc_.host().engine().snapshot_hz.store(on ? std::min(fps_, kMaxSnapshotHz) : 0, std::memory_order_relaxed);
}

bool LatticeView::poll_now() {
    auto& tb = proc_.host().engine().snapshots;
    if (!tb.update()) return false;
    const Snapshot& s = tb.read_buffer();
    if (have_snap_ && s.frame == last_frame_) return false;
    const float prevE = snap_.energy_total;
    snap_ = s; have_snap_ = true; last_frame_ = s.frame;
    float umax = 0.f; for (int i = 0; i < s.N; ++i) umax = std::max(umax, s.medium[(size_t)i]);
    const bool quiet = umax < 0.03f && std::abs(s.energy_total - prevE) <= 0.005f * std::max(1e-12f, std::abs(prevE));
    quiet_frames_ = quiet ? quiet_frames_ + 1 : 0;
    if (on_frame) on_frame(snap_);
    return true;
}

void LatticeView::timerCallback() {
    const bool showing = isShowing();
    if (showing != subscribed_) subscribe(showing);
    if (!showing) return;
    if (poll_now()) repaint();
    // adaptive rate: busy 30 (eco 12) fps; a settled field drops to 8 fps; anything new wakes it again
    const int want = quiet_frames_ > 45 ? kQuietFps : (eco_ ? kEcoFps : kBusyFps);
    if (want != fps_) { set_rate(want); proc_.host().engine().snapshot_hz.store(want, std::memory_order_relaxed); }
}

// --------------------------------------------------------------------------------------------- geometry cache
void LatticeView::rebuild_geometry(int L, float scale) {
    morpho_phs::Honeycomb h(L, L);   // message thread only, on resize / L change
    L_ = L; N_ = h.N;
    double x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (auto& q : h.xy) { x0 = std::min(x0, q[0]); x1 = std::max(x1, q[0]); y0 = std::min(y0, q[1]); y1 = std::max(y1, q[1]); }
    bx0_ = x0; bw_ = x1 - x0; by0_ = y0; bh_ = y1 - y0;
    frame_ = getLocalBounds().toFloat();
    auto area = frame_.reduced(24.f, 0.f).withTrimmedTop(34.f).withTrimmedBottom(30.f);
    const double bond = 1.0 / std::sqrt(3.0);
    // fit including a one-bond margin for the facets
    const float s = (float)std::min(area.getWidth() / (bw_ + 2 * bond), area.getHeight() / (bh_ + 2 * bond));
    const float w = (float)bw_ * s, hh = (float)bh_ * s;
    field_ = juce::Rectangle<float>(w, hh).withCentre(area.getCentre());
    bond_px_ = (float)bond * s;
    pts_.resize((size_t)N_);
    for (int i = 0; i < N_; ++i)
        pts_[(size_t)i] = {field_.getX() + (float)((h.xy[(size_t)i][0] - x0) / bw_) * w, field_.getBottom() - (float)((h.xy[(size_t)i][1] - y0) / bh_) * hh};
    bonds_ = h.bonds;
    // edge channel: sites within half a bond of the parallelogram hull (sheared coordinates u = x - y / sqrt3)
    is_edge_.assign((size_t)N_, 0);
    double u0 = 1e9, u1 = -1e9;
    for (auto& q : h.xy) { const double u = q[0] - q[1] / std::sqrt(3.0); u0 = std::min(u0, u); u1 = std::max(u1, u); }
    const double s60 = std::sqrt(3.0) / 2.0, tol = 0.55 * bond;
    for (int i = 0; i < N_; ++i) {
        const double X = h.xy[(size_t)i][0], Y = h.xy[(size_t)i][1], u = X - Y / std::sqrt(3.0);
        const double d = std::min({(u - u0) * s60, (u1 - u) * s60, Y - y0, y1 - Y});
        is_edge_[(size_t)i] = d <= tol ? 1 : 0;
    }
    edge_bonds_.clear();
    for (auto b : bonds_) if (is_edge_[(size_t)b[0]] && is_edge_[(size_t)b[1]]) edge_bonds_.push_back(b);
    // hull outline (one bond outside the outermost sites)
    {
        const float m = bond_px_ * 0.9f;
        auto P = [&](double u, double y) { const double X = u + y / std::sqrt(3.0); return juce::Point<float>(field_.getX() + (float)((X - x0) / bw_) * w, field_.getBottom() - (float)((y - y0) / bh_) * hh); };
        const double du = (double)m / s / s60, dy = (double)m / s;
        edge_path_.clear();
        edge_path_.startNewSubPath(P(u0 - du, y0 - dy)); edge_path_.lineTo(P(u1 + du, y0 - dy));
        edge_path_.lineTo(P(u1 + du, y1 + dy)); edge_path_.lineTo(P(u0 - du, y1 + dy)); edge_path_.closeSubPath();
    }
    arrows_sign_ = 0; arrows_path_.clear();   // chevrons are in component space: rebuild with the outline

    // ---- physical-pixel caches ----
    const auto ir = field_.expanded(bond_px_ * 1.1f).getSmallestIntegerContainer();
    rscale_ = std::min(scale, (float)std::sqrt(kMaxRenderPixels / std::max(1.0, (double)ir.getWidth() * ir.getHeight())));
    rscale_ = std::max(0.5f, rscale_);
    rw_ = std::max(1, (int)std::ceil(ir.getWidth() * rscale_)); rh_ = std::max(1, (int)std::ceil(ir.getHeight() * rscale_));
    img_ = juce::Image(juce::Image::RGB, rw_, rh_, false);
    const size_t np = (size_t)rw_ * (size_t)rh_;
    sitemap_.assign(np, -1);
    std::vector<float> dist(np, 1e30f);
    const float ox = (float)ir.getX(), oy = (float)ir.getY();
    const float cr = 0.72f * bond_px_ * rscale_, cr2 = cr * cr;
    for (int i = 0; i < N_; ++i) {
        const float cx = (pts_[(size_t)i].x - ox) * rscale_, cy = (pts_[(size_t)i].y - oy) * rscale_;
        const int xa = std::max(0, (int)std::floor(cx - cr)), xb = std::min(rw_ - 1, (int)std::ceil(cx + cr));
        const int ya = std::max(0, (int)std::floor(cy - cr)), yb = std::min(rh_ - 1, (int)std::ceil(cy + cr));
        for (int y = ya; y <= yb; ++y)
            for (int x = xa; x <= xb; ++x) {
                const float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy, d2 = dx * dx + dy * dy;
                const size_t k = (size_t)y * (size_t)rw_ + (size_t)x;
                if (d2 < cr2 && d2 < dist[k]) { dist[k] = d2; sitemap_[k] = (std::int16_t)i; }
            }
    }
    // bond mask (anti-aliased, drawn once with JUCE into a single-channel image)
    {
        juce::Image m(juce::Image::SingleChannel, rw_, rh_, true);
        {
            juce::Graphics gm(m);
            gm.addTransform(juce::AffineTransform::translation(-ox, -oy).scaled(rscale_));
            gm.setColour(juce::Colours::white);
            const float lw = juce::jlimit(0.7f, 1.4f, bond_px_ * 0.14f);
            juce::Path p;
            for (auto b : bonds_) { p.startNewSubPath(pts_[(size_t)b[0]]); p.lineTo(pts_[(size_t)b[1]]); }
            gm.strokePath(p, juce::PathStrokeType(lw));
        }
        bondmask_.assign(np, 0);
        juce::Image::BitmapData bd(m, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < rh_; ++y) { const auto* row = bd.getLinePointer(y); for (int x = 0; x < rw_; ++x) bondmask_[(size_t)y * rw_ + x] = row[x * bd.pixelStride]; }
    }
    // hatch (60-degree lines, analytic AA): the Barkley medium's texture
    hatch_.assign(np, 0);
    {
        const float P = 4.f * rscale_, hw = 0.5f * std::max(1.f, 0.9f * rscale_);
        const float ca = 0.5f, sa = 0.8660254f;
        for (int y = 0; y < rh_; ++y)
            for (int x = 0; x < rw_; ++x) {
                float d = std::fmod((float)x * ca + (float)y * sa, P); d = std::abs(d - 0.5f * P);
                hatch_[(size_t)y * rw_ + x] = (std::uint8_t)(255.f * juce::jlimit(0.f, 1.f, hw + 0.5f - d));
            }
    }
    site_col_.assign((size_t)N_, px(pal().heat[0]));
    site_hatch_.assign((size_t)N_, 0);
    geom_key_ = {getWidth(), getHeight(), L, scale};
}

// --------------------------------------------------------------------------------------------- paint
static juce::String fmt_energy(float e) {   // fixed-width mono readout: 0.0123 / 12.35 / 1.29e7
    if (!(e > 0.f)) return "0";
    if (e < 1e-3f || e >= 1e4f) { char b[24]; std::snprintf(b, sizeof b, "%.2fe%d", e / std::pow(10.f, std::floor(std::log10(e))), (int)std::floor(std::log10(e))); return b; }
    return juce::String(e, e < 10.f ? 3 : 1);
}
void LatticeView::paint(juce::Graphics& g) {
    auto t0 = juce::Time::getHighResolutionTicks();
    const auto& P = pal();
    g.fillAll(P.bulk0);
    const float phys = std::max(0.5f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int L = have_snap_ && snap_.L > 0 ? snap_.L : lattice_from_param(proc_.param_now(pLattice));
    if (!(geom_key_ == GeomKey{getWidth(), getHeight(), L, phys})) {
        const auto r0 = juce::Time::getHighResolutionTicks();
        rebuild_geometry(L, phys);
        rebuild_us_ = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - r0) * 1e6; ++nrebuild_;
        t0 += juce::Time::getHighResolutionTicks() - r0;   // steady-state stats exclude the one-off rebuild
    }
    const bool live = have_snap_ && snap_.L == L_ && snap_.N == N_;
    const int N = N_;

    // ---- per-site colours ----
    float emax = 0.f, qmax = 0.f;
    if (live)
        for (int i = 0; i < N; ++i) { emax = std::max(emax, snap_.energy[(size_t)i]); qmax = std::max(qmax, std::abs(snap_.qx[(size_t)i]) + std::abs(snap_.qy[(size_t)i])); }
    e_scale_ = std::max(emax, e_scale_ * 0.97f + 1e-12f);
    q_scale_ = std::max(qmax, q_scale_ * 0.97f + 1e-15f);
    const float le = std::log10(e_scale_ + 1e-30f), iq = 1.f / q_scale_;
    const auto cold = px(P.heat[0]), bulk = px(P.bulk0), seam = px(P.seam), ink = px(P.ink), medBase = px(P.bulk2);
    for (int i = 0; i < N; ++i) {
        juce::PixelARGB c = cold;
        std::uint8_t hz = 0;
        if (live) {
            const float e = snap_.energy[(size_t)i];
            float t = e > 0 ? 1.f + (std::log10(e + 1e-30f) - le) / 4.f : 0.f;   // 40 dB window
            t = juce::jlimit(0.f, 1.f, t);
            if (layer_ == Layer::Medium) c = medBase;
            else {
                c = heat_px(t);
                // facet tilt: displacement towards the upper-left light brightens, away darkens (the "morphing polyhedra")
                const float tilt = juce::jlimit(-1.f, 1.f, (-0.6f * snap_.qx[(size_t)i] + 0.8f * snap_.qy[(size_t)i]) * iq * 1.4f);
                const float f = 1.f + 0.32f * tilt * std::min(1.f, t * 1.6f);
                c = juce::PixelARGB(255, (std::uint8_t)std::min(255.f, c.getRed() * f), (std::uint8_t)std::min(255.f, c.getGreen() * f), (std::uint8_t)std::min(255.f, c.getBlue() * f));
            }
            if (layer_ != Layer::Field) {
                const float u = snap_.medium[(size_t)i];
                const float a = juce::jlimit(0.f, 1.f, (u - 0.06f) / 0.5f);
                hz = (std::uint8_t)(a * (layer_ == Layer::Medium ? 235.f : 150.f));
            }
            if (snap_.defect[(size_t)i]) c = mix(c, bulk, 170);
        }
        site_col_[(size_t)i] = c; site_hatch_[(size_t)i] = hz;
    }
    // ---- one per-pixel pass ----
    {
        juce::Image::BitmapData bd(img_, juce::Image::BitmapData::writeOnly);
        const int stride = bd.pixelStride;
        for (int y = 0; y < rh_; ++y) {
            auto* row = bd.getLinePointer(y);
            const size_t base = (size_t)y * (size_t)rw_;
            for (int x = 0; x < rw_; ++x) {
                const size_t k = base + (size_t)x;
                const int s = sitemap_[k];
                juce::PixelARGB c = s < 0 ? bulk : site_col_[(size_t)s];
                if (s >= 0 && site_hatch_[(size_t)s]) {
                    const int hm = (hatch_[k] * site_hatch_[(size_t)s]) >> 8;
                    if (hm) c = mix(c, ink, hm);
                }
                if (const int m = bondmask_[k]) c = mix(c, seam, (m * 205) >> 8);
                auto* d = row + x * stride;
                if (stride == 4) reinterpret_cast<juce::PixelARGB*>(d)->set(c);
                else reinterpret_cast<juce::PixelRGB*>(d)->set(c);
            }
        }
    }
    const auto ir = field_.expanded(bond_px_ * 1.1f).getSmallestIntegerContainer();
    g.setImageResamplingQuality(rscale_ < phys - 0.01f ? juce::Graphics::mediumResamplingQuality : juce::Graphics::lowResamplingQuality);
    g.drawImage(img_, ir.toFloat());

    // ---- edge channel ----
    const bool edgeMode = live ? snap_.mode == 0 : (proc_.param_now(pMode) < 0.5f && kIsFxVariant);
    const float chir = proc_.param_now(pChirality);
    const int sign = chir > 0.02f ? 1 : (chir < -0.02f ? -1 : 0);
    if (sign != arrows_sign_ || arrows_path_.isEmpty()) {
        arrows_sign_ = sign; arrows_path_.clear();
        if (sign != 0) {   // chevrons along the outline, pointing along the chiral (edge-mode) circulation
            juce::PathFlatteningIterator it(edge_path_);
            float next = 28.f; const float step = 56.f;   // distance along the outline to the next chevron
            juce::Point<float> a, b;
            while (it.next()) {
                a = {it.x1, it.y1}; b = {it.x2, it.y2};
                const float len = a.getDistanceFrom(b); if (len < 1e-3f) continue;
                auto dir = (b - a) / len; if (sign < 0) dir = -dir;
                const juce::Point<float> nrm(-dir.y, dir.x);
                float t = next;
                for (; t < len; t += step) {
                    const auto c = a + (b - a) * (t / len);
                    arrows_path_.startNewSubPath(c - dir * 3.f + nrm * 3.5f); arrows_path_.lineTo(c + dir * 2.f); arrows_path_.lineTo(c - dir * 3.f - nrm * 3.5f);
                }
                next = t - len;
            }
        }
    }
    g.setColour(P.accent.withAlpha(edgeMode ? 0.55f : 0.26f));
    g.strokePath(edge_path_, juce::PathStrokeType(1.f));
    g.setColour(P.accent.withAlpha(edgeMode ? 0.8f : 0.45f));
    g.strokePath(arrows_path_, juce::PathStrokeType(1.2f));
    if (live) {
        // energy carried along the edge: accent dashes on edge bonds, 4 alpha buckets -> 4 strokes
        juce::Path bucket[4];
        for (auto b : edge_bonds_) {
            const float e = std::max(snap_.energy[(size_t)b[0]], snap_.energy[(size_t)b[1]]);
            const float t = e > 0 ? 1.f + (std::log10(e + 1e-30f) - le) / 3.f : 0.f;
            if (t <= 0.15f) continue;
            const int k = std::min(3, (int)(t * 4.f));
            bucket[k].startNewSubPath(pts_[(size_t)b[0]]); bucket[k].lineTo(pts_[(size_t)b[1]]);
        }
        for (int k = 0; k < 4; ++k) if (!bucket[k].isEmpty()) {
            g.setColour(P.accent.withAlpha(0.25f + 0.22f * (float)k));
            g.strokePath(bucket[k], juce::PathStrokeType(1.6f + 0.3f * (float)k, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        }
        // gyro needles (displacement direction and size); skipped in eco / low-motion mode
        if (!eco_ && layer_ != Layer::Medium) {
            juce::Path nd; int count = 0;
            const float thr = 0.3f * q_scale_, len = bond_px_ * 0.85f;
            for (int i = 0; i < N && count < 600; ++i) {
                const float qx = snap_.qx[(size_t)i], qy = snap_.qy[(size_t)i], m = std::abs(qx) + std::abs(qy);
                if (m < thr) continue;
                const float r = std::sqrt(qx * qx + qy * qy) + 1e-30f, l = len * std::min(1.f, m * iq);
                const auto p = pts_[(size_t)i];
                nd.startNewSubPath(p); nd.lineTo(p.x + qx / r * l, p.y - qy / r * l); ++count;
            }
            g.setColour(P.ink.withAlpha(0.6f));
            g.strokePath(nd, juce::PathStrokeType(1.f));
        }
        // defects: hex rings
        juce::Path dp;
        const float hr = bond_px_ * 0.55f;
        for (int i = 0; i < N; ++i) if (snap_.defect[(size_t)i]) dp.addPath(hexagon(pts_[(size_t)i], hr));
        if (!dp.isEmpty()) { g.setColour(P.bulk0.withAlpha(0.7f)); g.strokePath(dp, juce::PathStrokeType(3.f)); g.setColour(P.ink.withAlpha(0.9f)); g.strokePath(dp, juce::PathStrokeType(1.1f)); }
        // ports
        g.setFont(mono(9.5f, true));
        auto port_pt = [&](int k) -> const juce::Point<float>* { const int s = snap_.port[(size_t)k]; return s >= 0 && s < N ? &pts_[(size_t)s] : nullptr; };
        const float pr = std::max(4.f, bond_px_ * 0.95f);
        // overlays keep a dark under-stroke so they stay legible on a saturated (bright) field
        if (auto* p = port_pt(kPortSrc)) {
            g.setColour(P.bulk0.withAlpha(0.75f)); g.strokePath(hexagon(*p, pr), juce::PathStrokeType(3.4f));
            g.setColour(P.accent); g.strokePath(hexagon(*p, pr), juce::PathStrokeType(1.4f));
            g.drawText("SRC", juce::Rectangle<float>(p->x + pr + 2.f, p->y - 7.f, 30.f, 14.f), juce::Justification::centredLeft, false);
        }
        for (int k : {kPortPickL, kPortPickR}) if (auto* p = port_pt(k)) {
            g.setColour(P.bulk0.withAlpha(0.75f)); g.drawEllipse(juce::Rectangle<float>(2 * pr, 2 * pr).withCentre(*p), 3.4f);
            g.setColour(P.ink); g.drawEllipse(juce::Rectangle<float>(2 * pr, 2 * pr).withCentre(*p), 1.2f);
            g.drawText(k == kPortPickL ? "L" : "R", juce::Rectangle<float>(p->x - 6.f, p->y - pr - 14.f, 12.f, 12.f), juce::Justification::centred, false);
        }
        if (auto* p = port_pt(kPortBrush)) {
            juce::Path ring, dash; ring.addEllipse(juce::Rectangle<float>(2.6f * pr, 2.6f * pr).withCentre(*p));
            const float dl[] = {2.5f, 2.5f}; juce::PathStrokeType(1.2f).createDashedStroke(dash, ring, dl, 2);
            g.setColour(P.accent.withAlpha(0.9f)); g.fillPath(dash);
        }
        g.setColour(P.accent);
        for (int k = 0; k < kMaxVoices; ++k) if (auto* p = port_pt(kPortVoice0 + k)) g.fillPath(hexagon(*p, std::max(2.5f, bond_px_ * 0.42f)));
    }

    // ---- frame furniture: caption, heat legend, info line, corner ticks ----
    const auto fr = frame_.reduced(0.5f);
    g.setColour(P.seam); g.drawRect(fr, 1.f);
    g.setColour(P.seamHi);
    for (auto c : {fr.getTopLeft(), fr.getTopRight(), fr.getBottomLeft(), fr.getBottomRight()}) {
        const float sx = c.x < fr.getCentreX() ? 1.f : -1.f, sy = c.y < fr.getCentreY() ? 1.f : -1.f;
        g.drawLine(c.x, c.y, c.x + 10.f * sx, c.y, 1.5f); g.drawLine(c.x, c.y, c.x, c.y + 10.f * sy, 1.5f);
    }
    auto top = frame_.reduced(16.f, 0.f).withHeight(30.f).withY(frame_.getY() + 4.f);
    draw_caption(g, "Lattice", top.removeFromLeft(60.f), juce::Justification::centredLeft, P.ink, 11.f, 0.14f);
    g.setFont(mono(11.f)); g.setColour(P.inkDim);
    g.drawText(layer_ == Layer::Medium ? "u(x,t)  BARKLEY" : (layer_ == Layer::Field ? "H(q,p)  ENERGY" : "H(q,p)  +  u(x,t)"), top.removeFromLeft(200.f), juce::Justification::centredLeft, false);
    {   // heat legend: 40 dB window
        auto lg = top.removeFromRight(170.f);
        auto lab = lg.removeFromRight(42.f); auto lab0 = lg.removeFromLeft(44.f);
        const auto bar = lg.withSizeKeepingCentre(lg.getWidth(), 6.f);
        const int n = 24;
        for (int i = 0; i < n; ++i) {
            g.setColour(heat((float)i / (float)(n - 1)));
            g.fillRect(juce::Rectangle<float>(bar.getX() + bar.getWidth() * (float)i / n, bar.getY(), bar.getWidth() / n + 0.5f, bar.getHeight()));
        }
        g.setFont(mono(9.5f)); g.setColour(P.inkDim);
        g.drawText("-40 dB", lab0, juce::Justification::centredRight, false);
        g.drawText("PEAK", lab.translated(5.f, 0.f), juce::Justification::centredLeft, false);
    }
    auto bot = frame_.reduced(16.f, 0.f).withTop(frame_.getBottom() - 28.f).withTrimmedBottom(6.f);
    juce::String info = juce::String(L_) + "x" + juce::String(L_) + "  " + juce::String(N_) + " SITES";
    if (live) {
        info << "  " << juce::String(snap_.threads) << " THR  H " << fmt_energy(snap_.energy_total);
        if (snap_.resets) info << "  RESETS " << snap_.resets;
    } else info << "  AWAITING AUDIO";
    g.setFont(mono(11.f)); g.setColour(P.inkDim);
    g.drawText(info, bot, juce::Justification::centredLeft, false);
    // the energy gate is the designed limiter of the bow / Life gain (it regulates sustain), so it reads as state, not alarm
    if (live && snap_.gate_open < 0.5f) { g.setColour(P.inkDim); g.drawText("E-GATE REGULATING", bot, juce::Justification::centred, false); }
    if (hover_site_ >= 0 && hover_site_ < N && live) {
        const auto i = (size_t)hover_site_;
        g.setColour(P.ink);
        g.drawText("SITE " + juce::String(hover_site_) + (is_edge_[i] ? " EDGE" : "") + "  E " + juce::String(snap_.energy[i], 4)
                       + "  u " + juce::String(snap_.medium[i], 2) + (snap_.defect[i] ? "  DEFECT" : ""),
                   bot, juce::Justification::centredRight, false);
        g.setColour(P.ink.withAlpha(0.8f)); g.strokePath(hexagon(pts_[i], bond_px_ * 0.7f), juce::PathStrokeType(1.f));
    } else if (eco_) {
        g.setColour(P.inkDim); g.drawText("LOW MOTION", bot, juce::Justification::centredRight, false);
    }

    last_us_ = juce::Time::highResolutionTicksToSeconds(juce::Time::getHighResolutionTicks() - t0) * 1e6;
    stat_[(size_t)(nstat_ % (int)stat_.size())] = last_us_; stat_phys_[(size_t)(nstat_ % (int)stat_.size())] = phys; ++nstat_;
}

LatticeView::PaintStats LatticeView::paint_stats(float only_phys) const {
    PaintStats s; s.last_us = last_us_; s.rebuild_us = rebuild_us_; s.rebuilds = nrebuild_; s.frames = nstat_; s.fps = fps_; s.render_w = rw_; s.render_h = rh_;
    const int n = std::min(nstat_, (int)stat_.size());
    std::vector<double> v;
    for (int k = 0; k < n; ++k) if (only_phys <= 0.f || std::abs(stat_phys_[(size_t)k] - only_phys) < 1e-3f) v.push_back(stat_[(size_t)k]);
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    const int n2 = (int)v.size(); s.frames = n2;
    s.median_us = v[(size_t)n2 / 2]; s.p95_us = v[(size_t)std::min(n2 - 1, (int)std::ceil(0.95 * n2) - 1)];
    return s;
}

// --------------------------------------------------------------------------------------------- interaction
juce::Point<float> LatticeView::to_norm(juce::Point<float> p) const {
    return {juce::jlimit(0.f, 1.f, (p.x - field_.getX()) / std::max(1.f, field_.getWidth())),
            juce::jlimit(0.f, 1.f, (field_.getBottom() - p.y) / std::max(1.f, field_.getHeight()))};
}
int LatticeView::site_at(juce::Point<float> p) const {
    int best = -1; float bd = bond_px_ * bond_px_;
    for (int i = 0; i < (int)pts_.size(); ++i) { const float d = pts_[(size_t)i].getDistanceSquaredFrom(p); if (d < bd) { bd = d; best = i; } }
    return best;
}
void LatticeView::apply_brush(juce::Point<float> pos, bool drag) {
    if (field_.isEmpty()) return;
    const auto q = to_norm(pos);
    UiEvent e; e.x = q.x; e.y = q.y; e.amount = 1.f;
    switch (brush_) {
        case Brush::Pluck: if (drag) return; e.type = UiEvent::Pluck; break;
        case Brush::Morphogen: e.type = UiEvent::Morphogen; e.amount = drag ? 0.35f : 0.8f; break;
        case Brush::Chisel: e.type = UiEvent::ChiselAdd; break;
        case Brush::Erase: e.type = UiEvent::ChiselRemove; break;
        case Brush::Spiral: if (drag) return; e.type = UiEvent::SeedSpiral; break;
    }
    proc_.send_event(e);
    quiet_frames_ = 0;
}
void LatticeView::mouseDown(const juce::MouseEvent& ev) {
    if (ev.mods.isRightButtonDown()) { const auto b = brush_; brush_ = Brush::Erase; apply_brush(ev.position, false); brush_ = b; return; }
    last_drag_ = ev.position; apply_brush(ev.position, false);
}
void LatticeView::mouseDrag(const juce::MouseEvent& ev) {
    if (ev.position.getDistanceFrom(last_drag_) < bond_px_ * 1.5f) return;   // rate-limit by distance
    last_drag_ = ev.position;
    if (ev.mods.isRightButtonDown()) { const auto b = brush_; brush_ = Brush::Erase; apply_brush(ev.position, true); brush_ = b; return; }
    apply_brush(ev.position, true);
}
void LatticeView::mouseMove(const juce::MouseEvent& ev) {
    const int s = site_at(ev.position);
    if (s != hover_site_) { hover_site_ = s; repaint(); }
}

}  // namespace morpho_plugin
