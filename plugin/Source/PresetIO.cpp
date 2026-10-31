// SPDX-License-Identifier: AGPL-3.0-only
#include "PresetIO.h"

namespace morpho_plugin {

juce::StringArray choice_labels(int param) {
    juce::StringArray a;
    if (kParams[(size_t)param].choices) a.addTokens(kParams[(size_t)param].choices, "|", "");
    return a;
}

static void read_points(const juce::var& v, std::vector<PresetPoint>& out, const char* what, PresetIssues& is) {
    if (v.isVoid()) return;
    if (!v.isArray()) { is.errors.add(juce::String(what) + ": expected an array of [x, y(, amount)]"); return; }
    for (auto& e : *v.getArray()) {
        if (!e.isArray() || e.size() < 2) { is.errors.add(juce::String(what) + ": entry is not [x, y]"); continue; }
        PresetPoint p; p.x = (float)(double)e[0]; p.y = (float)(double)e[1]; p.amount = e.size() > 2 ? (float)(double)e[2] : 1.f;
        if (p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) { is.warnings.add(juce::String(what) + ": point outside [0,1] clamped"); }
        p.x = juce::jlimit(0.f, 1.f, p.x); p.y = juce::jlimit(0.f, 1.f, p.y);
        out.push_back(p);
    }
}

PresetData preset_from_json(const juce::var& root, PresetIssues& is) {
    PresetData p;
    if (!root.isObject()) { is.errors.add("root is not a JSON object"); return p; }
    const juce::String fmt = root.getProperty("format", juce::var()).toString();
    if (fmt != "morpho-phs-preset") is.errors.add("format must be \"morpho-phs-preset\" (got \"" + fmt + "\")");
    const int ver = (int)root.getProperty("version", 0);
    if (ver != 1 && ver != 2) is.errors.add("unsupported version " + juce::String(ver) + " (expected 1 or 2)");
    p.version = ver;
    p.name = root.getProperty("name", "Untitled").toString();
    p.category = root.getProperty("category", "Uncategorised").toString();
    p.author = root.getProperty("author", "").toString();
    p.description = root.getProperty("description", "").toString();
    p.description_zh = root.getProperty("description_zh", "").toString();
    p.cpu_class = root.getProperty("cpu_class", "").toString();
    if (p.cpu_class.isNotEmpty() && p.cpu_class != "eco" && p.cpu_class != "standard" && p.cpu_class != "high")
        is.warnings.add("cpu_class \"" + p.cpu_class + "\" is not eco / standard / high");
    {
        const juce::var tg = root.getProperty("tags", juce::var());
        if (tg.isArray()) for (auto& t : *tg.getArray()) { const auto s = t.toString().trim().toLowerCase(); if (s.isNotEmpty()) p.tags.addIfNotAlreadyThere(s); }
        else if (!tg.isVoid()) is.warnings.add("\"tags\" should be an array of strings");
    }

    const juce::var params = root.getProperty("params", juce::var());
    if (auto* obj = params.getDynamicObject()) {
        for (auto& kv : obj->getProperties()) {
            const int idx = param_index(kv.name.toString().toStdString());
            if (idx < 0) { is.warnings.add("unknown parameter \"" + kv.name.toString() + "\" ignored"); continue; }
            const auto& d = kParams[(size_t)idx];
            float v;
            if (d.kind == PKind::Choice && kv.value.isString()) {
                const int k = choice_labels(idx).indexOf(kv.value.toString(), true);
                if (k < 0) { is.errors.add("parameter \"" + kv.name.toString() + "\": unknown choice \"" + kv.value.toString() + "\""); continue; }
                v = (float)k;
            } else if (kv.value.isDouble() || kv.value.isInt() || kv.value.isInt64() || kv.value.isBool()) {
                v = (float)(double)kv.value;
            } else { is.errors.add("parameter \"" + kv.name.toString() + "\": not a number"); continue; }
            if (v < d.min - 1e-6f || v > d.max + 1e-6f) {
                is.warnings.add("parameter \"" + kv.name.toString() + "\" = " + juce::String(v) + " outside [" + juce::String(d.min) + ", " + juce::String(d.max) + "], clamped");
                v = juce::jlimit(d.min, d.max, v);
            }
            if (d.kind == PKind::Choice) v = std::round(v);
            p.values[(size_t)idx] = v; p.present[(size_t)idx] = true;
        }
    } else if (!params.isVoid()) is.errors.add("\"params\" must be an object");
    // Backward compatibility: version 1 had "8 samples" as the g/mu interpolation default (Phase 4 / D1 changed the
    // default to "1 sample"), so a v1 preset without "interp" keeps its original sound.  v1 presets have no
    // edge_width / edge_crosstalk -> defaults 1 / 0 (identity, = the v1 behaviour).
    if (ver == 1 && !p.present[(size_t)pInterp]) { p.values[(size_t)pInterp] = 1.f; p.present[(size_t)pInterp] = true; }
    // Phase 5c: HQ exact is limited to 16x16 or smaller (MorphoEngine.hpp kHqExactMaxL); a 32x32 preset that asks for it
    // stays valid and loads unchanged, but plays as HQ fast -> warning only.
    if (p.present[(size_t)pPotential] && p.present[(size_t)pLattice] && p.values[(size_t)pPotential] >= 1.5f && p.values[(size_t)pLattice] >= 1.5f)
        is.warnings.add("potential \"HQ exact\" is limited to 16x16 or smaller; this 32x32 preset plays as \"HQ (fast log-cosh)\"");

    const juce::var macros = root.getProperty("macros", juce::var());
    if (macros.isObject()) {
        const juce::var names = macros.getProperty("names", juce::var());
        if (names.isArray())
            for (int k = 0; k < juce::jmin(8, names.size()); ++k) {
                p.macros.names[(size_t)k].fill(0);
                names[k].toString().copyToUTF8(p.macros.names[(size_t)k].data(), 23);
            }
        const juce::var targets = macros.getProperty("targets", juce::var());
        p.macros.count = 0;
        if (targets.isArray())
            for (auto& t : *targets.getArray()) {
                const int m = (int)t.getProperty("macro", 0) - 1;
                const int prm = param_index(t.getProperty("param", "").toString().toStdString());
                if (m < 0 || m > 7) { is.errors.add("macro target: \"macro\" must be 1..8"); continue; }
                if (prm < 0 || !is_engine_param(prm)) { is.errors.add("macro target: unknown or non-engine param \"" + t.getProperty("param", "").toString() + "\""); continue; }
                if (p.macros.count >= MacroMap::kMax) { is.errors.add("more than 32 macro targets"); break; }
                const auto& d = kParams[(size_t)prm];
                MacroTarget mt; mt.macro = m; mt.param = prm;
                mt.lo = juce::jlimit(d.min, d.max, (float)(double)t.getProperty("lo", d.min));
                mt.hi = juce::jlimit(d.min, d.max, (float)(double)t.getProperty("hi", d.max));
                p.macros.t[(size_t)p.macros.count++] = mt;
            }
    }
    const juce::var st = root.getProperty("lattice_state", juce::var());
    if (st.isObject()) {
        read_points(st.getProperty("defects", juce::var()), p.defects, "lattice_state.defects", is);
        read_points(st.getProperty("morphogen", juce::var()), p.morphogen, "lattice_state.morphogen", is);
        const juce::var sp = st.getProperty("seed_spiral", juce::var());
        if (sp.isArray() && sp.size() >= 2) { p.has_spiral = true; p.spiral.x = (float)(double)sp[0]; p.spiral.y = (float)(double)sp[1]; }
    }
    return p;
}

bool preset_from_string(const juce::String& text, PresetData& out, PresetIssues& is) {
    juce::var root;
    const auto r = juce::JSON::parse(text, root);
    if (r.failed()) { is.errors.add("JSON parse error: " + r.getErrorMessage()); return false; }
    out = preset_from_json(root, is);
    return is.ok();
}

bool preset_from_file(const juce::File& f, PresetData& out, PresetIssues& is) {
    if (!f.existsAsFile()) { is.errors.add("file not found: " + f.getFullPathName()); return false; }
    return preset_from_string(f.loadFileAsString(), out, is);
}

juce::var preset_to_json(const PresetData& p) {
    auto* root = new juce::DynamicObject();
    root->setProperty("format", "morpho-phs-preset");
    root->setProperty("version", 2);
    root->setProperty("name", p.name); root->setProperty("category", p.category);
    root->setProperty("author", p.author); root->setProperty("description", p.description);
    if (p.description_zh.isNotEmpty()) root->setProperty("description_zh", p.description_zh);
    if (!p.tags.isEmpty()) { juce::Array<juce::var> tg; for (auto& t : p.tags) tg.add(t); root->setProperty("tags", tg); }
    if (p.cpu_class.isNotEmpty()) root->setProperty("cpu_class", p.cpu_class);
    auto* params = new juce::DynamicObject();
    for (int i = 0; i < kNumParams; ++i) {
        const auto& d = kParams[(size_t)i];
        if (d.kind == PKind::Choice) params->setProperty(d.id, choice_labels(i)[(int)p.values[(size_t)i]]);
        else params->setProperty(d.id, (double)p.values[(size_t)i]);
    }
    root->setProperty("params", juce::var(params));
    auto* macros = new juce::DynamicObject();
    juce::Array<juce::var> names, targets;
    for (int k = 0; k < 8; ++k) names.add(juce::String(p.macros.names[(size_t)k].data()));
    for (int k = 0; k < p.macros.count; ++k) {
        auto* t = new juce::DynamicObject();
        const auto& mt = p.macros.t[(size_t)k];
        t->setProperty("macro", mt.macro + 1); t->setProperty("param", kParams[(size_t)mt.param].id);
        t->setProperty("lo", (double)mt.lo); t->setProperty("hi", (double)mt.hi);
        targets.add(juce::var(t));
    }
    macros->setProperty("names", names); macros->setProperty("targets", targets);
    root->setProperty("macros", juce::var(macros));
    auto* st = new juce::DynamicObject();
    auto pts = [](const std::vector<PresetPoint>& v, bool amt) {
        juce::Array<juce::var> a;
        for (auto& q : v) { juce::Array<juce::var> e { (double)q.x, (double)q.y }; if (amt) e.add((double)q.amount); a.add(e); }
        return a;
    };
    st->setProperty("defects", pts(p.defects, false));
    st->setProperty("morphogen", pts(p.morphogen, true));
    if (p.has_spiral) st->setProperty("seed_spiral", juce::Array<juce::var> { (double)p.spiral.x, (double)p.spiral.y });
    root->setProperty("lattice_state", juce::var(st));
    return juce::var(root);
}

juce::String preset_to_string(const PresetData& p) { return juce::JSON::toString(preset_to_json(p), false); }

int order_from_filename(const juce::String& fname) {
    const auto f = juce::File::createFileWithoutCheckingPath(fname).getFileName();
    const auto head = f.upToFirstOccurrenceOf("_", false, false);
    return head.isNotEmpty() && head.containsOnly("0123456789") ? head.getIntValue() : (1 << 20);
}

void factory_checks(const PresetData& p, PresetIssues& is) {
    if (p.author.trim().isEmpty()) is.errors.add("factory: author is empty");
    if (p.description.trim().isEmpty()) is.errors.add("factory: description (English) is empty");
    if (p.description_zh.trim().isEmpty()) is.errors.add("factory: description_zh is empty");
    if (p.tags.size() < 2) is.errors.add("factory: needs at least 2 tags");
    if (p.cpu_class != "eco" && p.cpu_class != "standard" && p.cpu_class != "high") is.errors.add("factory: cpu_class must be eco / standard / high");
    for (int i = 0; i < kNumParams; ++i)
        if (!is_macro_param(i) && !p.present[(size_t)i]) is.errors.add(juce::String("factory: parameter \"") + kParams[(size_t)i].id + "\" missing (factory presets are explicit)");
    std::array<int, 8> per{};
    for (int k = 0; k < p.macros.count; ++k) ++per[(size_t)p.macros.t[(size_t)k].macro];
    for (int k = 0; k < 8; ++k) {
        if (juce::String(p.macros.names[(size_t)k].data()).trim().isEmpty()) is.errors.add("factory: macro " + juce::String(k + 1) + " has no name");
        if (per[(size_t)k] == 0) is.errors.add("factory: macro " + juce::String(k + 1) + " has no target");
    }
    for (int k = 0; k < p.macros.count; ++k) {
        const auto& t = p.macros.t[(size_t)k];
        if (t.param == pMode || t.param == pLattice || t.param == pThreads || t.param == pPotential || t.param == pInterp || t.param == pExciter)
            is.errors.add(juce::String("factory: macro targets a structural choice parameter \"") + kParams[(size_t)t.param].id + "\" (rebuilds / clicks)");
        if (t.param == pOutGain || t.param == pInGain) is.errors.add("factory: macro targets a gain stage (breaks loudness normalisation)");
        if (std::abs(t.hi - t.lo) < 1e-6f) is.errors.add("factory: macro " + juce::String(t.macro + 1) + " target \"" + kParams[(size_t)t.param].id + "\" has lo == hi");
    }
    for (int a = 0; a < p.macros.count; ++a)   // the last target of a param wins in EngineHost; factory presets keep one owner
        for (int b = a + 1; b < p.macros.count; ++b)
            if (p.macros.t[(size_t)a].param == p.macros.t[(size_t)b].param)
                is.errors.add(juce::String("factory: parameter \"") + kParams[(size_t)p.macros.t[(size_t)a].param].id + "\" is targeted by more than one macro entry");
    const int lc = (int)p.values[(size_t)pLattice], L = lc <= 0 ? 8 : (lc == 1 ? 16 : 32);   // = lattice_size_from_choice
    if (L >= 32) {
        if (p.values[(size_t)pPotential] >= 1.5f) is.errors.add("factory: 32x32 must not ask for HQ exact (plays as HQ fast; use Quartic or HQ fast)");
        if (p.values[(size_t)pThreads] != 0.f) is.errors.add("factory: 32x32 presets must use Threads = Auto");
    }
    if (p.values[(size_t)pThreads] != 0.f) is.errors.add("factory: Threads must be Auto (the host machine decides)");
}

}  // namespace morpho_plugin
