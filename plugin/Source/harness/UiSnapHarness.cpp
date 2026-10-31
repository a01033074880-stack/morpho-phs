// SPDX-License-Identifier: AGPL-3.0-only
// Phase 6a UI snapshot + paint-time harness (not shipped).  Builds the real processor + editor of one variant, drives
// audio and brush events so the hero shows a live field, then renders PNGs at min / default / max size x 1x / 2x and
// times LatticeView::paint.  Run under a display (xvfb-run on Linux):
//   morpho_ui_snap_<variant> <out_dir> [program_index]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"
#include "../PluginEditor.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace morpho_plugin;

namespace {
struct Driver {
    MorphoProcessor& p;
    juce::AudioBuffer<float> buf{2, 128};
    juce::MidiBuffer midi;
    std::mt19937 rng{7};
    long samples = 0;
    void blocks(int n, bool fx) {
        std::normal_distribution<float> nd(0.f, 1.f);
        for (int b = 0; b < n; ++b) {
            buf.clear();
            if (fx) {   // drum-like input: a decaying noise burst every 0.25 s
                for (int i = 0; i < 128; ++i) {
                    const long t = (samples + i) % 12000;
                    const float env = std::exp(-(float)t / 900.f) * 0.5f;
                    const float v = env * nd(rng);
                    buf.setSample(0, i, v); buf.setSample(1, i, v);
                }
            }
            p.processBlock(buf, midi);
            midi.clear();
            samples += 128;
        }
    }
};
void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
bool write_png(const juce::Image& img, const juce::File& f) {
    f.deleteFile();
    juce::FileOutputStream os(f);
    if (!os.openedOk()) return false;
    juce::PNGImageFormat png;
    return png.writeImageToStream(img, os);
}
template <typename T> T* find_child(juce::Component& root) {
    if (auto* t = dynamic_cast<T*>(&root)) return t;
    for (auto* c : root.getChildren()) if (auto* r = find_child<T>(*c)) return r;
    return nullptr;
}
}  // namespace

#define STEP(x) do { std::fprintf(stderr, "[snap] %s\n", x); std::fflush(stderr); } while (0)
static int run_harness(int argc, const char* const* argv) {
    STEP("init");
    const juce::File out(argc > 1 ? juce::String(argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    out.createDirectory();
    const juce::String tag = kIsFxVariant ? "fx" : "inst";
    auto proc = std::make_unique<MorphoProcessor>();
    proc->setPlayConfigDetails(kIsFxVariant ? 2 : 0, 2, 48000.0, 128);
    proc->prepareToPlay(48000.0, 128);
    if (argc > 2) proc->setCurrentProgram(juce::String(argv[2]).getIntValue());
    STEP("prepared");
    for (int i = 0; i < proc->getNumPrograms(); ++i) std::fprintf(stderr, "  program %d: %s\n", i, proc->getProgramName(i).toRawUTF8());
    std::fprintf(stderr, "  current: %s\n", proc->getProgramName(proc->getCurrentProgram()).toRawUTF8());
    Driver d{*proc};
    d.blocks(40, kIsFxVariant);

    STEP("audio ok");
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc->createEditor());
    STEP("editor");
    auto* me = dynamic_cast<MorphoEditor*>(ed.get());
    ed->addToDesktop(0);
    ed->setVisible(true);
    pump(100);
    STEP("shown");
    auto& view = me->panel().view();
    // excite: notes (instrument) + brushes
    if (!kIsFxVariant) for (int n : {48, 55, 64}) d.midi.addEvent(juce::MidiMessage::noteOn(1, n, 0.6f), 0);
    const struct { int type; float x, y, a; } ev[] = {
        {UiEvent::Pluck, 0.30f, 0.55f, 1.f}, {UiEvent::SeedSpiral, 0.70f, 0.40f, 1.f}, {UiEvent::Morphogen, 0.55f, 0.75f, 0.8f},
        {UiEvent::ChiselAdd, 0.45f, 0.30f, 1.f}, {UiEvent::ChiselAdd, 0.48f, 0.33f, 1.f}, {UiEvent::ChiselAdd, 0.62f, 0.62f, 1.f}};
    for (auto& e : ev) { UiEvent u; u.type = e.type; u.x = e.x; u.y = e.y; u.amount = e.a * 0.6f; proc->send_event(u); d.blocks(4, kIsFxVariant); }
    for (int k = 0; k < 30; ++k) { d.blocks(12, kIsFxVariant); pump(5); }   // ~0.36 s of audio, publisher active

    juce::String json = "{\n  \"variant\": \"" + tag + "\",\n  \"publish_hz\": " + juce::String(proc->host().engine().snapshot_hz.load()) + ",\n  \"sizes\": [\n";
    const int widths[3] = {theme::kMinW, theme::kDesignW, theme::kMaxW};
    const char* names[3] = {"min", "default", "max"};
    bool first = true;
    for (int wi = 0; wi < 3; ++wi) {
        const int w = widths[wi], h = juce::roundToInt(w * (double)theme::kDesignH / theme::kDesignW);
        ed->setSize(w, h);
        pump(30);
        for (float sc : {1.f, 2.f}) {
            for (int k = 0; k < 4; ++k) { d.blocks(12, kIsFxVariant); pump(5); }
            view.poll_now();
            const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, sc);
            const auto f = out.getChildFile(tag + "_" + names[wi] + "_" + juce::String((int)sc) + "x.png");
            write_png(img, f);
            // paint timing of the hero (the only per-frame cost), at this size and scale
            const float phys = (float)w / theme::kDesignW * sc;
            (void)view.createComponentSnapshot(view.getLocalBounds(), true, phys);   // warm-up: geometry rebuild for this scale
            view.reset_paint_stats();
            for (int k = 0; k < 40; ++k) {
                d.blocks(12, kIsFxVariant); pump(1); view.poll_now();
                (void)view.createComponentSnapshot(view.getLocalBounds(), true, phys);
            }
            const auto st = view.paint_stats(phys);
            std::printf("%s %-7s %dx%d @%gx  hero render %dx%d px  paint median %.0f us  p95 %.0f us  (rebuilds %d, last %.0f us)\n", tag.toRawUTF8(), names[wi], w, h, sc,
                        st.render_w, st.render_h, st.median_us, st.p95_us, st.rebuilds, st.rebuild_us);
            json << (first ? "" : ",\n") << "    {\"size\": \"" << names[wi] << "\", \"w\": " << w << ", \"h\": " << h << ", \"scale\": " << sc
                 << ", \"render_px\": [" << st.render_w << ", " << st.render_h << "], \"paint_median_us\": " << juce::String(st.median_us, 1)
                 << ", \"paint_p95_us\": " << juce::String(st.p95_us, 1) << ", \"rebuilds\": " << st.rebuilds << ", \"rebuild_us\": " << juce::String(st.rebuild_us, 0) << ", \"png\": \"" << f.getFileName() << "\"}";
            first = false;
        }
    }
    json << "\n  ],\n";
    // eco timing at default size, 2x
    ed->setSize(theme::kDesignW, theme::kDesignH); pump(30);
    me->panel().set_eco(true); pump(30);
    (void)view.createComponentSnapshot(view.getLocalBounds(), true, 2.f);
    view.reset_paint_stats();
    for (int k = 0; k < 40; ++k) { d.blocks(12, kIsFxVariant); pump(1); view.poll_now(); (void)view.createComponentSnapshot(view.getLocalBounds(), true, 2.f); }
    const auto es = view.paint_stats(2.f);
    json << "  \"eco_default_2x\": {\"paint_median_us\": " << juce::String(es.median_us, 1) << ", \"paint_p95_us\": " << juce::String(es.p95_us, 1)
         << ", \"fps\": " << view.current_fps() << ", \"publish_hz\": " << proc->host().engine().snapshot_hz.load() << "},\n";
    std::printf("%s eco default @2x paint median %.0f us p95 %.0f us fps %d\n", tag.toRawUTF8(), es.median_us, es.p95_us, view.current_fps());
    me->panel().set_eco(false); pump(30);
    // drawer + about at the default size
    me->panel().set_advanced(true, false); pump(30);
    for (int k = 0; k < 3; ++k) { d.blocks(12, kIsFxVariant); pump(5); }
    write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), out.getChildFile(tag + "_advanced_1x.png"));
    me->panel().set_advanced(false, false);
    me->panel().show_about(true); pump(30);
    write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), out.getChildFile(tag + "_about_1x.png"));
    write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.f), out.getChildFile(tag + "_about_2x.png"));
    me->panel().show_about(false);
    // Phase 6b preset browser: All, then a category + tag filter, at 1x / 2x
    // a loaded preset + two favourites so the chip, the detail card and the hexagrams show real state (undone below)
    const juce::String fav_a = proc->getNumPrograms() > 4 ? proc->factory().getReference(0).name : juce::String(),
                       fav_b = proc->getNumPrograms() > 4 ? proc->factory().getReference(4).name : juce::String();
    const bool had_a = proc->is_favourite(fav_a), had_b = proc->is_favourite(fav_b);
    if (!had_a) proc->toggle_favourite(fav_a);
    if (!had_b) proc->toggle_favourite(fav_b);
    proc->setCurrentProgram(4); d.blocks(20, kIsFxVariant); pump(30);
    me->panel().show_browser(true); pump(30);
    write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), out.getChildFile(tag + "_browser_all_1x.png"));
    write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.f), out.getChildFile(tag + "_browser_all_2x.png"));
    {
        const auto& f = proc->factory();
        const juce::String cat = f.size() > 6 ? f.getReference(6).category : juce::String();
        const juce::String tg = f.size() > 6 && f.getReference(6).tags.size() > 0 ? f.getReference(6).tags[0] : juce::String();
        me->panel().browser().set_filter(cat, {}); pump(30);
        write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.f), out.getChildFile(tag + "_browser_category_2x.png"));
        write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), out.getChildFile(tag + "_browser_category_1x.png"));
        me->panel().browser().set_filter(PresetBrowser::kAll, tg); pump(30);
        write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), out.getChildFile(tag + "_browser_tag_1x.png"));
        me->panel().browser().set_filter(PresetBrowser::kFav, {}); pump(30);
        write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.f), out.getChildFile(tag + "_browser_favourites_1x.png"));
        std::printf("%s browser: %d factory presets, filter cat='%s' tag='%s'\n", tag.toRawUTF8(), f.size(), cat.toRawUTF8(), tg.toRawUTF8());
    }
    me->panel().browser().set_filter(PresetBrowser::kAll, {});
    me->panel().show_browser(false); pump(30);
    if (!had_a) proc->toggle_favourite(fav_a);
    if (!had_b) proc->toggle_favourite(fav_b);
    write_png(ed->createComponentSnapshot(ed->getLocalBounds(), true, 2.f), out.getChildFile(tag + "_header_2x.png"));
    // hidden editor -> publisher off
    ed->setVisible(false); pump(400);
    const int hiddenHz = proc->host().engine().snapshot_hz.load();
    ed.reset();
    const int closedHz = proc->host().engine().snapshot_hz.load();
    json << "  \"publish_hz_hidden\": " << hiddenHz << ",\n  \"publish_hz_closed\": " << closedHz << "\n}\n";
    std::printf("%s publish_hz hidden=%d closed=%d\n", tag.toRawUTF8(), hiddenHz, closedHz);
    out.getChildFile(tag + "_paint.json").replaceWithText(json);
    proc->releaseResources();
    proc.reset();
    return 0;
}

// run as a real JUCE GUI application (the X11 peer needs the app's window-system initialisation)
class SnapApp : public juce::JUCEApplicationBase {
public:
    const juce::String getApplicationName() override { return "morpho_ui_snap"; }
    const juce::String getApplicationVersion() override { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override { return true; }
    void anotherInstanceStarted(const juce::String&) override {}
    void suspended() override {}
    void resumed() override {}
    void systemRequestedQuit() override { quit(); }
    void unhandledException(const std::exception*, const juce::String&, int) override {}
    void shutdown() override {}
    void initialise(const juce::String&) override {
        juce::MessageManager::callAsync([this] {
            const auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
            std::vector<std::string> st{"morpho_ui_snap"}; for (auto& a : args) st.push_back(a.toStdString());
            std::vector<const char*> av; for (auto& x : st) av.push_back(x.c_str());
            const int rc = run_harness((int)av.size(), av.data());
            std::fflush(stdout); std::fflush(stderr);
            std::_Exit(rc);   // harness only: skip X11 teardown (it can block under a bare Xvfb)
        });
    }
};
START_JUCE_APPLICATION(SnapApp)
