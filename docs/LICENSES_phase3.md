# Phase 3 third-party licences (MORPHO-PHS plugin)

| component | version / commit | licence | how it is used |
|---|---|---|---|
| JUCE | 8.0.9 (tag, f72bad6) | **dual: AGPLv3 or commercial JUCE licence** (JUCE 8 dropped the GPLv3 option) | framework for the plugin, editor and CLI; statically linked |
| clap-juce-extensions | 7adee3a | MIT | CLAP wrapper (target MorphoPHS_CLAP) |
| CLAP SDK (submodule of the above) | 1.2.7 (29ffcc2) | MIT | CLAP headers |
| clap-helpers (submodule) | a61bcdf | MIT | CLAP helper templates |
| Steinberg VST3 SDK (bundled in JUCE) | as shipped with JUCE 8.0.9 | **Steinberg VST3 License or GPLv3** (`third_party/JUCE/modules/juce_audio_processors/format_types/VST3_SDK/LICENSE.txt`) | VST3 wrapper; AGPLv3 distribution of the plugin is GPLv3-compatible, closed-source distribution uses the Steinberg licence |
| pluginval (tool only, not linked) | latest release (downloaded 30 Oct 2026) | GPLv3 | validation |

## What this means
* **The current build is under AGPLv3.** Any binary distributed from it (VST3/CLAP/Standalone) must come with the
  complete corresponding source of MORPHO-PHS under AGPLv3, unless the user holds a JUCE commercial licence (Indie
  or Pro). JUCE "Personal/Starter" tiers are free with revenue caps; check the current terms on juce.com before
  release.
* For **closed-source or commercial distribution**, a JUCE commercial licence is required, and VST3 distribution
  then falls under the Steinberg VST3 licence. No code changes are needed. (JUCE 8 has no mandatory splash screen;
  the build sets `JUCE_DISPLAY_SPLASH_SCREEN=0`.)
* The engine headers (`include/morpho_phs/`) do not depend on JUCE. They can be licensed separately by the project
  owner.
* Not chosen yet: the project's own licence for its original code. **This is a user decision.**
