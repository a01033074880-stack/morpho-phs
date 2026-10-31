<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# Phase 4 licensing (MORPHO-PHS)

## Project licence (decision D6)
MORPHO-PHS — the engine headers (`include/morpho_phs/`), both plugins, `morpho_cli`, the tests, the Python and
PowerShell scripts, and the presets and documentation — is licensed under the **GNU Affero General Public License,
version 3** (`AGPL-3.0-only`). The full text is in [`LICENSE`](../LICENSE), copied verbatim from
<https://www.gnu.org/licenses/agpl-3.0.txt>. The file is byte-identical to Debian's
`/usr/share/common-licenses/AGPL-3.0` (sha256 `0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0`).

Every first-party source file (`*.hpp`, `*.h`, `*.cpp`, `*.py`, `*.ps1`, `*.sh`, `CMakeLists.txt`) starts with
the comment `SPDX-License-Identifier: AGPL-3.0-only`. Files under `third_party/` keep their own licences.

`-only` rather than `-or-later` was chosen as the conservative default. Moving to `AGPL-3.0-or-later` is a
one-line change per file; it is listed as an open decision for Phase 5.

## Third-party components (unchanged from Phase 3)
| component | version / commit | licence | how it is used |
|---|---|---|---|
| JUCE | 8.0.9 (tag, f72bad6) | dual: AGPLv3 or commercial JUCE licence | framework for both plugins, the editor and the CLI; statically linked. **Used under its AGPLv3 option**, which matches the project licence |
| clap-juce-extensions | 7adee3a | MIT | CLAP wrapper (targets `MorphoPHS_CLAP`, `MorphoPHSFX_CLAP`) |
| CLAP SDK (submodule) | 1.2.7 (29ffcc2) | MIT | CLAP headers |
| clap-helpers (submodule) | a61bcdf | MIT | CLAP helper templates |
| Steinberg VST3 SDK (bundled in JUCE) | as shipped with JUCE 8.0.9 | Steinberg VST3 License or GPLv3 | VST3 wrapper. GPLv3 is compatible with AGPLv3 distribution (GPLv3 §13 / AGPLv3 §13) |
| Windows `avrt.lib` (MMCSS) | Windows SDK | system library | `AvSetMmThreadCharacteristicsW` for worker threads (decision D5); dynamically linked system DLL |
| pluginval (tool only, not linked) | release downloaded 30 Oct 2026 | GPLv3 | validation |

## What AGPLv3 means in practice
* Anyone who receives a binary (VST3 / CLAP / Standalone / `morpho_cli`) must be offered the complete
  corresponding source under AGPLv3. The simplest way is to ship the source zip next to the binaries or link to a
  public repository.
* AGPLv3 §13 (network use) only matters if the software is offered to users over a network, e.g. a hosted render
  service. A local plugin is not affected.
* MIT (CLAP) and the GPLv3 option of the VST3 SDK are compatible with AGPLv3. No extra notices are needed beyond
  keeping their licence files, which stay in `third_party/`.
* Closed-source or commercial distribution would need the copyright holder to relicense the first-party code
  **and** a JUCE commercial licence (plus the Steinberg VST3 licence for VST3). No code changes are needed for that.
