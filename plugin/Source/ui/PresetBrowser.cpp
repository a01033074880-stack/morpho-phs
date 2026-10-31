// SPDX-License-Identifier: AGPL-3.0-only
#include "PresetBrowser.h"
#include <map>

namespace morpho_plugin {
using namespace theme;

juce::Path hexagram(juce::Point<float> c, float r) {
    juce::Path p;
    for (int t = 0; t < 2; ++t) {
        for (int k = 0; k < 3; ++k) {
            const float a = juce::MathConstants<float>::pi * (-0.5f + (float)t + 2.f * (float)k / 3.f);
            const juce::Point<float> q(c.x + r * std::cos(a), c.y + r * std::sin(a));
            if (k == 0) p.startNewSubPath(q); else p.lineTo(q);
        }
        p.closeSubPath();
    }
    return p;
}

namespace {
int cpu_level(const juce::String& c) { return c == "eco" ? 1 : (c == "high" ? 3 : (c == "standard" ? 2 : 0)); }
juce::String cpu_words(const juce::String& c) {
    if (c == "eco") return "ECO  -  under 20 % of budget (i9 est., p99)";
    if (c == "high") return "HIGH  -  50 % of budget or more (i9 est., p99)";
    if (c == "standard") return "STANDARD  -  20-50 % of budget (i9 est., p99)";
    return "not measured";
}
void pill_button(juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, bool primary, bool over) {
    const auto& P = pal();
    const auto path = hex_pill(r);
    g.setColour(primary ? P.accent.withAlpha(over ? 0.24f : 0.14f) : (over ? P.bulk2 : P.bulk1)); g.fillPath(path);
    g.setColour(primary ? P.accent : (over ? P.seamHi : P.seam)); g.strokePath(path, juce::PathStrokeType(primary ? 1.2f : 1.f));
    draw_caption(g, text, r, juce::Justification::centred, primary ? P.accent : (over ? P.ink : P.inkDim), 11.f, 0.1f);
}
}  // namespace

void draw_cpu_pips(juce::Graphics& g, juce::Point<float> c, const juce::String& cls, float r) {
    const auto& P = pal();
    const int lv = cpu_level(cls);
    for (int k = 0; k < 3; ++k) {
        const auto h = hexagon({c.x + (float)k * (2.4f * r), c.y}, r);
        if (k < lv) { g.setColour(lv == 3 ? P.warn : (lv == 1 ? P.accent : P.ink.withAlpha(0.8f))); g.fillPath(h); }
        else { g.setColour(P.seamHi); g.strokePath(h, juce::PathStrokeType(1.f)); }
    }
}

// ================================================================================================ PresetChip
void PresetChip::paint(juce::Graphics& g) {
    const auto& P = pal();
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    const auto path = hex_pill(r);
    g.setColour(open ? P.accent.withAlpha(0.12f) : (hover_ ? P.bulk2 : P.bulk0)); g.fillPath(path);
    g.setColour(open ? P.accent : (hover_ || hasKeyboardFocus(false) ? P.seamHi : P.seam)); g.strokePath(path, juce::PathStrokeType(open ? 1.2f : 1.f));
    const auto name = proc_.current_preset_name();
    const int fi = proc_.find_factory(name);
    const juce::String cat = fi >= 0 ? proc_.factory().getReference(fi).category : (name == "Init" ? juce::String() : juce::String("User"));
    const juce::String cls = fi >= 0 ? proc_.factory().getReference(fi).cpu_class : juce::String();
    auto t = r.reduced(14.f, 0.f);
    t.removeFromRight(26.f);
    if (cls.isNotEmpty()) { draw_cpu_pips(g, {t.getRight() - 16.f, t.getCentreY()}, cls, 2.6f); t.removeFromRight(26.f); }
    const auto catR = t.removeFromLeft(juce::jmin(118.f, juce::GlyphArrangement::getStringWidth(sans(10.f, true).withExtraKerningFactor(0.1f), cat.toUpperCase()) + 14.f));
    draw_caption(g, cat, catR, juce::Justification::centredLeft, P.inkDim, 10.f, 0.1f);
    g.setFont(sans(14.f, true)); g.setColour(P.ink);
    g.drawText(name, t, juce::Justification::centredLeft, true);
    const auto s = star_area();
    const bool fav = fi >= 0 && proc_.is_favourite(name);
    const auto star = hexagram(s.getCentre().translated(-4.f, 0.f), 7.f);
    if (fav) { g.setColour(P.accent); g.fillPath(star); }
    else { g.setColour(fi >= 0 ? P.inkDim : P.inkFaint); g.strokePath(star, juce::PathStrokeType(1.f)); }
}

void PresetChip::mouseUp(const juce::MouseEvent& e) {
    if (!getLocalBounds().contains(e.getPosition())) return;
    if (star_area().contains(e.position)) {
        if (proc_.find_factory(proc_.current_preset_name()) >= 0) { proc_.toggle_favourite(proc_.current_preset_name()); repaint(); if (on_favourite) on_favourite(); }
        return;
    }
    if (on_open) on_open();
}

bool PresetChip::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::returnKey || k == juce::KeyPress::spaceKey) { if (on_open) on_open(); return true; }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> PresetChip::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::button,
        juce::AccessibilityActions().addAction(juce::AccessibilityActionType::press, [this] { if (on_open) on_open(); }));
}

// ================================================================================================ PresetBrowser
PresetBrowser::PresetBrowser(MorphoProcessor& p) : proc_(p) {
    setWantsKeyboardFocus(true); setTitle("Preset browser");
    rebuild();
}

void PresetBrowser::rebuild() {
    cats_.clearQuick(); cat_counts_.clearQuick();
    cats_.add(kAll); cats_.add(kFav);
    const auto& f = proc_.factory();
    for (auto& pd : f) cats_.addIfNotAlreadyThere(pd.category);
    for (auto& c : cats_) {
        int n = 0;
        for (auto& pd : f) n += c == kAll ? 1 : (c == kFav ? (proc_.is_favourite(pd.name) ? 1 : 0) : (pd.category == c ? 1 : 0));
        cat_counts_.add(n);
    }
    refilter();
}

void PresetBrowser::set_filter(const juce::String& c, const juce::String& t) { cat_ = cats_.contains(c) ? c : juce::String(kAll); tag_ = t; refilter(); }

void PresetBrowser::refilter() {
    const auto& f = proc_.factory();
    auto in_cat = [&](const PresetData& pd) { return cat_ == kAll || (cat_ == kFav ? proc_.is_favourite(pd.name) : pd.category == cat_); };
    std::map<juce::String, int> freq;
    for (auto& pd : f) if (in_cat(pd)) for (auto& t : pd.tags) ++freq[t];
    std::vector<std::pair<int, juce::String>> v;
    for (auto& [t, n] : freq) v.push_back({-n, t});
    std::sort(v.begin(), v.end());
    tags_.clearQuick();
    for (auto& [n, t] : v) if (tags_.size() < 14 && n <= -2) tags_.add(t);   // only tags shared by >= 2 presets filter usefully
    if (tag_.isNotEmpty() && !freq.count(tag_)) tag_.clear();
    if (tag_.isNotEmpty() && !tags_.contains(tag_)) tags_.add(tag_);
    rows_.clearQuick();
    for (int i = 0; i < f.size(); ++i) if (in_cat(f.getReference(i)) && (tag_.isEmpty() || f.getReference(i).tags.contains(tag_))) rows_.add(i);
    sel_ = -1;
    sync_to_current();
    layout(); repaint();
}

void PresetBrowser::sync_to_current() {
    const int fi = proc_.find_factory(proc_.current_preset_name());
    sel_ = rows_.indexOf(fi);
    ensure_visible();
    repaint();
}

void PresetBrowser::layout() {
    card_ = getLocalBounds().toFloat().reduced(16.f, 8.f);
    auto r = card_;
    catCol_ = r.removeFromLeft(208.f);
    detail_ = r.removeFromRight(360.f);
    tagRow_ = r.removeFromTop(70.f).reduced(12.f, 0.f).withTrimmedTop(28.f);
    list_ = r.reduced(8.f, 0.f).withTrimmedBottom(30.f);   // key-hint line below
    // tag chips: flow layout, two rows max
    tagRects_.clear();
    float x = tagRow_.getX(), y = tagRow_.getY();
    const auto font = mono(10.f);
    juce::StringArray chips; chips.add({}); chips.addArray(tags_);   // "" = any tag
    for (auto& t : chips) {
        const juce::String label = t.isEmpty() ? juce::String("any") : t;
        const float w = juce::GlyphArrangement::getStringWidth(font, label) + 22.f;
        if (x + w > tagRow_.getRight()) { x = tagRow_.getX(); y += 22.f; if (y > tagRow_.getBottom()) break; }
        tagRects_.push_back({{x, y, w, 18.f}, t});
        x += w + 6.f;
    }
    auto d = detail_.reduced(20.f, 18.f);
    auto b = d.removeFromBottom(30.f);
    closeBtn_ = b.removeFromRight(88.f); b.removeFromRight(8.f);
    initBtn_ = b.removeFromRight(88.f); b.removeFromRight(8.f);
    loadBtn_ = b;
}

void PresetBrowser::ensure_visible() {
    const int vis = juce::jmax(1, (int)(list_.getHeight() / kRowH));
    if (sel_ >= 0) { if (sel_ < scroll_) scroll_ = sel_; if (sel_ >= scroll_ + vis) scroll_ = sel_ - vis + 1; }
    scroll_ = juce::jlimit(0, juce::jmax(0, rows_.size() - vis), scroll_);
}

void PresetBrowser::paint(juce::Graphics& g) {
    const auto& P = pal();
    g.fillAll(P.bulk0.withAlpha(0.88f));
    g.setColour(P.bulk1); g.fillRect(card_);
    g.setColour(P.seamHi); g.drawRect(card_, 1.f);
    g.setColour(P.accent); g.fillRect(card_.withHeight(2.f));
    const auto& f = proc_.factory();

    // ---- category column ----
    g.setColour(P.bulk0); g.fillRect(catCol_.reduced(1.f).withTrimmedTop(2.f));
    g.setColour(P.seam); g.fillRect(catCol_.getRight() - 1.f, catCol_.getY() + 2.f, 1.f, catCol_.getHeight() - 2.f);
    draw_caption(g, kIsFxVariant ? "FX bank" : "Instrument bank", {catCol_.getX() + 16.f, catCol_.getY() + 12.f, 180.f, 16.f},
                 juce::Justification::centredLeft, P.inkDim, 10.5f, 0.14f);
    for (int i = 0; i < cats_.size(); ++i) {
        const juce::Rectangle<float> r(catCol_.getX() + 8.f, catCol_.getY() + 38.f + (float)i * 30.f + (i >= 2 ? 10.f : 0.f), catCol_.getWidth() - 16.f, 26.f);
        const bool on = cats_[i] == cat_, over = hover_hit_.kind == 1 && hover_hit_.index == i;
        if (on) { g.setColour(P.accent.withAlpha(0.12f)); g.fillRect(r); g.setColour(P.accent); g.fillRect(r.withWidth(2.f)); }
        else if (over) { g.setColour(P.bulk2); g.fillRect(r); }
        auto tr = r.reduced(12.f, 0.f);
        if (i == 1) { const auto s = hexagram({tr.getX() + 5.f, tr.getCentreY()}, 5.5f); g.setColour(on ? P.accent : P.inkDim); g.strokePath(s, juce::PathStrokeType(1.f)); tr.removeFromLeft(16.f); }
        g.setFont(sans(13.f, on)); g.setColour(on ? P.ink : (cat_counts_[i] > 0 ? P.inkDim : P.inkFaint));
        g.drawText(cats_[i], tr, juce::Justification::centredLeft, true);
        g.setFont(mono(10.f)); g.setColour(P.inkFaint);
        g.drawText(juce::String(cat_counts_[i]), tr, juce::Justification::centredRight, false);
        if (i == 1) { g.setColour(P.seam); g.fillRect(r.getX(), r.getBottom() + 6.f, r.getWidth(), 1.f); }
    }

    // ---- tag chips ----
    draw_caption(g, "Tags", {tagRow_.getX(), card_.getY() + 12.f, 120.f, 16.f}, juce::Justification::centredLeft, P.inkDim, 10.5f, 0.14f);
    {
        juce::String info = juce::String(rows_.size()) + (rows_.size() == 1 ? " PRESET" : " PRESETS");
        g.setFont(mono(10.f)); g.setColour(P.inkFaint);
        g.drawText(info, juce::Rectangle<float>(tagRow_.getRight() - 160.f, card_.getY() + 12.f, 160.f, 16.f), juce::Justification::centredRight, false);
    }
    for (size_t i = 0; i < tagRects_.size(); ++i) {
        const auto& [r, t] = tagRects_[i];
        const bool on = t == tag_, over = hover_hit_.kind == 2 && hover_hit_.index == (int)i;
        const auto path = hex_pill(r);
        g.setColour(on ? P.accent.withAlpha(0.16f) : (over ? P.bulk2 : P.bulk0)); g.fillPath(path);
        g.setColour(on ? P.accent : (over ? P.seamHi : P.seam)); g.strokePath(path, juce::PathStrokeType(1.f));
        g.setFont(mono(10.f)); g.setColour(on ? P.accent : (over ? P.ink : P.inkDim));
        g.drawText(t.isEmpty() ? juce::String("any") : t, r, juce::Justification::centred, false);
    }

    // ---- list ----
    g.setColour(P.bulk0); g.fillRect(list_);
    g.setColour(P.seam); g.drawRect(list_, 1.f);
    const int vis = (int)(list_.getHeight() / kRowH);
    {
    juce::Graphics::ScopedSaveState ss(g);
    g.reduceClipRegion(list_.toNearestInt());
    for (int k = 0; k < vis + 1 && scroll_ + k < rows_.size(); ++k) {
        const int row = scroll_ + k;
        const auto& pd = f.getReference(rows_[row]);
        const juce::Rectangle<float> r(list_.getX() + 1.f, list_.getY() + 1.f + (float)k * kRowH, list_.getWidth() - 2.f, kRowH);
        const bool on = row == sel_, over = row == hover_row_;
        const bool current = pd.name == proc_.current_preset_name();
        if (on) { g.setColour(P.accent.withAlpha(0.10f)); g.fillRect(r); }
        else if (over) { g.setColour(P.bulk2); g.fillRect(r); }
        if (current) { g.setColour(P.accent); g.fillRect(r.withWidth(2.f)); }
        else if ((row & 1) == 0 && !over) { g.setColour(P.bulk1.withAlpha(0.35f)); g.fillRect(r); }
        auto tr = r.reduced(10.f, 0.f);
        const auto sr = tr.removeFromLeft(22.f);
        const bool fav = proc_.is_favourite(pd.name);
        const auto star = hexagram(sr.getCentre(), 6.f);
        const bool star_over = hover_hit_.kind == 4 && hover_hit_.index == row;
        if (fav) { g.setColour(P.accent); g.fillPath(star); }
        else { g.setColour(star_over ? P.ink : P.inkFaint); g.strokePath(star, juce::PathStrokeType(1.f)); }
        tr.removeFromLeft(6.f);
        draw_cpu_pips(g, {tr.getRight() - 30.f, tr.getCentreY()}, pd.cpu_class, 2.8f);
        tr.removeFromRight(42.f);
        const int L = lattice_size_from_choice((int)std::lround(pd.values[(size_t)pLattice]));
        const int pot = (int)std::lround(pd.values[(size_t)pPotential]);
        g.setFont(mono(9.5f)); g.setColour(P.inkFaint);
        g.drawText(juce::String(L) + (pot == 0 ? " Q" : " HQ"), tr.removeFromRight(46.f), juce::Justification::centredRight, false);
        if (cat_ == kAll || cat_ == kFav) {
            g.setFont(sans(11.5f)); g.setColour(P.inkDim);
            g.drawText(pd.category, tr.removeFromRight(150.f), juce::Justification::centredLeft, true);
        }
        g.setFont(sans(14.f, on || current)); g.setColour(on || current ? P.ink : P.ink.withAlpha(0.86f));
        g.drawText(pd.name, tr, juce::Justification::centredLeft, true);
    }
    }
    if (rows_.isEmpty()) {
        g.setFont(sans(13.f)); g.setColour(P.inkDim);
        g.drawText(cat_ == kFav ? "No favourites yet: click the hexagram next to a preset." : "No preset matches this tag.", list_, juce::Justification::centred, false);
    }
    if (rows_.size() > vis) {   // scroll bar: a hairline + an accent segment
        const float h = list_.getHeight() - 8.f, y0 = list_.getY() + 4.f, x = list_.getRight() - 5.f;
        g.setColour(P.seam); g.fillRect(x, y0, 1.f, h);
        const float a = (float)scroll_ / (float)rows_.size(), b = (float)juce::jmin(rows_.size(), scroll_ + vis) / (float)rows_.size();
        g.setColour(P.inkDim); g.fillRect(x - 1.f, y0 + a * h, 3.f, (b - a) * h);
    }

    // ---- detail card ----
    g.setColour(P.seam); g.fillRect(detail_.getX(), detail_.getY() + 2.f, 1.f, detail_.getHeight() - 2.f);
    auto d = detail_.reduced(20.f, 18.f);
    const PresetData* pd = juce::isPositiveAndBelow(sel_, rows_.size()) ? &f.getReference(rows_[sel_]) : nullptr;
    const bool filtered_out = pd == nullptr;   // the current preset is outside the filter: still describe it
    if (pd == nullptr) { const int fi = proc_.find_factory(proc_.current_preset_name()); if (fi >= 0) pd = &f.getReference(fi); }
    if (pd != nullptr) {
        draw_caption(g, filtered_out ? pd->category + "  /  current, not in this filter" : pd->category, d.removeFromTop(16.f),
                     juce::Justification::centredLeft, P.accent, 10.5f, 0.14f);
        g.setFont(sans(22.f, true)); g.setColour(P.ink);
        g.drawText(pd->name, d.removeFromTop(32.f), juce::Justification::centredLeft, true);
        g.setFont(mono(9.5f)); g.setColour(P.inkFaint);
        g.drawText(pd->author.toUpperCase(), d.removeFromTop(14.f), juce::Justification::centredLeft, true);
        d.removeFromTop(10.f);
        {
            juce::AttributedString s; s.setLineSpacing(3.f);
            s.append(pd->description, sans(13.f), P.ink.withAlpha(0.9f));
            juce::TextLayout tl; tl.createLayout(s, d.getWidth());
            const float h = juce::jmin(110.f, tl.getHeight());
            tl.draw(g, d.removeFromTop(h));
        }
        d.removeFromTop(10.f);
        {   // tags as plain mono text (filtering lives in the chip row)
            g.setFont(mono(10.f)); g.setColour(P.inkDim);
            g.drawText(pd->tags.joinIntoString("  /  "), d.removeFromTop(14.f), juce::Justification::centredLeft, true);
        }
        d.removeFromTop(14.f);
        draw_caption(g, "Macros", d.removeFromTop(16.f), juce::Justification::centredLeft, P.inkDim, 10.5f, 0.14f);
        d.removeFromTop(4.f);
        for (int k = 0; k < 8; ++k) {
            const juce::Rectangle<float> r(d.getX() + (float)(k / 4) * (d.getWidth() * 0.5f), d.getY() + (float)(k % 4) * 20.f, d.getWidth() * 0.5f - 6.f, 18.f);
            g.setColour(P.accent.withAlpha(0.7f)); g.fillPath(hexagon({r.getX() + 5.f, r.getCentreY()}, 3.f));
            g.setFont(mono(10.f)); g.setColour(P.inkFaint);
            g.drawText(juce::String(k + 1), r.withTrimmedLeft(14.f).withWidth(12.f), juce::Justification::centredLeft, false);
            g.setFont(sans(12.5f)); g.setColour(P.ink.withAlpha(0.88f));
            g.drawText(juce::String(pd->macros.names[(size_t)k].data()), r.withTrimmedLeft(28.f), juce::Justification::centredLeft, true);
        }
        d.removeFromTop(4.f * 20.f + 14.f);
        draw_caption(g, "Engine", d.removeFromTop(16.f), juce::Justification::centredLeft, P.inkDim, 10.5f, 0.14f);
        const int L = lattice_size_from_choice((int)std::lround(pd->values[(size_t)pLattice]));
        const int pot = (int)std::lround(pd->values[(size_t)pPotential]);
        const int mode = (int)std::lround(pd->values[(size_t)pMode]);
        const int T = threads_from_choice(0, L, mode, pot);
        g.setFont(mono(10.5f)); g.setColour(P.ink.withAlpha(0.85f));
        g.drawText(juce::String(L) + "x" + juce::String(L) + "  /  " + (pot == 0 ? "QUARTIC" : (pot == 1 ? "HQ FAST" : "HQ EXACT")) + "  /  AUTO " + juce::String(T) + "T",
                   d.removeFromTop(18.f), juce::Justification::centredLeft, false);
        d.removeFromTop(4.f);
        draw_cpu_pips(g, {d.getX() + 4.f, d.getY() + 8.f}, pd->cpu_class, 3.2f);
        g.setFont(mono(10.f)); g.setColour(pd->cpu_class == "high" ? P.warn : P.inkDim);
        g.drawText(cpu_words(pd->cpu_class), d.removeFromTop(16.f).withTrimmedLeft(26.f), juce::Justification::centredLeft, true);
    } else {
        g.setFont(sans(13.f)); g.setColour(P.inkDim);
        g.drawText(proc_.current_preset_name() == "Init" ? "Init: every parameter at its default, no macro targets." : "Select a preset",
                   d.removeFromTop(40.f), juce::Justification::centredLeft, false);
    }
    pill_button(g, loadBtn_, "Load + close", true, hover_hit_.kind == 5);
    pill_button(g, initBtn_, "Init", false, hover_hit_.kind == 6);
    pill_button(g, closeBtn_, "Close", false, hover_hit_.kind == 7);
    g.setFont(mono(9.5f)); g.setColour(P.inkFaint);
    g.drawText("CLICK = AUDITION   DOUBLE-CLICK / ENTER = LOAD   F = FAVOURITE   ESC = CLOSE",
               juce::Rectangle<float>(list_.getX(), card_.getBottom() - 26.f, list_.getWidth(), 14.f), juce::Justification::centredLeft, false);
}

PresetBrowser::Hit PresetBrowser::hit_test(juce::Point<float> p) const {
    for (int i = 0; i < cats_.size(); ++i) {
        const juce::Rectangle<float> r(catCol_.getX() + 8.f, catCol_.getY() + 38.f + (float)i * 30.f + (i >= 2 ? 10.f : 0.f), catCol_.getWidth() - 16.f, 26.f);
        if (r.contains(p)) return {1, i, cats_[i]};
    }
    for (size_t i = 0; i < tagRects_.size(); ++i) if (tagRects_[i].first.contains(p)) return {2, (int)i, tagRects_[i].second};
    if (list_.contains(p)) {
        const int row = scroll_ + (int)((p.y - list_.getY() - 1.f) / kRowH);
        if (row >= 0 && row < rows_.size()) return {p.x < list_.getX() + 34.f ? 4 : 3, row, {}};
        return {};
    }
    if (loadBtn_.contains(p)) return {5, 0, {}};
    if (initBtn_.contains(p)) return {6, 0, {}};
    if (closeBtn_.contains(p)) return {7, 0, {}};
    if (!card_.contains(p)) return {7, -1, {}};   // click outside the card closes
    return {};
}

void PresetBrowser::audition(int row) {
    if (!juce::isPositiveAndBelow(row, rows_.size())) return;
    sel_ = row; ensure_visible();
    proc_.setCurrentProgram(rows_[row]);
    if (on_loaded) on_loaded();
    repaint();
}

void PresetBrowser::mouseDown(const juce::MouseEvent& e) {
    const auto h = hit_test(e.position);
    switch (h.kind) {
        case 1: cat_ = h.text; tag_.clear(); scroll_ = 0; refilter(); break;
        case 2: tag_ = h.text == tag_ ? juce::String() : h.text; scroll_ = 0; refilter(); break;
        case 3: audition(h.index); break;
        case 4: { proc_.toggle_favourite(proc_.factory().getReference(rows_[h.index]).name);
                  const int keep = scroll_; rebuild(); scroll_ = keep; ensure_visible(); if (on_loaded) on_loaded(); repaint(); break; }
        case 5: if (juce::isPositiveAndBelow(sel_, rows_.size()) && proc_.current_preset_name() != proc_.factory().getReference(rows_[sel_]).name) audition(sel_);
                if (on_close) on_close(); break;
        case 6: proc_.load_init(); if (on_loaded) on_loaded(); sel_ = -1; repaint(); break;
        case 7: if (on_close) on_close(); break;
        default: break;
    }
}

void PresetBrowser::mouseDoubleClick(const juce::MouseEvent& e) {
    const auto h = hit_test(e.position);
    if (h.kind == 3 && on_close) on_close();
}

void PresetBrowser::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) {
    scroll_ -= (int)std::round(w.deltaY * 8.f) == 0 ? (w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0)) : (int)std::round(w.deltaY * 8.f);
    ensure_visible(); repaint();
}

void PresetBrowser::mouseMove(const juce::MouseEvent& e) {
    const auto h = hit_test(e.position);
    const int hr = h.kind == 3 || h.kind == 4 ? h.index : -1;
    if (h.kind != hover_hit_.kind || h.index != hover_hit_.index || hr != hover_row_) { hover_hit_ = h; hover_row_ = hr; repaint(); }
}

bool PresetBrowser::keyPressed(const juce::KeyPress& k) {
    if (k == juce::KeyPress::escapeKey) { if (on_close) on_close(); return true; }
    if (k == juce::KeyPress::returnKey) { if (sel_ >= 0 && proc_.current_preset_name() != proc_.factory().getReference(rows_[sel_]).name) audition(sel_); if (on_close) on_close(); return true; }
    if (k == juce::KeyPress::downKey) { audition(juce::jmin(rows_.size() - 1, sel_ + 1)); return true; }
    if (k == juce::KeyPress::upKey) { audition(juce::jmax(0, sel_ - 1)); return true; }
    if (k.getTextCharacter() == 'f' || k.getTextCharacter() == 'F') {
        if (juce::isPositiveAndBelow(sel_, rows_.size())) { proc_.toggle_favourite(proc_.factory().getReference(rows_[sel_]).name); const int s = scroll_; rebuild(); scroll_ = s; ensure_visible(); if (on_loaded) on_loaded(); }
        return true;
    }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> PresetBrowser::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::list);
}

}  // namespace morpho_plugin
