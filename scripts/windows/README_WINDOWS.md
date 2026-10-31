<!-- SPDX-License-Identifier: AGPL-3.0-only -->
# MORPHO-PHS on Windows: build and benchmark (Phase 5 / 5b)

These scripts were written on the Linux box, which cannot run Windows. `build_windows.ps1` is unchanged since Phase 4 (it
built and ran on the i9 on 2026-10-30); `bench_windows.ps1` was rewritten for Phase 5 and ran on the i9 on 2026-10-31 (it stopped after 11 of
27 runs because a dedicated-pool run hung; the hang is fixed in Phase 5b). Phase 5b added per-run timeouts, `-Resume`
and new variants; that version has been parse-checked and dry-run on Linux only (pwsh 7, stubbed WMI/powercfg, a fake
morpho_cli that hangs or crashes on purpose).
They use the same CMake project that builds and passes pluginval on Linux (plugin/CMakeLists.txt). The MSVC flags are
already in that file: `/O2 /fp:precise /arch:AVX2 /permissive- /utf-8 /bigobj`.

## Requirements (one-time)
- Visual Studio 2022 or 2026 (Community is fine) with the **"Desktop development with C++"** workload.
- CMake ≥ 3.22 and Git:
  `winget install Kitware.CMake Git.Git`
- Optional, only for the Ninja option: `winget install Ninja-build.Ninja`.
- Internet access on the first run. The script clones JUCE 8.0.9 and clap-juce-extensions@7adee3a into `third_party\`,
  because the zip does not include them.
- The CPU must support AVX2. Every i9 does.

## Steps (PowerShell, in the unzipped project root)
```powershell
cd <path>\morpho-phs-workspace
powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja
dist\windows\morpho_stress.exe 15 1          # thread-pool stress test, ~1 min; exit 0 = pass, 2 = HANG
powershell -ExecutionPolicy Bypass -File scripts\windows\bench_windows.ps1
```

**Build output:**
- `dist\windows\`: two plugins from one project, each as VST3, CLAP and Standalone, plus `morpho_cli.exe`:
  - `MORPHO-PHS.vst3/.clap/.exe`: the **instrument** (MIDI in, stereo out; code `Mpsi`, `lab.morpho.phs.instrument`);
  - `MORPHO-PHS FX.vst3/.clap/.exe`: the **effect** (stereo in/out; code `Mpsx`, `lab.morpho.phs.fx`).
  - The Phase 3 plugin (code `Mphs`) is retired; DAW sessions that used it will not find it.
- The build log is `logs\build_windows_*.txt`.
- To install the plugins, run the build again **as administrator** with `-Install`. This copies them to
  `C:\Program Files\Common Files\VST3` and `\CLAP`. Alternatively, copy the .vst3 there by hand.

**Benchmark output:** `experiments\results\phase5\windows\<PC>_<time>\` with `sysinfo.json`, `runlog.csv` (one line per
variant run: order, start time, power scheme + power-mode overlay, AC/battery, WMI effective clock), `<variant>_r<k>.json/.txt`
and `SUMMARY.txt` (criterion rows aggregated over repeats: median µs/sample, worst and median p99/budget, total overruns,
barrier OS waits `bOSw`, clock; verdicts; ALL-PASS line). Zip that folder and send it back.

**Benchmark tips:**
- Close the DAW and browsers; keep the laptop on mains power; use the *Best performance* power mode.
- Default run ≈ 31 min (13 variants, 29 thread configurations × 3 repeats × 10 s, 20 s cooldown before every variant
  run, seeded random order); `-Repeats 2` ≈ 21 min. `-Quick` ≈ 6 min (2 s, 1 repeat, 5 s cooldown).
- Every morpho_cli call has a timeout (3 × expected duration, at least 60 s). A run that times out is killed, its
  finished rows are kept, a `TIMEOUT` row and a runlog line with `exit=TIMEOUT` are written, and the script continues.
  A crashing variant is recorded as `FAILED` and the script continues too.
- An interrupted or partly failed run can be completed with `-Resume <folder>`: every (variant, repeat) that already has
  `exit=0` in that folder's `runlog.csv` is skipped.

## Options
| script | option | effect |
|---|---|---|
| build | `-VsPath C:\SDKs\AetherBuildTools` | use this VS / Build Tools install (runs its vcvars64) |
| build | `-Generator Ninja` | Ninja + MSVC (runs vcvars64 itself) instead of the VS solution |
| build | `-NoClap` | skip CLAP (use this if the clap-juce-extensions clone fails) |
| build | `-Clean` | fresh configure |
| build | `-Install` | copy VST3/CLAP into Common Files (admin) |
| bench | `-Quick` | 2 s per configuration, 1 repeat, 5 s cooldown |
| bench | `-Variants 1T,1T_csr,p5_caller_cpuset` | run only these variants (names: see the script header) |
| bench | `-Resume <result folder>` | continue in that folder; runs only (variant, repeat) pairs without `exit=0` in its runlog |
| bench | `-TimeoutFactor 3` / `-MinTimeout 60` | per-call timeout = factor × expected duration, at least this many s |
| bench | `-Repeats 3` / `-Seed 5` / `-Cooldown 20` / `-Seconds 10` | repeats, shuffle seed, cooldown s, seconds per configuration |
| bench | `-Blocks 128` / `-Sizes 32` | block sizes / lattice sizes (defaults shown) |
| bench | `-Unpaced` | blocks back to back instead of host-like pacing |
| bench | `-Load 3` | add 3 busy background threads |

## Plugin real-time settings (environment variables, read when the plugin loads)
| variable | values | default |
|---|---|---|
| `MORPHO_PHS_PIN` | `cpuset` (CPU Sets on the clean P-cores: no core 0, one logical CPU per core) / `pcores` (hard pin) / `pset` (Phase 4 soft affinity) / `none` | `cpuset` |
| `MORPHO_PHS_MMCSS` | `1` / `0` (MMCSS "Pro Audio" on worker threads) | `1` |
| `MORPHO_PHS_POOL` | `caller` (host thread computes one share) / `auto` / `dedicated` | `caller` |
| `MORPHO_PHS_KERNEL` | `stencil` (Phase 5 bipartite SIMD kernel) / `csr` (Phase 2–4 gather kernel, bit-identical) | `stencil` |
| `MORPHO_PHS_ERSAV` | `1` / `0` (element-wise relaxed SAV, experimental) | `0` |
| `MORPHO_PHS_INBLOCK_WAIT` | `1` / `0` (`1` = Phase 4 hybrid in-block barrier with OS waits) | `0` (pure spin) |
| `MORPHO_PHS_ECOQOS` | `1` / `0` (`1` = leave Windows EcoQoS power throttling alone) | `0` (throttling opted out) |

The "Threads" parameter's **Auto** choice depends on lattice size, mode and potential (Phase 5c, from the i9 final
data, 3 repeats x 3750 blocks), capped by the number of clean P-cores:

| lattice | FX, Quartic | Instrument | HQ (fast log-cosh) | HQ (exact log-cosh) |
|---|---|---|---|---|
| 32x32 | 2 (i9: 0/11250 overruns, worst p99/budget 0.31) | 3 (0/11250, 0.48) | 3, shown as "HIGH CPU" (1/11250, 0.76) | plays as HQ fast (see below) |
| 16x16 | 1 | 1 | 1 | 2, shown as "HIGH CPU" |
| 8x8 | 1 | 1 | 1 | 1 |

- Changing mode or potential at 32x32 while Threads = Auto rebuilds the lattice with the new thread count (message
  thread, 256-sample crossfade), the same way a lattice-size change does.
- **HQ exact is limited to 16x16 or smaller.** On the i9 it needs 24-58 us/sample at 32x32 with 1-3 threads (budget
  20.8). The parameter value is kept (presets, automation and host state stay valid), but at 32x32 the engine plays
  HQ fast and the editor shows "POTENTIAL - HQ FAST @32". `morpho_cli validate` prints a warning (not an error) for such
  presets. Benchmarks can measure the real HQ exact with `morpho_cli bench ... --potential 2 --hq-exact-any-L 1`; the
  `hq_L32` bench variant does this.
- The editor's THREADS caption shows what Auto resolves to, and the tooltips give the CPU cost of each potential.
- "1" stays selectable for a strictly single-threaded plugin (i9 32x32 FX 1T: 1/11250 overruns, p99/budget 0.50).
- The dedicated pool (`MORPHO_PHS_POOL=dedicated`) stays opt-in: on the i9 it had 22-60 overruns per 11250 blocks.

**Stress test (`morpho_stress.exe <seconds per part> <seed>`):** P1/P2 tiny blocks on both pools, P3 repeated
`prepare()` reconfiguration, P4 plugin-like thread-count switches at 32x32. A 10 s watchdog without progress prints the
pool state and exits with code 2 (HANG). Since Phase 5c, P3 passes when there is no hang, at least min(100, 10 x seconds)
stop/start cycles completed and no single cycle took 2 s or more; it prints the cycle cost (mean/median/p95/max) and the
`prepare()` (stop + restart) cost. On Windows one cycle costs about 60 ms (thread creation + MMCSS registration), so the
old fixed rule of 1000 cycles failed the i9 without a hang (256 cycles in 15 s).

## Phase 6b: new factory presets (after unzipping the Phase 6b sync zip)
```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\apply_phase6b_sync.ps1   # removes the 27 old presets\NN_*.morpho (kept in presets\retired)
powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja
dist\windows\morpho_cli.exe validate presets --factory 1                          # expect 57 OK, 0 errors
dist\windows\morpho_cli.exe qa presets logs\qa6b --play 8 --tail 8 --cost 4       # optional: i9 render + timing per preset (qa.json; writes about 340 MB of WAVs)
```
The sync zip cannot delete files, so run the helper first: otherwise the old presets are compiled into the plugin next
to the new bank. The preset browser opens from the preset name in the header; favourites are stored in
`%APPDATA%\MORPHO-PHS\favourites_inst.txt` / `favourites_fx.txt`. The `cpu_class` of each preset is an i9 estimate from
box timings (Phase 5 box-to-i9 ratios). The optional `qa` run gives the real i9 cost: send `logs\qa6b\qa.json` back
to re-class the bank.

## Phase 6b2: i9 QA fixes (after unzipping the Phase 6b2 sync zip)
```powershell
powershell -ExecutionPolicy Bypass -File scripts\windows\apply_phase6b2_sync.ps1   # deletes presets\029_big_spiral_32.morpho
powershell -ExecutionPolicy Bypass -File scripts\windows\build_windows.ps1 -VsPath C:\SDKs\AetherBuildTools -Generator Ninja
dist\windows\morpho_cli.exe validate presets --factory 1                          # expect 57 OK, 0 errors
dist\windows\morpho_cli.exe qa presets logs\qa6b2 --play 8 --tail 8 --cost 4 --only "006_,007_,008_,009_,010_,011_,012_,013_,015_,021_,029_,032_,101_,102_,103_,104_,105_,106_,107_,114_,118_,120_,122_,125_"
```
The rebuild is required: the presets are compiled into the plugin, and `morpho_cli qa` gained `--only`, `--seed`,
`--noise portable|std` (default portable, so the i9 and the box render the same FX input) and `--no-wav 1`.
Send `logs\qa6b2\qa.json` back: it replaces the estimated CPU class of Big Spiral and confirms 0 limiter samples.

## Optional: pluginval on Windows
```powershell
Invoke-WebRequest https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip -OutFile pluginval.zip
Expand-Archive pluginval.zip -DestinationPath tools\pluginval -Force
tools\pluginval\pluginval.exe --strictness-level 10 --validate-in-process --output-dir logs dist\windows\MORPHO-PHS.vst3
tools\pluginval\pluginval.exe --strictness-level 10 --validate-in-process --output-dir logs "dist\windows\MORPHO-PHS FX.vst3"
```

## Troubleshooting
- **"running scripts is disabled"**: keep the `-ExecutionPolicy Bypass` shown above.
- **`Unsupported VS major version`**: pass `-Generator Ninja`, or update CMake. VS 2026 needs CMake ≥ 4.2.
- **Antivirus slowing the build**: exclude the `build\` folder.
